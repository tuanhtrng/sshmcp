#include "harness.hpp"

#include <sshmcp/allow.hpp>

#include <string>
#include <vector>

namespace {

namespace t = sshmcp::test;

[[maybe_unused]] auto const t1 = t::add_test("parse_allow", [] {
    auto const parsed = sshmcp::parse_allow("git, make ,ls,,");
    auto const want = std::vector<std::string>{"git", "make", "ls"};
    t::expect(parsed == want, "comma split with trim");
    t::expect(sshmcp::parse_allow("").empty(), "empty");
});

[[maybe_unused]] auto const t2 = t::add_test("allow empty", [] {
    auto const none = std::vector<std::string>{};
    t::expect(sshmcp::check_allowed("rm -rf /", none).has_value(),
              "empty list passes everything");
});

[[maybe_unused]] auto const t3 = t::add_test("allow tokens", [] {
    auto const allow = std::vector<std::string>{"git", "ls"};
    t::expect(sshmcp::check_allowed("git status", allow).has_value(),
              "listed passes");
    t::expect(sshmcp::check_allowed("  \tls -la", allow).has_value(),
              "leading whitespace tokenizes");
    auto const denied = sshmcp::check_allowed("rm x", allow);
    t::expect(!denied.has_value(), "unlisted rejected");
    t::expect(denied.error().message.contains("'rm'"), "names offender");
});

[[maybe_unused]] auto const t4 = t::add_test("allow syntax", [] {
    auto const allow = std::vector<std::string>{"git", "echo"};
    for (auto const command :
         {"git a; git b", "git a && b", "git a || b", "git a | wc", "git a & ",
          "echo `id`", "echo $(id)", "git a\ngit b"}) {
        auto const denied = sshmcp::check_allowed(command, allow);
        t::expect(!denied.has_value(), "compound rejected");
        t::expect(denied.error().message.contains("compound"),
                  "syntax message");
    }
});

} // namespace
