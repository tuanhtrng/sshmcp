#pragma once

#include <sshmcp/allow.hpp>
#include <sshmcp/ssh.hpp>
#include <sshmcp/types.hpp>

#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace sshmcp {

using get_env_fn_t = std::function<char const*(char const*)>;

inline auto parse_log_level(std::string_view text) -> log_level_t {
    if (text == "off") {
        return log_level_t::OFF;
    }
    if (text == "debug") {
        return log_level_t::DEBUG;
    }
    return log_level_t::INFO;
}

inline auto load_config(get_env_fn_t const& get_env,
                        std::span<std::string const> args)
    -> std::expected<config_t, error_t> {
    auto config = config_t{};
    auto const* target = get_env("SSHMCP_TARGET");
    if (target != nullptr && *target != '\0') {
        config.target = target;
    } else if (!args.empty()) {
        config.target = args[0];
    } else {
        return std::unexpected{
            error_t{"no target: set SSHMCP_TARGET or pass user@host "
                    "as the first argument"}};
    }
    auto const* ssh_exe = get_env("SSHMCP_SSH_EXE");
    if (ssh_exe != nullptr && *ssh_exe != '\0') {
        config.ssh_exe = split_command(ssh_exe);
        if (config.ssh_exe.empty()) {
            return std::unexpected{error_t{"SSHMCP_SSH_EXE is blank"}};
        }
    }
    auto const* ssh_args = get_env("SSHMCP_SSH_ARGS");
    if (ssh_args != nullptr) {
        config.ssh_args = split_command(ssh_args);
    }
    auto const* allow = get_env("SSHMCP_ALLOW");
    if (allow != nullptr) {
        config.allow = parse_allow(allow);
    }
    auto const* log_level = get_env("SSHMCP_LOG");
    if (log_level != nullptr) {
        config.log_level = parse_log_level(log_level);
    }
    return config;
}

} // namespace sshmcp
