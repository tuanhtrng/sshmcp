#include "harness.hpp"

#include <sshmcp/tools.hpp>

#include <nlohmann/json.hpp>

#include <expected>
#include <string>
#include <utility>

namespace {

namespace t = sshmcp::test;

auto context_with(sshmcp::spawn_result_t reply,
                  sshmcp::spawn_request_t& captured) -> sshmcp::context_t {
    auto config = sshmcp::config_t{};
    config.target = "dev@host";
    return sshmcp::context_t{
        .config = config,
        .spawn = [reply = std::move(reply),
                  &captured](sshmcp::spawn_request_t const& request)
            -> std::expected<sshmcp::spawn_result_t, sshmcp::error_t> {
            captured = request;
            return reply;
        }};
}

[[maybe_unused]] auto const t1 = t::add_test("exec ok", [] {
    auto captured = sshmcp::spawn_request_t{};
    auto context =
        context_with({.stdout_text = "hi\n", .exit_code = 0}, captured);
    auto const result =
        sshmcp::exec_tool_t::invoke(context, {{"command", "echo hi"}});
    t::expect(!result.is_error, "not error");
    t::expect(captured.argv.back() == "echo hi", "command");
    t::expect(captured.argv.front() == "ssh", "exe");
    auto const body = nlohmann::json::parse(result.text);
    t::expect(body["stdout"] == "hi\n", "stdout");
    t::expect(body["exit_code"] == 0, "exit code");
    t::expect(body["is_timed_out"] == false, "no timeout");
});

[[maybe_unused]] auto const t2 = t::add_test("exec args", [] {
    auto captured = sshmcp::spawn_request_t{};
    auto context = context_with({.exit_code = 1}, captured);
    auto const missing = sshmcp::exec_tool_t::invoke(context, {});
    t::expect(missing.is_error, "missing command errors");

    auto const clamped = sshmcp::exec_tool_t::invoke(
        context, {{"command", "x"}, {"timeout_ms", 999999999}});
    t::expect(captured.timeout_ms == sshmcp::MAX_TIMEOUT_MS, "timeout clamped");
    t::expect(!clamped.is_error, "nonzero exit not error");

    sshmcp::exec_tool_t::invoke(context,
                                {{"command", "make"}, {"cwd", "/src"}});
    t::expect(captured.argv.back() == "cd '/src' && make", "cwd prepended");
});

[[maybe_unused]] auto const t3 = t::add_test("exec timeout", [] {
    auto captured = sshmcp::spawn_request_t{};
    auto context =
        context_with({.exit_code = 9, .is_timed_out = true}, captured);
    auto const result =
        sshmcp::exec_tool_t::invoke(context, {{"command", "sleep 99"}});
    auto const body = nlohmann::json::parse(result.text);
    t::expect(body["is_timed_out"] == true, "flag");
    t::expect(body["exit_code"] == -1, "timeout exit normalized");
});

[[maybe_unused]] auto const t4 = t::add_test("read_file", [] {
    auto captured = sshmcp::spawn_request_t{};
    auto context =
        context_with({.stdout_text = "content", .exit_code = 0}, captured);
    auto const result =
        sshmcp::read_file_tool_t::invoke(context, {{"path", "/a b"}});
    t::expect(!result.is_error, "ok");
    t::expect(result.text == "content", "raw text");
    t::expect(captured.argv.back() == "cat '/a b'", "quoted");

    auto failing = context_with(
        {.stderr_text = "cat: no such file", .exit_code = 1}, captured);
    auto const missing =
        sshmcp::read_file_tool_t::invoke(failing, {{"path", "/nope"}});
    t::expect(missing.is_error, "nonzero exit errors");
    t::expect(missing.text.contains("no such file"), "stderr surfaced");

    auto big = context_with(
        {.stdout_text = std::string(sshmcp::MAX_READ_BYTES + 1, 'x'),
         .exit_code = 0},
        captured);
    auto const too_big =
        sshmcp::read_file_tool_t::invoke(big, {{"path", "/big"}});
    t::expect(too_big.is_error, "size cap");
});

[[maybe_unused]] auto const t5 = t::add_test("write_file", [] {
    auto captured = sshmcp::spawn_request_t{};
    auto context = context_with({.exit_code = 0}, captured);
    auto const result = sshmcp::write_file_tool_t::invoke(
        context, {{"path", "/d/f.txt"}, {"content", "hi\n"}});
    t::expect(!result.is_error, "ok");
    t::expect(captured.stdin_data == "hi\n", "content piped");
    t::expect(captured.argv.back() == "mkdir -p '/d' && cat > '/d/f.txt'",
              "remote command");
    t::expect(result.text.contains("3 bytes"), "byte count reported");

    auto const no_content =
        sshmcp::write_file_tool_t::invoke(context, {{"path", "/x"}});
    t::expect(no_content.is_error, "content required");
});

} // namespace
