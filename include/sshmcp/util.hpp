#pragma once

#include <sshmcp/types.hpp>

#include <format>
#include <string>
#include <string_view>
#include <utility>

namespace sshmcp {

struct truncated_t {
    std::string text;
    bool is_truncated{};
};

inline auto truncate_middle(std::string_view text, std::size_t max_chars)
    -> truncated_t {
    if (text.size() <= max_chars) {
        return {std::string{text}, false};
    }
    auto const half = max_chars / 2;
    auto const dropped = text.size() - 2 * half;
    auto out = std::string{text.substr(0, half)};
    out += std::format("\n...[sshmcp: {} chars truncated]...\n", dropped);
    out += text.substr(text.size() - half);
    return {std::move(out), true};
}

} // namespace sshmcp
