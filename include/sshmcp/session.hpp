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

} // namespace sshmcp
