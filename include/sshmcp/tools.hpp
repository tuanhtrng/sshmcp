#pragma once

#include <sshmcp/allow.hpp>
#include <sshmcp/base64.hpp>
#include <sshmcp/jsonrpc.hpp>
#include <sshmcp/ssh.hpp>
#include <sshmcp/types.hpp>
#include <sshmcp/util.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace sshmcp {

inline auto error_result(std::string message) -> tool_result_t {
    return {.text = std::move(message), .is_error = true};
}

inline auto parse_encoding(nlohmann::json const& arguments)
    -> std::expected<bool, error_t> { // true = base64
    if (!arguments.contains("encoding")) {
        return false;
    }
    if (!arguments["encoding"].is_string()) {
        return std::unexpected{error_t{"'encoding' must be a string"}};
    }
    auto const encoding = arguments["encoding"].get<std::string>();
    if (encoding == "text") {
        return false;
    }
    if (encoding == "base64") {
        return true;
    }
    return std::unexpected{
        error_t{"'encoding' must be \"text\" or \"base64\""}};
}

struct exec_tool_t {
    static constexpr auto NAME = std::string_view{"exec"};
    static constexpr auto DESCRIPTION =
        std::string_view{"Run a shell command on the remote host over ssh. "
                         "Returns stdout, stderr and exit_code; a non-zero "
                         "exit_code is a normal result, not an error."};

    static auto schema() -> nlohmann::json {
        return {{"type", "object"},
                {"properties",
                 {{"command",
                   {{"type", "string"},
                    {"description", "shell command to run remotely"}}},
                  {"cwd",
                   {{"type", "string"},
                    {"description", "remote directory to run in"}}},
                  {"timeout_ms",
                   {{"type", "integer"},
                    {"description", "timeout in milliseconds "
                                    "(default 60000, max 600000)"}}},
                  {"session",
                   {{"type", "string"},
                    {"description", "named persistent shell; cwd/env persist "
                                    "across calls with the same name "
                                    "(auto-created, idle-reaped)"}}}}},
                {"required", nlohmann::json::array({"command"})}};
    }

    static auto invoke_session(context_t& context, std::string const& name,
                               std::string const& command,
                               std::int64_t timeout_ms) -> tool_result_t {
        if (!context.session_exec) {
            return error_result("exec: sessions unavailable in this build");
        }
        auto const spawned = context.session_exec(name, command, timeout_ms);
        if (!spawned) {
            return error_result("exec: " + spawned.error().message);
        }
        auto const& inner = spawned->result;
        auto const out = truncate_middle(inner.stdout_text, MAX_OUTPUT_CHARS);
        auto const err = truncate_middle(inner.stderr_text, MAX_OUTPUT_CHARS);
        return {.text = dump_json(
                    {{"stdout", out.text},
                     {"stderr", err.text},
                     {"exit_code", inner.is_timed_out ? -1 : inner.exit_code},
                     {"is_truncated", out.is_truncated || err.is_truncated},
                     {"is_timed_out", inner.is_timed_out},
                     {"session", name},
                     {"is_session_dead", spawned->is_session_dead}}),
                .is_error = false};
    }

    static auto invoke(context_t& context, nlohmann::json const& arguments)
        -> tool_result_t {
        if (!arguments.contains("command") ||
            !arguments["command"].is_string()) {
            return error_result("exec: 'command' (string) is required");
        }
        auto const command = arguments["command"].get<std::string>();
        auto const allowed = check_allowed(command, context.config.allow);
        if (!allowed) {
            return error_result("exec: " + allowed.error().message);
        }
        auto cwd = std::optional<std::string>{};
        if (arguments.contains("cwd") && arguments["cwd"].is_string()) {
            cwd = arguments["cwd"].get<std::string>();
        }
        auto timeout_ms = DEFAULT_TIMEOUT_MS;
        if (arguments.contains("timeout_ms") &&
            arguments["timeout_ms"].is_number_integer()) {
            timeout_ms = std::clamp(arguments["timeout_ms"].get<std::int64_t>(),
                                    std::int64_t{1}, MAX_TIMEOUT_MS);
        }
        if (arguments.contains("session") && arguments["session"].is_string()) {
            return invoke_session(context,
                                  arguments["session"].get<std::string>(),
                                  exec_command(command, cwd), timeout_ms);
        }
        auto const spawned = context.spawn(spawn_request_t{
            .argv = build_ssh_argv(context.config, exec_command(command, cwd)),
            .stdin_data = {},
            .timeout_ms = timeout_ms});
        if (!spawned) {
            return error_result("exec: " + spawned.error().message);
        }
        auto const out =
            truncate_middle(spawned->stdout_text, MAX_OUTPUT_CHARS);
        auto const err =
            truncate_middle(spawned->stderr_text, MAX_OUTPUT_CHARS);
        return {
            .text = dump_json(
                {{"stdout", out.text},
                 {"stderr", err.text},
                 {"exit_code", spawned->is_timed_out ? -1 : spawned->exit_code},
                 {"is_truncated", out.is_truncated || err.is_truncated},
                 {"is_timed_out", spawned->is_timed_out}}),
            .is_error = false};
    }
};

struct read_file_tool_t {
    static constexpr auto NAME = std::string_view{"read_file"};
    static constexpr auto DESCRIPTION =
        std::string_view{"Read a UTF-8 text file from the remote host "
                         "(max 1 MiB)."};

    static auto schema() -> nlohmann::json {
        return {{"type", "object"},
                {"properties",
                 {{"path",
                   {{"type", "string"},
                    {"description", "absolute or home-relative remote "
                                    "path"}}},
                  {"encoding",
                   {{"type", "string"},
                    {"description", "\"text\" (default) or \"base64\" "
                                    "for binary (max 8 MiB)"}}}}},
                {"required", nlohmann::json::array({"path"})}};
    }

    static auto invoke(context_t& context, nlohmann::json const& arguments)
        -> tool_result_t {
        if (!arguments.contains("path") || !arguments["path"].is_string()) {
            return error_result("read_file: 'path' (string) is required");
        }
        auto const is_base64 = parse_encoding(arguments);
        if (!is_base64) {
            return error_result("read_file: " + is_base64.error().message);
        }
        auto const path = arguments["path"].get<std::string>();
        auto const spawned = context.spawn(spawn_request_t{
            .argv = build_ssh_argv(context.config, read_command(path)),
            .stdin_data = {},
            .timeout_ms = DEFAULT_TIMEOUT_MS});
        if (!spawned) {
            return error_result("read_file: " + spawned.error().message);
        }
        if (spawned->is_timed_out) {
            return error_result("read_file: timed out");
        }
        if (spawned->exit_code != 0) {
            return error_result(
                "read_file failed: " +
                truncate_middle(spawned->stderr_text, 1000).text);
        }
        auto const cap = *is_base64 ? MAX_BINARY_BYTES : MAX_READ_BYTES;
        if (spawned->stdout_text.size() > cap) {
            return error_result(
                std::format("read_file: {} exceeds {} bytes ({} bytes)", path,
                            cap, spawned->stdout_text.size()));
        }
        return {.text = *is_base64 ? base64_encode(spawned->stdout_text)
                                   : spawned->stdout_text,
                .is_error = false};
    }
};

struct write_file_tool_t {
    static constexpr auto NAME = std::string_view{"write_file"};
    static constexpr auto DESCRIPTION =
        std::string_view{"Write exact bytes to a file on the remote host, "
                         "creating parent directories."};

    static auto schema() -> nlohmann::json {
        return {{"type", "object"},
                {"properties",
                 {{"path",
                   {{"type", "string"}, {"description", "remote file path"}}},
                  {"encoding",
                   {{"type", "string"},
                    {"description", "\"text\" (default) or \"base64\" "
                                    "content (max 8 MiB decoded)"}}},
                  {"content",
                   {{"type", "string"},
                    {"description", "full file content to write"}}}}},
                {"required", nlohmann::json::array({"path", "content"})}};
    }

    static auto invoke(context_t& context, nlohmann::json const& arguments)
        -> tool_result_t {
        if (!arguments.contains("path") || !arguments["path"].is_string()) {
            return error_result("write_file: 'path' (string) is required");
        }
        if (!arguments.contains("content") ||
            !arguments["content"].is_string()) {
            return error_result("write_file: 'content' (string) is required");
        }
        auto const path = arguments["path"].get<std::string>();
        auto const is_base64 = parse_encoding(arguments);
        if (!is_base64) {
            return error_result("write_file: " + is_base64.error().message);
        }
        auto content = arguments["content"].get<std::string>();
        if (*is_base64) {
            auto decoded = base64_decode(content);
            if (!decoded) {
                return error_result("write_file: " + decoded.error().message);
            }
            content = std::move(*decoded);
        }
        if (content.size() > MAX_BINARY_BYTES) {
            return error_result("write_file: content exceeds 8 MiB");
        }
        auto const size = content.size();
        auto const spawned = context.spawn(spawn_request_t{
            .argv = build_ssh_argv(context.config, write_command(path)),
            .stdin_data = std::move(content),
            .timeout_ms = DEFAULT_TIMEOUT_MS});
        if (!spawned) {
            return error_result("write_file: " + spawned.error().message);
        }
        if (spawned->is_timed_out) {
            return error_result("write_file: timed out");
        }
        if (spawned->exit_code != 0) {
            return error_result(
                "write_file failed: " +
                truncate_middle(spawned->stderr_text, 1000).text);
        }
        return {.text = std::format("wrote {} bytes to {}", size, path),
                .is_error = false};
    }
};

struct session_close_tool_t {
    static constexpr auto NAME = std::string_view{"session_close"};
    static constexpr auto DESCRIPTION =
        std::string_view{"Close a named persistent session opened via exec."};

    static auto schema() -> nlohmann::json {
        return {{"type", "object"},
                {"properties",
                 {{"session",
                   {{"type", "string"}, {"description", "session name"}}}}},
                {"required", nlohmann::json::array({"session"})}};
    }

    static auto invoke(context_t& context, nlohmann::json const& arguments)
        -> tool_result_t {
        if (!arguments.contains("session") ||
            !arguments["session"].is_string()) {
            return error_result(
                "session_close: 'session' (string) is required");
        }
        if (!context.session_close) {
            return error_result("session_close: sessions unavailable");
        }
        auto const name = arguments["session"].get<std::string>();
        if (!context.session_close(name)) {
            return error_result("no such session: " + name);
        }
        return {.text = "closed " + name, .is_error = false};
    }
};

} // namespace sshmcp
