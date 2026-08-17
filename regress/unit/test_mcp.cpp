#include "harness.hpp"

#include <sshmcp/mcp.hpp>
#include <sshmcp/tools.hpp>
#include <sshmcp/version.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <expected>
#include <sstream>
#include <string>

namespace {

namespace t = sshmcp::test;

using server_t =
    sshmcp::server_t<sshmcp::exec_tool_t, sshmcp::read_file_tool_t,
                     sshmcp::write_file_tool_t, sshmcp::session_close_tool_t,
                     sshmcp::forward_open_tool_t, sshmcp::forward_close_tool_t,
                     sshmcp::upload_file_tool_t, sshmcp::download_file_tool_t>;

auto test_server() -> server_t {
    return server_t{sshmcp::context_t{
        .config = {},
        .spawn = [](sshmcp::spawn_request_t const&)
            -> std::expected<sshmcp::spawn_result_t, sshmcp::error_t> {
            return sshmcp::spawn_result_t{.stdout_text = "out\n",
                                          .exit_code = 0};
        }}};
}

[[maybe_unused]] auto const t1 = t::add_test("initialize", [] {
    auto server = test_server();
    auto const reply =
        server.handle_line(R"({"jsonrpc":"2.0","id":0,"method":"initialize",)"
                           R"("params":{"protocolVersion":"2025-06-18"}})");
    t::expect(reply.has_value(), "replies");
    auto const body = nlohmann::json::parse(*reply);
    t::expect(body["result"]["serverInfo"]["name"] == "sshmcp", "server name");
    t::expect(body["result"]["serverInfo"]["version"] ==
                  std::string{sshmcp::VERSION},
              "version from header");
    t::expect(body["result"]["protocolVersion"] == "2025-06-18",
              "echoes client protocol");
    t::expect(body["result"]["capabilities"].contains("tools"),
              "tools capability");
});

[[maybe_unused]] auto const t2 = t::add_test("lifecycle", [] {
    auto server = test_server();
    t::expect(!server
                   .handle_line(R"({"jsonrpc":"2.0","method":)"
                                R"("notifications/initialized"})")
                   .has_value(),
              "notification silent");
    auto const pong =
        server.handle_line(R"({"jsonrpc":"2.0","id":9,"method":"ping"})");
    t::expect(nlohmann::json::parse(*pong)["result"].is_object(),
              "ping empty object");
});

[[maybe_unused]] auto const t3 = t::add_test("tools flow", [] {
    auto server = test_server();
    auto const list =
        server.handle_line(R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
    auto const tools = nlohmann::json::parse(*list)["result"]["tools"];
    t::expect(tools.size() == 8, "eight tools listed");

    auto const call =
        server.handle_line(R"({"jsonrpc":"2.0","id":2,"method":"tools/call",)"
                           R"("params":{"name":"exec",)"
                           R"("arguments":{"command":"ls"}}})");
    auto const body = nlohmann::json::parse(*call);
    t::expect(body["result"]["isError"] == false, "call ok");
    t::expect(body["result"]["content"][0]["type"] == "text", "text content");

    auto const unknown =
        server.handle_line(R"({"jsonrpc":"2.0","id":3,"method":"tools/call",)"
                           R"("params":{"name":"nope","arguments":{}}})");
    t::expect(nlohmann::json::parse(*unknown)["error"]["code"] ==
                  sshmcp::INVALID_PARAMS,
              "unknown tool errors");
});

[[maybe_unused]] auto const t4 = t::add_test("errors", [] {
    auto server = test_server();
    auto const bad = server.handle_line("garbage");
    t::expect(nlohmann::json::parse(*bad)["error"]["code"] ==
                  sshmcp::PARSE_ERROR,
              "parse error");
    auto const missing =
        server.handle_line(R"({"jsonrpc":"2.0","id":4,"method":"wat"})");
    t::expect(nlohmann::json::parse(*missing)["error"]["code"] ==
                  sshmcp::METHOD_NOT_FOUND,
              "method not found");
});

[[maybe_unused]] auto const t5 = t::add_test("run loop", [] {
    auto server = test_server();
    auto input = std::istringstream{"{\"jsonrpc\":\"2.0\",\"id\":0,"
                                    "\"method\":\"ping\"}\r\n"
                                    "\n"
                                    "{\"jsonrpc\":\"2.0\",\"id\":1,"
                                    "\"method\":\"ping\"}\n"};
    auto output = std::ostringstream{};
    auto const code = server.run(input, output);
    t::expect(code == 0, "clean exit");
    auto const text = output.str();
    t::expect(std::count(text.begin(), text.end(), '\n') == 2,
              "two reply lines");
});

} // namespace
