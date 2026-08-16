#pragma once

#include <sshmcp/types.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sshmcp {

inline auto quote_posix(std::string_view text) -> std::string {
    auto out = std::string{"'"};
    for (auto const ch : text) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('\'');
    return out;
}

inline auto split_command(std::string_view text)
    -> std::vector<std::string> {
    auto tokens = std::vector<std::string>{};
    auto current = std::string{};
    auto in_quotes = false;
    auto has_token = false;
    for (auto const ch : text) {
        if (ch == '"') {
            in_quotes = !in_quotes;
            has_token = true;
            continue;
        }
        if (!in_quotes && (ch == ' ' || ch == '\t')) {
            if (has_token) {
                tokens.push_back(current);
                current.clear();
                has_token = false;
            }
            continue;
        }
        current.push_back(ch);
        has_token = true;
    }
    if (has_token) {
        tokens.push_back(current);
    }
    return tokens;
}

inline auto build_ssh_argv(config_t const& config,
                           std::string remote_command)
    -> std::vector<std::string> {
    auto argv = config.ssh_exe;
    argv.push_back("-T");
    argv.push_back("-o");
    argv.push_back("BatchMode=yes");
    argv.insert(argv.end(), config.ssh_args.begin(),
                config.ssh_args.end());
    argv.push_back(config.target);
    argv.push_back(std::move(remote_command));
    return argv;
}

inline auto exec_command(std::string_view command,
                         std::optional<std::string> const& cwd)
    -> std::string {
    if (cwd) {
        return "cd " + quote_posix(*cwd) + " && " +
               std::string{command};
    }
    return std::string{command};
}

inline auto read_command(std::string_view path) -> std::string {
    return "cat " + quote_posix(path);
}

inline auto parent_dir(std::string_view path)
    -> std::optional<std::string> {
    auto const pos = path.rfind('/');
    if (pos == std::string_view::npos || pos == 0) {
        return std::nullopt;
    }
    return std::string{path.substr(0, pos)};
}

inline auto write_command(std::string_view path) -> std::string {
    auto const dir = parent_dir(path);
    if (dir) {
        return "mkdir -p " + quote_posix(*dir) + " && cat > " +
               quote_posix(path);
    }
    return "cat > " + quote_posix(path);
}

}  // namespace sshmcp
