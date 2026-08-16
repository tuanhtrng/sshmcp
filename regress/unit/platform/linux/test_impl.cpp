#include "harness.hpp"

#include <sshmcp/subprocess.hpp>

#include <string>

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

} // namespace
