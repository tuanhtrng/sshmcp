#include "harness.hpp"

#include <sshmcp/registry.hpp>
#include <sshmcp/tools.hpp>

#include <nlohmann/json.hpp>

#include <expected>

namespace {

namespace t = sshmcp::test;

static_assert(sshmcp::tool<sshmcp::exec_tool_t>);
static_assert(sshmcp::tool<sshmcp::read_file_tool_t>);
static_assert(sshmcp::tool<sshmcp::write_file_tool_t>);

using tools_t =
    sshmcp::tool_set_t<sshmcp::exec_tool_t, sshmcp::read_file_tool_t,
                       sshmcp::write_file_tool_t>;

[[maybe_unused]] auto const t1 = t::add_test("list", [] {
    auto const tools = tools_t::list();
    t::expect(tools.size() == 3, "three tools");
    t::expect(tools[0]["name"] == "exec", "exec first");
    t::expect(tools[1]["name"] == "read_file", "read");
    t::expect(tools[2]["name"] == "write_file", "write");
    t::expect(tools[0].contains("inputSchema"), "schema");
    t::expect(tools[0].contains("description"), "description");
});

[[maybe_unused]] auto const t2 = t::add_test("dispatch", [] {
    auto context = sshmcp::context_t{
        .config = {},
        .spawn = [](sshmcp::spawn_request_t const&)
            -> std::expected<sshmcp::spawn_result_t, sshmcp::error_t> {
            return sshmcp::spawn_result_t{.stdout_text = "ok\n",
                                          .exit_code = 0};
        }};
    auto const known = tools_t::call(context, "exec", {{"command", "true"}});
    t::expect(known.has_value(), "known dispatches");
    t::expect(!known->is_error, "invoked");

    auto const unknown = tools_t::call(context, "nope", {});
    t::expect(!unknown.has_value(), "unknown is nullopt");
});

} // namespace
