#pragma once

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
                                    "(default 60000, max 600000)"}}}}},
                {"required", nlohmann::json::array({"command"})}};
    }

    static auto invoke(context_t& context, nlohmann::json const& arguments)
        -> tool_result_t {
        if (!arguments.contains("command") ||
            !arguments["command"].is_string()) {
            return error_result("exec: 'command' (string) is required");
        }
        auto const command = arguments["command"].get<std::string>();
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
                                    "path"}}}}},
                {"required", nlohmann::json::array({"path"})}};
    }

    static auto invoke(context_t& context, nlohmann::json const& arguments)
        -> tool_result_t {
        if (!arguments.contains("path") || !arguments["path"].is_string()) {
            return error_result("read_file: 'path' (string) is required");
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
        if (spawned->stdout_text.size() > MAX_READ_BYTES) {
            return error_result(
                std::format("read_file: {} exceeds 1 MiB ({} bytes)", path,
                            spawned->stdout_text.size()));
        }
        return {.text = spawned->stdout_text, .is_error = false};
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
        auto content = arguments["content"].get<std::string>();
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

} // namespace sshmcp
