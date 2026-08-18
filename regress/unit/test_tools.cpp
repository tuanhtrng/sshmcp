#include "harness.hpp"

#include <sshmcp/base64.hpp>
#include <sshmcp/tools.hpp>

#include <nlohmann/json.hpp>

#include <cstdint>
#include <expected>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
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

[[maybe_unused]] auto const t6 = t::add_test("exec session", [] {
    auto seen_name = std::string{};
    auto seen_command = std::string{};
    auto context = sshmcp::context_t{
        .config = {},
        .spawn = {},
        .session_exec = [&](std::string_view name, std::string_view command,
                            std::int64_t)
            -> std::expected<sshmcp::session_result_t, sshmcp::error_t> {
            seen_name = name;
            seen_command = command;
            return sshmcp::session_result_t{
                .result = {.stdout_text = "in session\n", .exit_code = 0},
                .is_session_dead = false};
        },
        .session_close = {}};
    auto const result = sshmcp::exec_tool_t::invoke(
        context, {{"command", "make"}, {"cwd", "/src"}, {"session", "dev"}});
    t::expect(!result.is_error, "ok");
    t::expect(seen_name == "dev", "session name");
    t::expect(seen_command == "cd '/src' && make", "cwd folded into command");
    auto const body = nlohmann::json::parse(result.text);
    t::expect(body["session"] == "dev", "session echoed");
    t::expect(body["is_session_dead"] == false, "alive");
    t::expect(body["stdout"] == "in session\n", "stdout");

    auto missing = sshmcp::context_t{.config = {}, .spawn = {}};
    auto const no_backend = sshmcp::exec_tool_t::invoke(
        missing, {{"command", "x"}, {"session", "s"}});
    t::expect(no_backend.is_error, "no session backend");
});

[[maybe_unused]] auto const t7 = t::add_test("session dead", [] {
    auto context = sshmcp::context_t{
        .config = {},
        .spawn = {},
        .session_exec = [](std::string_view, std::string_view, std::int64_t)
            -> std::expected<sshmcp::session_result_t, sshmcp::error_t> {
            return sshmcp::session_result_t{.result = {.stdout_text = "partial",
                                                       .exit_code = -1,
                                                       .is_timed_out = true},
                                            .is_session_dead = true};
        },
        .session_close = {}};
    auto const result = sshmcp::exec_tool_t::invoke(
        context, {{"command", "sleep 99"}, {"session", "dev"}});
    auto const body = nlohmann::json::parse(result.text);
    t::expect(body["is_session_dead"] == true, "dead");
    t::expect(body["is_timed_out"] == true, "timed out");
    t::expect(body["exit_code"] == -1, "exit -1");
});

[[maybe_unused]] auto const t8 = t::add_test("close tool", [] {
    auto context =
        sshmcp::context_t{.config = {},
                          .spawn = {},
                          .session_exec = {},
                          .session_close = [](std::string_view name) -> bool {
                              return name == "known";
                          }};
    auto const hit =
        sshmcp::session_close_tool_t::invoke(context, {{"session", "known"}});
    t::expect(!hit.is_error, "close ok");
    t::expect(hit.text == "closed known", "message");
    auto const miss =
        sshmcp::session_close_tool_t::invoke(context, {{"session", "ghost"}});
    t::expect(miss.is_error, "unknown errors");
    t::expect(miss.text == "no such session: ghost", "unknown message");
});

[[maybe_unused]] auto const t9 = t::add_test("binary read", [] {
    auto captured = sshmcp::spawn_request_t{};
    auto const raw = std::string{"\x00\xff\x10hi", 5};
    auto context = context_with({.stdout_text = raw, .exit_code = 0}, captured);
    auto const result = sshmcp::read_file_tool_t::invoke(
        context, {{"path", "/bin/x"}, {"encoding", "base64"}});
    t::expect(!result.is_error, "ok");
    t::expect(sshmcp::base64_decode(result.text).value() == raw,
              "encoded round trip");

    auto big = context_with(
        {.stdout_text = std::string(sshmcp::MAX_BINARY_BYTES + 1, 'x'),
         .exit_code = 0},
        captured);
    t::expect(sshmcp::read_file_tool_t::invoke(
                  big, {{"path", "/big"}, {"encoding", "base64"}})
                  .is_error,
              "8 MiB cap");

    auto text_ok = context_with(
        {.stdout_text = std::string(sshmcp::MAX_READ_BYTES + 1, 'x'),
         .exit_code = 0},
        captured);
    t::expect(!sshmcp::read_file_tool_t::invoke(
                   text_ok, {{"path", "/t"}, {"encoding", "base64"}})
                   .is_error,
              "base64 mode allows >1MiB");

    auto bad = context_with({.exit_code = 0}, captured);
    t::expect(sshmcp::read_file_tool_t::invoke(
                  bad, {{"path", "/x"}, {"encoding", "nope"}})
                  .is_error,
              "bad encoding rejected");
});

[[maybe_unused]] auto const t10 = t::add_test("binary write", [] {
    auto captured = sshmcp::spawn_request_t{};
    auto context = context_with({.exit_code = 0}, captured);
    auto const raw = std::string{"\x01\x02\x00zz", 5};
    auto const result = sshmcp::write_file_tool_t::invoke(
        context, {{"path", "/bin/x"},
                  {"content", sshmcp::base64_encode(raw)},
                  {"encoding", "base64"}});
    t::expect(!result.is_error, "ok");
    t::expect(captured.stdin_data == raw, "raw bytes piped");
    t::expect(result.text.contains("5 bytes"), "decoded size reported");

    auto const bad = sshmcp::write_file_tool_t::invoke(
        context, {{"path", "/x"}, {"content", "!!!!"}, {"encoding", "base64"}});
    t::expect(bad.is_error, "invalid base64 rejected");
    t::expect(bad.text.contains("base64"), "clear error");
});

[[maybe_unused]] auto const t12 = t::add_test("forward tools", [] {
    auto opened_args = std::string{};
    auto closed_port = 0;
    auto context = sshmcp::context_t{};
    context.forward_open =
        [&](int local_port, std::string_view remote_host,
            int remote_port) -> std::expected<std::string, sshmcp::error_t> {
        opened_args =
            std::format("{}:{}:{}", local_port, remote_host, remote_port);
        return std::string{"forwarding ok"};
    };
    context.forward_close = [&](int local_port) -> bool {
        closed_port = local_port;
        return local_port == 18080;
    };

    auto const opened = sshmcp::forward_open_tool_t::invoke(
        context, {{"local_port", 18080}, {"remote_port", 3000}});
    t::expect(!opened.is_error, "open ok");
    t::expect(opened.text == "forwarding ok", "text passthrough");
    t::expect(opened_args == "18080:localhost:3000", "default remote_host");

    sshmcp::forward_open_tool_t::invoke(context, {{"local_port", 18081},
                                                  {"remote_port", 80},
                                                  {"remote_host", "10.0.0.5"}});
    t::expect(opened_args == "18081:10.0.0.5:80", "explicit remote_host");

    for (auto const bad_port : {0, 70000}) {
        auto const bad = sshmcp::forward_open_tool_t::invoke(
            context, {{"local_port", bad_port}, {"remote_port", 80}});
        t::expect(bad.is_error, "port range enforced");
    }
    auto const not_int = sshmcp::forward_open_tool_t::invoke(
        context, {{"local_port", "80"}, {"remote_port", 80}});
    t::expect(not_int.is_error, "string port rejected");

    auto const closed =
        sshmcp::forward_close_tool_t::invoke(context, {{"local_port", 18080}});
    t::expect(!closed.is_error, "close ok");
    t::expect(closed.text == "closed forward 18080", "close text");
    t::expect(closed_port == 18080, "close routed");
    auto const missing =
        sshmcp::forward_close_tool_t::invoke(context, {{"local_port", 18099}});
    t::expect(missing.is_error, "unknown forward errors");

    auto bare = sshmcp::context_t{};
    t::expect(sshmcp::forward_open_tool_t::invoke(
                  bare, {{"local_port", 1}, {"remote_port", 2}})
                  .is_error,
              "no backend errors");
});

auto temp_dir() -> std::filesystem::path {
    auto const base =
        std::filesystem::temp_directory_path() / "sshmcp_transfer_test";
    std::filesystem::create_directories(base);
    return base;
}

[[maybe_unused]] auto const t13 = t::add_test("upload_file", [] {
    auto const dir = temp_dir();
    auto const src = dir / "in.bin";
    auto const raw = std::string{"\x01\x00\xffup", 5};
    {
        auto out = std::ofstream{src, std::ios::binary};
        out.write(raw.data(), static_cast<std::streamsize>(raw.size()));
    }
    auto captured = sshmcp::spawn_request_t{};
    auto context = context_with({.exit_code = 0}, captured);
    auto const result = sshmcp::upload_file_tool_t::invoke(
        context,
        {{"local_path", src.string()}, {"remote_path", "/srv/a b/out.bin"}});
    t::expect(!result.is_error, "ok");
    t::expect(captured.stdin_data == raw, "bytes piped");
    t::expect(captured.argv.back() ==
                  "mkdir -p '/srv/a b' && cat > '/srv/a b/out.bin'",
              "remote command");
    t::expect(result.text.contains("5 bytes"), "size reported");

    auto const missing = sshmcp::upload_file_tool_t::invoke(
        context,
        {{"local_path", (dir / "nope.bin").string()}, {"remote_path", "/x"}});
    t::expect(missing.is_error, "missing local errors");

    auto failing =
        context_with({.stderr_text = "disk full", .exit_code = 1}, captured);
    auto const remote_fail = sshmcp::upload_file_tool_t::invoke(
        failing, {{"local_path", src.string()}, {"remote_path", "/x"}});
    t::expect(remote_fail.is_error, "remote failure errors");
    t::expect(remote_fail.text.contains("disk full"), "stderr surfaced");
});

[[maybe_unused]] auto const t14 = t::add_test("download_file", [] {
    auto const dir = temp_dir();
    auto const dst = dir / "sub" / "got.bin";
    std::filesystem::remove(dst);
    auto const raw = std::string{"\x00down\xff", 6};
    auto captured = sshmcp::spawn_request_t{};
    auto context = context_with({.stdout_text = raw, .exit_code = 0}, captured);
    auto const result = sshmcp::download_file_tool_t::invoke(
        context,
        {{"remote_path", "/var/log/x y.log"}, {"local_path", dst.string()}});
    t::expect(!result.is_error, "ok");
    t::expect(captured.argv.back() == "cat '/var/log/x y.log'",
              "remote command");
    auto in = std::ifstream{dst, std::ios::binary};
    auto const got = std::string{std::istreambuf_iterator<char>{in},
                                 std::istreambuf_iterator<char>{}};
    t::expect(got == raw, "bytes on disk (parent created)");

    auto failing = context_with(
        {.stderr_text = "cat: no such file", .exit_code = 1}, captured);
    auto const remote_fail = sshmcp::download_file_tool_t::invoke(
        failing, {{"remote_path", "/nope"}, {"local_path", dst.string()}});
    t::expect(remote_fail.is_error, "remote failure errors");
});

} // namespace
