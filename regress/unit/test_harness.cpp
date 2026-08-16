#include "harness.hpp"

namespace {

namespace t = sshmcp::test;

[[maybe_unused]] auto const t1 =
    t::add_test("harness registers and counts", [] {
        t::expect(true, "trivially true");
        t::expect(!t::state().cases.empty(), "case registered");
    });

}  // namespace
