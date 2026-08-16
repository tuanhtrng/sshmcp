#pragma once

#include <sshmcp/types.hpp>

#include <array>
#include <expected>
#include <string>
#include <string_view>

namespace sshmcp {

inline constexpr auto BASE64_ALPHABET = std::string_view{
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz0123456789+/"};

inline auto base64_encode(std::string_view data) -> std::string {
    auto out = std::string{};
    out.reserve((data.size() + 2) / 3 * 4);
    auto index = std::size_t{0};
    while (index + 3 <= data.size()) {
        auto const a = static_cast<unsigned char>(data[index]);
        auto const b =
            static_cast<unsigned char>(data[index + 1]);
        auto const c =
            static_cast<unsigned char>(data[index + 2]);
        out.push_back(BASE64_ALPHABET[a >> 2]);
        out.push_back(
            BASE64_ALPHABET[((a & 0x03) << 4) | (b >> 4)]);
        out.push_back(
            BASE64_ALPHABET[((b & 0x0f) << 2) | (c >> 6)]);
        out.push_back(BASE64_ALPHABET[c & 0x3f]);
        index += 3;
    }
    auto const rest = data.size() - index;
    if (rest == 1) {
        auto const a = static_cast<unsigned char>(data[index]);
        out.push_back(BASE64_ALPHABET[a >> 2]);
        out.push_back(BASE64_ALPHABET[(a & 0x03) << 4]);
        out += "==";
    } else if (rest == 2) {
        auto const a = static_cast<unsigned char>(data[index]);
        auto const b =
            static_cast<unsigned char>(data[index + 1]);
        out.push_back(BASE64_ALPHABET[a >> 2]);
        out.push_back(
            BASE64_ALPHABET[((a & 0x03) << 4) | (b >> 4)]);
        out.push_back(BASE64_ALPHABET[(b & 0x0f) << 2]);
        out.push_back('=');
    }
    return out;
}

inline auto base64_value(char ch) -> int {
    auto const pos = BASE64_ALPHABET.find(ch);
    return pos == std::string_view::npos ? -1
                                         : static_cast<int>(pos);
}

inline auto base64_decode(std::string_view text)
    -> std::expected<std::string, error_t> {
    if (text.size() % 4 != 0) {
        return std::unexpected{
            error_t{"base64: length not multiple of 4"}};
    }
    auto out = std::string{};
    out.reserve(text.size() / 4 * 3);
    for (auto index = std::size_t{0}; index < text.size();
         index += 4) {
        auto const quad = text.substr(index, 4);
        auto const is_last = index + 4 == text.size();
        auto values = std::array<int, 4>{};
        auto pads = 0;
        for (auto slot = std::size_t{0}; slot < 4; ++slot) {
            auto const ch = quad[slot];
            if (ch == '=') {
                if (!is_last || slot < 2 ||
                    (slot == 2 && quad[3] != '=')) {
                    return std::unexpected{
                        error_t{"base64: bad padding"}};
                }
                ++pads;
                values[slot] = 0;
            } else {
                if (pads > 0) {
                    return std::unexpected{
                        error_t{"base64: data after padding"}};
                }
                values[slot] = base64_value(ch);
                if (values[slot] < 0) {
                    return std::unexpected{
                        error_t{"base64: invalid character"}};
                }
            }
        }
        out.push_back(static_cast<char>((values[0] << 2) |
                                        (values[1] >> 4)));
        if (pads < 2) {
            out.push_back(static_cast<char>(
                ((values[1] & 0x0f) << 4) | (values[2] >> 2)));
        }
        if (pads < 1) {
            out.push_back(static_cast<char>(
                ((values[2] & 0x03) << 6) | values[3]));
        }
    }
    return out;
}

}  // namespace sshmcp
