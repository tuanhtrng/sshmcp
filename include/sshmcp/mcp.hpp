#pragma once

#include <sshmcp/jsonrpc.hpp>
#include <sshmcp/registry.hpp>
#include <sshmcp/types.hpp>
#include <sshmcp/version.hpp>

#include <nlohmann/json.hpp>

#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace sshmcp {

inline constexpr auto PROTOCOL_VERSION = std::string_view{"2024-11-05"};

template <tool... ToolTypes>
class server_t {
  public:
    explicit server_t(context_t context) : context{std::move(context)} {}

    auto handle_line(std::string_view line) -> std::optional<std::string> {
        auto request = parse_request(line);
        if (!request) {
            return make_error(nlohmann::json{}, PARSE_ERROR,
                              request.error().message);
        }
        if (request->is_notification) {
            return std::nullopt;
        }
        if (request->method == "initialize") {
            return handle_initialize(*request);
        }
        if (request->method == "ping") {
            return make_result(request->id, nlohmann::json::object());
        }
        if (request->method == "tools/list") {
            return make_result(request->id,
                               {{"tools", tool_set_t<ToolTypes...>::list()}});
        }
        if (request->method == "tools/call") {
            return handle_call(*request);
        }
        return make_error(request->id, METHOD_NOT_FOUND,
                          "method not found: " + request->method);
    }

    auto run(std::istream& in, std::ostream& out) -> int {
        auto line = std::string{};
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }
            if (auto const reply = handle_line(line)) {
                out << *reply << '\n' << std::flush;
            }
        }
        return 0;
    }

  private:
    auto handle_initialize(request_t const& request) -> std::string {
        auto protocol = std::string{PROTOCOL_VERSION};
        if (request.params.contains("protocolVersion") &&
            request.params["protocolVersion"].is_string()) {
            protocol = request.params["protocolVersion"].get<std::string>();
        }
        return make_result(
            request.id,
            {{"protocolVersion", protocol},
             {"capabilities", {{"tools", nlohmann::json::object()}}},
             {"serverInfo", {{"name", "sshmcp"}, {"version", VERSION}}}});
    }

    auto handle_call(request_t const& request) -> std::string {
        if (!request.params.contains("name") ||
            !request.params["name"].is_string()) {
            return make_error(request.id, INVALID_PARAMS,
                              "tools/call: 'name' required");
        }
        auto const name = request.params["name"].get<std::string>();
        auto arguments =
            request.params.value("arguments", nlohmann::json::object());
        if (!arguments.is_object()) {
            arguments = nlohmann::json::object();
        }
        auto const result =
            tool_set_t<ToolTypes...>::call(context, name, arguments);
        if (!result) {
            return make_error(request.id, INVALID_PARAMS,
                              "unknown tool: " + name);
        }
        return make_result(
            request.id,
            {{"content", nlohmann::json::array(
                             {{{"type", "text"}, {"text", result->text}}})},
             {"isError", result->is_error}});
    }

    context_t context;
};

} // namespace sshmcp
