#include "harness.hpp"

#include <sshmcp/subprocess.hpp>

#include <chrono>
#include <expected>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace t = sshmcp::test;

[[maybe_unused]] auto const t1 = t::add_test("spawn echo", [] {
    auto impl = sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run({.argv = {"/bin/sh", "-c", "echo hi"},
                                  .stdin_data = {},
                                  .timeout_ms = 30'000});
    t::expect(result.has_value(), "spawn ok");
    t::expect(result->exit_code == 0, "exit 0");
    t::expect(result->stdout_text == "hi\n", "stdout");
});

[[maybe_unused]] auto const t2 = t::add_test("spawn stdin", [] {
    auto impl = sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run({.argv = {"/bin/sh", "-c", "cat"},
                                  .stdin_data = "roundtrip",
                                  .timeout_ms = 30'000});
    t::expect(result.has_value(), "spawn ok");
    t::expect(result->stdout_text == "roundtrip", "stdin piped");
});

[[maybe_unused]] auto const t3 = t::add_test("spawn exit", [] {
    auto impl = sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run({.argv = {"/bin/sh", "-c", "exit 3"},
                                  .stdin_data = {},
                                  .timeout_ms = 30'000});
    t::expect(result->exit_code == 3, "exit code");
});

[[maybe_unused]] auto const t4 = t::add_test("spawn timeout", [] {
    auto impl = sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run({.argv = {"/bin/sh", "-c", "sleep 30"},
                                  .stdin_data = {},
                                  .timeout_ms = 500});
    t::expect(result->is_timed_out, "timed out");
});

auto python_argv(std::string code) -> std::vector<std::string> {
    return {"python3", "-u", "-c", std::move(code)};
}

// ubuntu CI + OpenBSD ship python3; fall back to python.
auto stream_spawn_python(sshmcp::platform_impl_t& impl, std::string code)
    -> std::expected<sshmcp::platform_impl_t::stream_id_t, sshmcp::error_t> {
    auto spawned = impl.stream_spawn(python_argv(code));
    if (spawned.has_value()) {
        return spawned;
    }
    return impl.stream_spawn({"python", "-u", "-c", std::move(code)});
}

[[maybe_unused]] auto const s1 = t::add_test("stream echo", [] {
    auto impl = sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const spawned = stream_spawn_python(
        impl,
        "import sys;[(sys.stdout.buffer.write(('E:'+l).encode()),"
        "sys.stdout.buffer.flush()) for l in iter(sys.stdin.readline,'')]");
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

[[maybe_unused]] auto const s2 = t::add_test("stream slice", [] {
    auto impl = sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const spawned =
        stream_spawn_python(impl, "import time;time.sleep(30)");
    t::expect(spawned.has_value(), "spawn ok");
    auto const chunk = impl.stream_read(*spawned, sshmcp::stream_t::STDOUT,
                                        std::chrono::steady_clock::now() +
                                            std::chrono::milliseconds{300});
    t::expect(chunk.has_value(), "read ok");
    t::expect(chunk->data.empty() && !chunk->is_closed,
              "deadline slice is empty and open");
    impl.stream_kill(*spawned);
});

} // namespace
