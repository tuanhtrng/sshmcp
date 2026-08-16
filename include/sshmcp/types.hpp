#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace sshmcp {

inline constexpr auto DEFAULT_TIMEOUT_MS = std::int64_t{60'000};
inline constexpr auto MAX_TIMEOUT_MS = std::int64_t{600'000};
inline constexpr auto MAX_OUTPUT_CHARS = std::size_t{50'000};
inline constexpr auto MAX_READ_BYTES = std::size_t{1'048'576};

enum class log_level_t { OFF, INFO, DEBUG };

struct error_t {
    std::string message;
};

struct spawn_request_t {
    std::vector<std::string> argv;
    std::string stdin_data;
    std::int64_t timeout_ms{DEFAULT_TIMEOUT_MS};
};

struct spawn_result_t {
    std::string stdout_text;
    std::string stderr_text;
    int exit_code{};
    bool is_timed_out{};
};

using spawn_fn_t = std::function<std::expected<
    spawn_result_t, error_t>(spawn_request_t const&)>;

struct config_t {
    std::string target;
    std::vector<std::string> ssh_exe{"ssh"};
    std::vector<std::string> ssh_args;
    log_level_t log_level{log_level_t::INFO};
};

struct context_t {
    config_t config;
    spawn_fn_t spawn;
};

struct tool_result_t {
    std::string text;
    bool is_error{};
};

}  // namespace sshmcp
