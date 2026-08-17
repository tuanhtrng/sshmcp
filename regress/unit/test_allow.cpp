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

[[maybe_unused]] auto const t5 = t::add_test("parse_deny", [] {
    auto const parsed = sshmcp::parse_deny("cvs commit, rm  -rf ,,");
    auto const want =
        std::vector<std::vector<std::string>>{{"cvs", "commit"}, {"rm", "-rf"}};
    t::expect(parsed == want, "comma split then word split");
    t::expect(sshmcp::parse_deny("").empty(), "empty");
});

[[maybe_unused]] auto const t6 = t::add_test("deny empty", [] {
    auto const none = std::vector<std::vector<std::string>>{};
    t::expect(sshmcp::check_denied("cvs commit -m x", none).has_value(),
              "empty list passes everything");
});

[[maybe_unused]] auto const t7 = t::add_test("deny match", [] {
    auto const deny = std::vector<std::vector<std::string>>{{"cvs", "commit"}};
    auto const blocked = sshmcp::check_denied("cvs commit -m x", deny);
    t::expect(!blocked.has_value(), "head plus subcommand blocked");
    t::expect(blocked.error().message.contains("'cvs commit'"),
              "names blocked entry");
    t::expect(!sshmcp::check_denied("cvs -q commit", deny).has_value(),
              "subcommand after flags still blocked");
    t::expect(!sshmcp::check_denied("  \tcvs commit", deny).has_value(),
              "leading whitespace tokenizes");
    t::expect(sshmcp::check_denied("cvs diff", deny).has_value(),
              "other subcommand passes");
    t::expect(sshmcp::check_denied("git commit -m x", deny).has_value(),
              "other head passes");
    t::expect(sshmcp::check_denied("echo cvs commit", deny).has_value(),
              "entry head must be command head");
});

[[maybe_unused]] auto const t8 = t::add_test("deny head only", [] {
    auto const deny = std::vector<std::vector<std::string>>{{"shutdown"}};
    t::expect(!sshmcp::check_denied("shutdown -h now", deny).has_value(),
              "single token entry blocks head");
    t::expect(sshmcp::check_denied("ls shutdown", deny).has_value(),
              "argument mention passes");
});

[[maybe_unused]] auto const t9 = t::add_test("deny syntax", [] {
    auto const deny = std::vector<std::vector<std::string>>{{"cvs", "commit"}};
    for (auto const command :
         {"ls; cvs commit", "ls && cvs commit", "true || cvs commit",
          "echo x | cvs commit", "cvs commit & ", "echo `cvs commit`",
          "echo $(cvs commit)", "ls\ncvs commit"}) {
        auto const blocked = sshmcp::check_denied(command, deny);
        t::expect(!blocked.has_value(), "compound rejected while deny active");
        t::expect(blocked.error().message.contains("compound"),
                  "syntax message");
    }
});

} // namespace
