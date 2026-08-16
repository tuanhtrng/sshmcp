#include "harness.hpp"

#include <sshmcp/subprocess.hpp>

#include <string>

namespace {

namespace t = sshmcp::test;

[[maybe_unused]] auto const t1 = t::add_test("win quoting", [] {
    t::expect(sshmcp::quote_win_arg(L"plain") == L"plain",
              "no quotes needed");
    t::expect(sshmcp::quote_win_arg(L"a b") == L"\"a b\"",
              "spaces quoted");
    t::expect(sshmcp::quote_win_arg(L"a\"b") == L"\"a\\\"b\"",
              "quote escaped");
    t::expect(sshmcp::quote_win_arg(L"end\\") == L"end\\",
              "bare backslash needs no quoting");
    t::expect(sshmcp::quote_win_arg(L"a b\\") ==
                  L"\"a b\\\\\"",
              "trailing backslash doubled inside quotes");
    t::expect(sshmcp::quote_win_arg(L"") == L"\"\"",
              "empty quoted");
});

[[maybe_unused]] auto const t2 = t::add_test("spawn echo", [] {
    auto impl =
        sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result =
        impl.run({.argv = {"cmd", "/c", "echo hi"},
                  .stdin_data = {},
                  .timeout_ms = 30'000});
    t::expect(result.has_value(), "spawn ok");
    t::expect(result->exit_code == 0, "exit 0");
    t::expect(result->stdout_text.contains("hi"), "stdout");
    t::expect(!result->is_timed_out, "no timeout");
});

[[maybe_unused]] auto const t3 = t::add_test("spawn stdin", [] {
    auto impl =
        sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run({.argv = {"findstr", "x"},
                                  .stdin_data = "axb\nqqq\n",
                                  .timeout_ms = 30'000});
    t::expect(result.has_value(), "spawn ok");
    t::expect(result->stdout_text.contains("axb"),
              "stdin consumed");
});

[[maybe_unused]] auto const t4 = t::add_test("spawn timeout", [] {
    auto impl =
        sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run(
        {.argv = {"cmd", "/c", "ping -n 30 127.0.0.1 >nul"},
         .stdin_data = {},
         .timeout_ms = 500});
    t::expect(result.has_value(), "spawn ok");
    t::expect(result->is_timed_out, "timed out");
});

[[maybe_unused]] auto const t5 = t::add_test("spawn missing", [] {
    auto impl =
        sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result =
        impl.run({.argv = {"definitely-not-a-real-exe-xyz"},
                  .stdin_data = {},
                  .timeout_ms = 5'000});
    t::expect(!result.has_value(), "spawn fails cleanly");
});

}  // namespace
