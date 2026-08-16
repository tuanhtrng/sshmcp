#pragma once

#include <sshmcp/ssh.hpp>
#include <sshmcp/types.hpp>

#include <charconv>
#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace sshmcp {

inline auto session_payload(std::uint64_t counter, std::string_view command)
    -> std::string {
    return std::format("{{\n"
                       "{}\n"
                       "}} </dev/null\n"
                       "printf '\\n@sshmcp:{}:%s@\\n' \"$?\"\n"
                       "printf '\\n@sshmcp:{}@\\n' >&2\n",
                       command, counter, counter);
}

struct scan_result_t {
    bool is_found{};
    std::string output;
    int exit_code{};
};

// Marker must begin a line: at buffer start or right after \n.
inline auto find_marker(std::string_view buffer, std::string_view marker)
    -> std::size_t {
    auto from = std::size_t{0};
    while (true) {
        auto const pos = buffer.find(marker, from);
        if (pos == std::string_view::npos) {
            return std::string_view::npos;
        }
        if (pos == 0 || buffer[pos - 1] == '\n') {
            return pos;
        }
        from = pos + 1;
    }
}

inline auto strip_injected_newline(std::string_view buffer,
                                   std::size_t marker_pos) -> std::string {
    // The payload printf always injects one leading \n right
    // before the marker line; drop exactly that one.
    auto output = buffer.substr(0, marker_pos);
    if (!output.empty() && output.back() == '\n') {
        output.remove_suffix(1);
    }
    return std::string{output};
}

inline auto scan_stdout(std::string_view buffer, std::uint64_t counter)
    -> scan_result_t {
    auto const head = std::format("@sshmcp:{}:", counter);
    auto const pos = find_marker(buffer, head);
    if (pos == std::string_view::npos) {
        return {};
    }
    auto const digits_at = pos + head.size();
    auto exit_code = 0;
    auto const parsed = std::from_chars(
        buffer.data() + digits_at, buffer.data() + buffer.size(), exit_code);
    auto const tail =
        std::string_view{parsed.ptr, buffer.data() + buffer.size()};
    if (parsed.ec != std::errc{} || !tail.starts_with("@\n")) {
        return {};
    }
    return {.is_found = true,
            .output = strip_injected_newline(buffer, pos),
            .exit_code = exit_code};
}

inline auto scan_stderr(std::string_view buffer, std::uint64_t counter)
    -> scan_result_t {
    auto const marker = std::format("@sshmcp:{}@\n", counter);
    auto const pos = find_marker(buffer, marker);
    if (pos == std::string_view::npos) {
        return {};
    }
    return {.is_found = true,
            .output = strip_injected_newline(buffer, pos),
            .exit_code = 0};
}

template <typename PlatformType>
class session_manager_t {
  public:
    session_manager_t(PlatformType& platform, config_t config)
        : platform{platform}, config{std::move(config)} {}

    auto exec(std::string_view name, std::string_view command,
              std::int64_t timeout_ms)
        -> std::expected<session_result_t, error_t> {
        reap();
        auto found = sessions.find(name);
        if (found == sessions.end()) {
            if (sessions.size() >= MAX_SESSIONS) {
                auto message = std::string{"session limit reached; live:"};
                for (auto const& [live, entry] : sessions) {
                    message += ' ';
                    message += live;
                }
                return std::unexpected{error_t{std::move(message)}};
            }
            auto spawned = platform.stream_spawn(build_ssh_argv(config, "sh"));
            if (!spawned) {
                return std::unexpected{spawned.error()};
            }
            found = sessions.emplace(std::string{name}, entry_t{.id = *spawned})
                        .first;
        }
        auto& entry = found->second;
        entry.last_used = std::chrono::steady_clock::now();
        auto const call = ++counter;
        if (!platform.stream_write(entry.id, session_payload(call, command))) {
            return kill_and_report(found, {}, {}, false);
        }
        auto const deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds{timeout_ms};
        auto out_buffer = std::string{};
        auto err_buffer = std::string{};
        auto out_scan = scan_result_t{};
        auto err_scan = scan_result_t{};
        while (!out_scan.is_found || !err_scan.is_found) {
            auto const now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return kill_and_report(found, out_buffer, err_buffer, true);
            }
            auto const slice =
                std::min(deadline, now + std::chrono::milliseconds{50});
            auto const which =
                !out_scan.is_found ? stream_t::STDOUT : stream_t::STDERR;
            auto const chunk = platform.stream_read(entry.id, which, slice);
            if (!chunk) {
                return kill_and_report(found, out_buffer, err_buffer, false);
            }
            if (which == stream_t::STDOUT) {
                out_buffer += chunk->data;
                out_scan = scan_stdout(out_buffer, call);
            } else {
                err_buffer += chunk->data;
                err_scan = scan_stderr(err_buffer, call);
            }
            if (chunk->is_closed &&
                (!out_scan.is_found || !err_scan.is_found)) {
                return kill_and_report(found, out_buffer, err_buffer, false);
            }
        }
        entry.last_used = std::chrono::steady_clock::now();
        return session_result_t{
            .result = spawn_result_t{.stdout_text = out_scan.output,
                                     .stderr_text = err_scan.output,
                                     .exit_code = out_scan.exit_code,
                                     .is_timed_out = false},
            .is_session_dead = false};
    }

    auto close(std::string_view name) -> bool {
        reap();
        auto const found = sessions.find(name);
        if (found == sessions.end()) {
            return false;
        }
        platform.stream_kill(found->second.id);
        sessions.erase(found);
        return true;
    }

  private:
    struct entry_t {
        typename PlatformType::stream_id_t id;
        std::chrono::steady_clock::time_point last_used{
            std::chrono::steady_clock::now()};
    };
    using session_map_t = std::map<std::string, entry_t, std::less<>>;

    auto reap() -> void {
        auto const now = std::chrono::steady_clock::now();
        auto const idle = std::chrono::milliseconds{config.session_idle_ms};
        for (auto it = sessions.begin(); it != sessions.end();) {
            if (now - it->second.last_used > idle) {
                platform.stream_kill(it->second.id);
                it = sessions.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto kill_and_report(typename session_map_t::iterator it,
                         std::string out_buffer, std::string err_buffer,
                         bool is_timed_out)
        -> std::expected<session_result_t, error_t> {
        platform.stream_kill(it->second.id);
        sessions.erase(it);
        return session_result_t{
            .result = spawn_result_t{.stdout_text = std::move(out_buffer),
                                     .stderr_text = std::move(err_buffer),
                                     .exit_code = -1,
                                     .is_timed_out = is_timed_out},
            .is_session_dead = true};
    }

    PlatformType& platform;
    config_t config;
    session_map_t sessions;
    std::uint64_t counter{};
};

} // namespace sshmcp
