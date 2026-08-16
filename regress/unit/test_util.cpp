#include "harness.hpp"

#include <sshmcp/util.hpp>

#include <string>

namespace {

namespace t = sshmcp::test;

[[maybe_unused]] auto const t1 = t::add_test("truncate", [] {
    auto const small = sshmcp::truncate_middle("abc", 10);
    t::expect(small.text == "abc", "small unchanged");
    t::expect(!small.is_truncated, "small flag");

    auto const big =
        sshmcp::truncate_middle(std::string(100, 'x'), 10);
    t::expect(big.is_truncated, "big flag");
    t::expect(big.text.starts_with("xxxxx"), "head kept");
    t::expect(big.text.ends_with("xxxxx"), "tail kept");
    t::expect(big.text.contains("[sshmcp: 90 chars truncated]"),
              "marker");

    auto const exact =
        sshmcp::truncate_middle(std::string(10, 'y'), 10);
    t::expect(!exact.is_truncated, "exact fits");
});

}  // namespace
