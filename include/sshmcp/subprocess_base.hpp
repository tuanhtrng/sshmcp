#pragma once

#include <sshmcp/types.hpp>

#include <chrono>
#include <expected>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace sshmcp {

template <typename Derived>
class subprocess_base_t {
  public:
    explicit subprocess_base_t(log_level_t log_level) : log_level{log_level} {}

    auto init_stdio() -> void {
        static_cast<Derived*>(this)->init_stdio_impl();
    }

    auto harden() -> void {
        static_cast<Derived*>(this)->harden_impl();
    }

    auto run(spawn_request_t const& request)
        -> std::expected<spawn_result_t, error_t> {
        if (log_level == log_level_t::DEBUG) {
            auto line = std::string{};
            for (auto const& arg : request.argv) {
                line += arg;
                line += ' ';
            }
            std::println(stderr, "sshmcp: spawn: {}", line);
        }
        auto const start = std::chrono::steady_clock::now();
        auto result = static_cast<Derived*>(this)->spawn_impl(request);
        auto const elapsed_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start)
                .count();
        if (log_level != log_level_t::OFF) {
            if (result) {
                std::println(stderr, "sshmcp: exit {} in {} ms{}",
                             result->exit_code, elapsed_ms,
                             result->is_timed_out ? " (timeout)" : "");
            } else {
                std::println(stderr, "sshmcp: spawn error: {}",
                             result.error().message);
            }
        }
        return result;
    }

    auto stream_spawn(std::vector<std::string> const& argv) {
        if (log_level == log_level_t::DEBUG) {
            std::println(stderr, "sshmcp: stream spawn: {}",
                         argv.empty() ? "" : argv.front());
        }
        return static_cast<Derived*>(this)->stream_spawn_impl(argv);
    }

    template <typename StreamId>
    auto stream_write(StreamId const& id, std::string_view data) -> bool {
        return static_cast<Derived*>(this)->stream_write_impl(id, data);
    }

    template <typename StreamId>
    auto stream_read(StreamId const& id, stream_t which,
                     std::chrono::steady_clock::time_point deadline)
        -> std::expected<chunk_t, error_t> {
        return static_cast<Derived*>(this)->stream_read_impl(id, which,
                                                             deadline);
    }

    template <typename StreamId>
    auto stream_kill(StreamId const& id) -> void {
        static_cast<Derived*>(this)->stream_kill_impl(id);
    }

  private:
    log_level_t log_level;
};

} // namespace sshmcp
