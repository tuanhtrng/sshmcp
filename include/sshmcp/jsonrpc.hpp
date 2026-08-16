#pragma once

#include <sshmcp/types.hpp>

#include <nlohmann/json.hpp>

#include <expected>
#include <string>
#include <string_view>

namespace sshmcp {

inline constexpr auto PARSE_ERROR = -32700;
inline constexpr auto INVALID_REQUEST = -32600;
inline constexpr auto METHOD_NOT_FOUND = -32601;
inline constexpr auto INVALID_PARAMS = -32602;

struct request_t {
    nlohmann::json id;
    std::string method;
    nlohmann::json params;
    bool is_notification{};
};

inline auto dump_json(nlohmann::json const& value) -> std::string {
    return value.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

inline auto parse_request(std::string_view line)
    -> std::expected<request_t, error_t> {
    auto const parsed = nlohmann::json::parse(line, nullptr, false);
    if (parsed.is_discarded()) {
        return std::unexpected{error_t{"invalid JSON"}};
    }
    if (!parsed.is_object() || parsed.value("jsonrpc", "") != "2.0" ||
        !parsed.contains("method") || !parsed["method"].is_string()) {
        return std::unexpected{error_t{"invalid JSON-RPC request"}};
    }
    auto request = request_t{};
    request.method = parsed["method"].get<std::string>();
    request.is_notification = !parsed.contains("id");
    if (!request.is_notification) {
        request.id = parsed["id"];
    }
    request.params = parsed.value("params", nlohmann::json::object());
    if (!request.params.is_object()) {
        request.params = nlohmann::json::object();
    }
    return request;
}

inline auto make_result(nlohmann::json const& id, nlohmann::json const& result)
    -> std::string {
    return dump_json({{"jsonrpc", "2.0"}, {"id", id}, {"result", result}});
}

inline auto make_error(nlohmann::json const& id, int code,
                       std::string_view message) -> std::string {
    return dump_json(
        {{"jsonrpc", "2.0"},
         {"id", id},
         {"error", {{"code", code}, {"message", std::string{message}}}}});
}

} // namespace sshmcp
