#include "harness.hpp"

#include <sshmcp/jsonrpc.hpp>

#include <nlohmann/json.hpp>

namespace {

namespace t = sshmcp::test;

[[maybe_unused]] auto const t1 = t::add_test("parse request", [] {
    auto const request = sshmcp::parse_request(
        R"({"jsonrpc":"2.0","id":7,"method":"ping"})");
    t::expect(request.has_value(), "parses");
    t::expect(request->method == "ping", "method");
    t::expect(request->id == 7, "id");
    t::expect(!request->is_notification, "not notification");
    t::expect(request->params.is_object(),
              "params normalized to object");
});

[[maybe_unused]] auto const t2 = t::add_test("notification", [] {
    auto const request = sshmcp::parse_request(
        R"({"jsonrpc":"2.0","method":"notifications/x"})");
    t::expect(request.has_value(), "parses");
    t::expect(request->is_notification, "flagged");
});

[[maybe_unused]] auto const t3 = t::add_test("bad input", [] {
    t::expect(!sshmcp::parse_request("not json").has_value(),
              "invalid json");
    t::expect(!sshmcp::parse_request(R"({"id":1})").has_value(),
              "missing method");
    t::expect(!sshmcp::parse_request(
                   R"({"jsonrpc":"1.0","id":1,"method":"m"})")
                   .has_value(),
              "wrong version");
});

[[maybe_unused]] auto const t4 = t::add_test("serialize", [] {
    auto const result = nlohmann::json::parse(
        sshmcp::make_result(3, {{"ok", true}}));
    t::expect(result["jsonrpc"] == "2.0", "result envelope");
    t::expect(result["id"] == 3, "result id");
    t::expect(result["result"]["ok"] == true, "result body");

    auto const error = nlohmann::json::parse(sshmcp::make_error(
        nlohmann::json{}, sshmcp::PARSE_ERROR, "bad"));
    t::expect(error["error"]["code"] == sshmcp::PARSE_ERROR,
              "error code");
    t::expect(error["error"]["message"] == "bad",
              "error message");
    t::expect(error["id"].is_null(), "null id");
});

}  // namespace
