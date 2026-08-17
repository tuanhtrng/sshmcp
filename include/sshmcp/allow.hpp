#pragma once

#include <sshmcp/types.hpp>

#include <array>
#include <expected>
#include <format>
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

// Guardrail against accidents, not a security boundary: the AI
// operator is trusted (spec v0.1 section 10).
inline auto check_allowed(std::string_view command,
                          std::vector<std::string> const& allow)
    -> std::expected<void, error_t> {
    if (allow.empty()) {
        return {};
    }
    constexpr auto BANNED = std::array{
        std::string_view{";"},  std::string_view{"&&"}, std::string_view{"||"},
        std::string_view{"|"},  std::string_view{"&"},  std::string_view{"`"},
        std::string_view{"$("}, std::string_view{"\n"}};
    for (auto const& token : BANNED) {
        if (command.find(token) != std::string_view::npos) {
            return std::unexpected{error_t{std::format(
                "allowlist active: compound syntax '{}' not "
                "permitted",
                token == "\n" ? std::string_view{"<newline>"} : token)}};
        }
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
