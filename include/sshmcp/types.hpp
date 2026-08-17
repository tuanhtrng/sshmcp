#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace sshmcp {

inline constexpr auto DEFAULT_TIMEOUT_MS = std::int64_t{60'000};
inline constexpr auto MAX_TIMEOUT_MS = std::int64_t{600'000};
inline constexpr auto MAX_OUTPUT_CHARS = std::size_t{50'000};
inline constexpr auto MAX_READ_BYTES = std::size_t{1'048'576};
inline constexpr auto MAX_BINARY_BYTES = std::size_t{8'388'608};
inline constexpr auto MAX_SESSIONS = std::size_t{4};
inline constexpr auto MAX_FORWARDS = std::size_t{8};
inline constexpr auto DEFAULT_SESSION_IDLE_MS = std::int64_t{1'800'000};

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

enum class stream_t { STDOUT, STDERR };

struct chunk_t {
    std::string data;
    bool is_closed{};
};

struct session_result_t {
    spawn_result_t result;
    bool is_session_dead{};
};

using spawn_fn_t = std::function<std::expected<spawn_result_t, error_t>(
    spawn_request_t const&)>;

struct config_t {
    std::string target;
    std::vector<std::string> ssh_exe{"ssh"};
    std::vector<std::string> ssh_args;
    log_level_t log_level{log_level_t::INFO};
    std::int64_t session_idle_ms{DEFAULT_SESSION_IDLE_MS};
    std::vector<std::string> allow;
};

using session_exec_fn_t =
    std::function<std::expected<session_result_t, error_t>(
        std::string_view, std::string_view, std::int64_t)>;
using session_close_fn_t = std::function<bool(std::string_view)>;
using forward_open_fn_t = std::function<std::expected<std::string, error_t>(
    int, std::string_view, int)>;
using forward_close_fn_t = std::function<bool(int)>;

struct context_t {
    config_t config;
    spawn_fn_t spawn;
    session_exec_fn_t session_exec;
    session_close_fn_t session_close;
    forward_open_fn_t forward_open;
    forward_close_fn_t forward_close;
};

struct tool_result_t {
    std::string text;
    bool is_error{};
};

} // namespace sshmcp
