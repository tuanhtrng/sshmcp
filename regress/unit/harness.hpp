#pragma once

#include <functional>
#include <print>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sshmcp::test {

struct case_t {
    std::string name;
    std::function<void()> body;
};

struct state_t {
    std::vector<case_t> cases;
    int checks_total{};
    int checks_failed{};
};

inline auto state() -> state_t& {
    static auto instance = state_t{};
    return instance;
}

inline auto add_test(std::string_view name, std::function<void()> body)
    -> bool {
    state().cases.push_back({std::string{name}, std::move(body)});
    return true;
}

inline auto expect(bool is_ok, std::string_view what,
                   std::source_location loc = // style-ok
                   std::source_location::current()) -> void {
    ++state().checks_total;
    if (!is_ok) {
        ++state().checks_failed;
        std::println(stderr, "FAIL {}:{}: {}", loc.file_name(), loc.line(),
                     what);
    }
}

inline auto run_all() -> int {
    for (auto const& test_case : state().cases) {
        test_case.body();
    }
    std::println(stderr, "{} cases, {} checks, {} failed", state().cases.size(),
                 state().checks_total, state().checks_failed);
    return state().checks_failed == 0 ? 0 : 1;
}

} // namespace sshmcp::test
