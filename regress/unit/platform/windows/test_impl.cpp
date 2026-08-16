#include "harness.hpp"

#include <sshmcp/subprocess.hpp>

#include <string>

namespace {

namespace t = sshmcp::test;

[[maybe_unused]] auto const t1 = t::add_test("win quoting", [] {
    t::expect(sshmcp::quote_win_arg(L"plain") == L"plain", "no quotes needed");
    t::expect(sshmcp::quote_win_arg(L"a b") == L"\"a b\"", "spaces quoted");
    t::expect(sshmcp::quote_win_arg(L"a\"b") == L"\"a\\\"b\"", "quote escaped");
    t::expect(sshmcp::quote_win_arg(L"end\\") == L"end\\",
              "bare backslash needs no quoting");
    t::expect(sshmcp::quote_win_arg(L"a b\\") == L"\"a b\\\\\"",
              "trailing backslash doubled inside quotes");
    t::expect(sshmcp::quote_win_arg(L"") == L"\"\"", "empty quoted");
});

[[maybe_unused]] auto const t2 = t::add_test("spawn echo", [] {
    auto impl = sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run({.argv = {"cmd", "/c", "echo hi"},
                                  .stdin_data = {},
                                  .timeout_ms = 30'000});
    t::expect(result.has_value(), "spawn ok");
    t::expect(result->exit_code == 0, "exit 0");
    t::expect(result->stdout_text.contains("hi"), "stdout");
    t::expect(!result->is_timed_out, "no timeout");
});

[[maybe_unused]] auto const t3 = t::add_test("spawn stdin", [] {
    auto impl = sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run({.argv = {"findstr", "x"},
                                  .stdin_data = "axb\nqqq\n",
                                  .timeout_ms = 30'000});
    t::expect(result.has_value(), "spawn ok");
    t::expect(result->stdout_text.contains("axb"), "stdin consumed");
});

[[maybe_unused]] auto const t4 = t::add_test("spawn timeout", [] {
    auto impl = sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run({.argv = {"ping", "-n", "30", "127.0.0.1"},
                                  .stdin_data = {},
                                  .timeout_ms = 500});
    t::expect(result.has_value(), "spawn ok");
    t::expect(result->is_timed_out, "timed out");
});

auto python_argv(std::string code) -> std::vector<std::string> {
    return {"python", "-u", "-c", std::move(code)};
}

[[maybe_unused]] auto const s1 = t::add_test("stream echo", [] {
    auto impl = sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const spawned = impl.stream_spawn(python_argv(
        "import sys;[(sys.stdout.buffer.write(('E:'+l).encode()),"
        "sys.stdout.buffer.flush()) for l in iter(sys.stdin.readline,'')]"));
    t::expect(spawned.has_value(), "spawn ok");
    auto const id = *spawned;
    t::expect(impl.stream_write(id, "hello\n"), "write ok");
    auto text = std::string{};
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{20};
    while (!text.contains("E:hello\n")) {
        auto const chunk =
            impl.stream_read(id, sshmcp::stream_t::STDOUT, deadline);
        t::expect(chunk.has_value(), "read ok");
        if (!chunk.has_value() || chunk->is_closed ||
            std::chrono::steady_clock::now() >= deadline) {
            break;
        }
        text += chunk->data;
    }
    t::expect(text == "E:hello\n", "echo round trip");
    impl.stream_kill(id);
    impl.stream_kill(id); // idempotent
});

[[maybe_unused]] auto const s2 = t::add_test("stream close", [] {
    auto impl = sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const spawned = impl.stream_spawn(python_argv("print('bye')"));
    t::expect(spawned.has_value(), "spawn ok");
    auto const deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds{20};
    auto text = std::string{};
    auto closed = false;
    while (!closed && std::chrono::steady_clock::now() < deadline) {
        auto const chunk =
            impl.stream_read(*spawned, sshmcp::stream_t::STDOUT, deadline);
        t::expect(chunk.has_value(), "read ok");
        if (!chunk.has_value()) {
            break;
        }
        text += chunk->data;
        closed = chunk->is_closed;
    }
    t::expect(text.contains("bye"), "output before close");
    t::expect(closed, "close observed");
    impl.stream_kill(*spawned);
});

[[maybe_unused]] auto const s3 = t::add_test("stream slice", [] {
    auto impl = sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const spawned =
        impl.stream_spawn(python_argv("import time;time.sleep(30)"));
    t::expect(spawned.has_value(), "spawn ok");
    auto const chunk = impl.stream_read(*spawned, sshmcp::stream_t::STDOUT,
                                        std::chrono::steady_clock::now() +
                                            std::chrono::milliseconds{300});
    t::expect(chunk.has_value(), "read ok");
    t::expect(chunk->data.empty() && !chunk->is_closed,
              "deadline slice is empty and open");
    impl.stream_kill(*spawned);
});

[[maybe_unused]] auto const t5 = t::add_test("spawn missing", [] {
    auto impl = sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run({.argv = {"definitely-not-a-real-exe-xyz"},
                                  .stdin_data = {},
                                  .timeout_ms = 5'000});
    t::expect(!result.has_value(), "spawn fails cleanly");
});

} // namespace
