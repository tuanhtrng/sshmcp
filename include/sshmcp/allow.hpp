#pragma once

#include <sshmcp/types.hpp>

#include <algorithm>
#include <array>
#include <expected>
#include <format>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace sshmcp {

inline auto trim(std::string_view text) -> std::string_view {
    auto const first = text.find_first_not_of(" \t");
    if (first == std::string_view::npos) {
        return {};
    }
    auto const last = text.find_last_not_of(" \t");
    return text.substr(first, last - first + 1);
}

inline auto parse_allow(std::string_view text) -> std::vector<std::string> {
    auto allow = std::vector<std::string>{};
    while (!text.empty()) {
        auto const comma = text.find(',');
        auto const item = trim(text.substr(0, comma));
        if (!item.empty()) {
            allow.emplace_back(item);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        text.remove_prefix(comma + 1);
    }
    return allow;
}

inline auto split_words(std::string_view text) -> std::vector<std::string> {
    auto words = std::vector<std::string>{};
    while (true) {
        auto const start = text.find_first_not_of(" \t");
        if (start == std::string_view::npos) {
            break;
        }
        text.remove_prefix(start);
        auto const end = text.find_first_of(" \t");
        words.emplace_back(text.substr(0, end));
        if (end == std::string_view::npos) {
            break;
        }
        text.remove_prefix(end);
    }
    return words;
}

// Each entry is a comma-separated phrase; words within a phrase
// are the command head plus required argument tokens.
inline auto parse_deny(std::string_view text)
    -> std::vector<std::vector<std::string>> {
    auto deny = std::vector<std::vector<std::string>>{};
    while (!text.empty()) {
        auto const comma = text.find(',');
        auto words = split_words(text.substr(0, comma));
        if (!words.empty()) {
            deny.push_back(std::move(words));
        }
        if (comma == std::string_view::npos) {
            break;
        }
        text.remove_prefix(comma + 1);
    }
    return deny;
}

// Compound syntax defeats first-token matching, so both list
// checks reject it outright while their list is active.
inline auto check_compound(std::string_view command, std::string_view list_name)
    -> std::expected<void, error_t> {
    constexpr auto BANNED = std::array{
        std::string_view{";"},  std::string_view{"&&"}, std::string_view{"||"},
        std::string_view{"|"},  std::string_view{"&"},  std::string_view{"`"},
        std::string_view{"$("}, std::string_view{"\n"}};
    for (auto const& token : BANNED) {
        if (command.find(token) != std::string_view::npos) {
            return std::unexpected{error_t{std::format(
                "{} active: compound syntax '{}' not permitted", list_name,
                token == "\n" ? std::string_view{"<newline>"} : token)}};
        }
    }
    return {};
}

inline auto check_denied(std::string_view command,
                         std::vector<std::vector<std::string>> const& deny)
    -> std::expected<void, error_t> {
    if (deny.empty()) {
        return {};
    }
    auto const compound = check_compound(command, "denylist");
    if (!compound) {
        return compound;
    }
    auto const words = split_words(command);
    for (auto const& entry : deny) {
        if (words.empty() || words[0] != entry[0]) {
            continue;
        }
        auto const args = std::span{words}.subspan(1);
        auto const is_match = std::ranges::all_of(
            entry | std::views::drop(1), [&](auto const& w) {
                return std::ranges::find(args, w) != args.end();
            });
        if (is_match) {
            auto joined = entry[0];
            for (auto const& word : entry | std::views::drop(1)) {
                joined += ' ';
                joined += word;
            }
            return std::unexpected{error_t{
                std::format("denylist active: '{}' is blocked", joined)}};
        }
    }
    return {};
}

// Guardrail against accidents, not a security boundary: the AI
// operator is trusted (spec v0.1 section 10).
inline auto check_allowed(std::string_view command,
                          std::vector<std::string> const& allow)
    -> std::expected<void, error_t> {
    if (allow.empty()) {
        return {};
    }
    auto const compound = check_compound(command, "allowlist");
    if (!compound) {
        return compound;
    }
    auto const trimmed = trim(command);
    auto const space = trimmed.find_first_of(" \t");
    auto const head = trimmed.substr(
        0, space == std::string_view::npos ? trimmed.size() : space);
    for (auto const& item : allow) {
        if (head == item) {
            return {};
        }
    }
    return std::unexpected{error_t{
        std::format("allowlist active: '{}' is not an allowed command", head)}};
}

} // namespace sshmcp
