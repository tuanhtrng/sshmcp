#pragma once

#include <sshmcp/types.hpp>

#include <chrono>
#include <expected>
#include <print>
#include <string>

namespace sshmcp {

template <typename Derived>
class subprocess_base_t {
  public:
    explicit subprocess_base_t(log_level_t log_level) : log_level{log_level} {}

    auto init_stdio() -> void {
        static_cast<Derived*>(this)->init_stdio_impl();
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

  private:
    log_level_t log_level;
};

} // namespace sshmcp
