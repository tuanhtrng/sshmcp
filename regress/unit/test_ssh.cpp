#include "harness.hpp"

#include <sshmcp/ssh.hpp>

#include <string>
#include <vector>

namespace {

namespace t = sshmcp::test;

[[maybe_unused]] auto const t1 = t::add_test("quote_posix", [] {
    t::expect(sshmcp::quote_posix("abc") == "'abc'", "plain");
    t::expect(sshmcp::quote_posix("a b") == "'a b'", "space");
    t::expect(sshmcp::quote_posix("a'b") == "'a'\\''b'", "embedded quote");
    t::expect(sshmcp::quote_posix("") == "''", "empty");
});

[[maybe_unused]] auto const t2 = t::add_test("split_command", [] {
    t::expect(sshmcp::split_command("ssh") == std::vector<std::string>{"ssh"},
              "single");
    t::expect(sshmcp::split_command("a  b\tc") ==
                  std::vector<std::string>{"a", "b", "c"},
              "whitespace runs");
    t::expect(sshmcp::split_command("\"C:\\Program Files\\p\" x.py") ==
                  std::vector<std::string>{"C:\\Program Files\\p", "x.py"},
              "quoted token");
    t::expect(sshmcp::split_command("\"\"").size() == 1,
              "empty quoted token survives");
    t::expect(sshmcp::split_command("").empty(), "empty");
    t::expect(sshmcp::split_command("  \t ").empty(), "blank");
});

[[maybe_unused]] auto const t3 = t::add_test("build_ssh_argv", [] {
    auto config = sshmcp::config_t{};
    config.target = "dev@host";
    config.ssh_args = {"-p", "2222"};
    auto const argv = sshmcp::build_ssh_argv(config, "echo hi");
    auto const want =
        std::vector<std::string>{"ssh", "-T",   "-o",       "BatchMode=yes",
                                 "-p",  "2222", "dev@host", "echo hi"};
    t::expect(argv == want, "argv layout");
});

[[maybe_unused]] auto const t4 = t::add_test("remote commands", [] {
    t::expect(sshmcp::exec_command("make", std::nullopt) == "make", "no cwd");
    t::expect(sshmcp::exec_command("make", std::string{"/src"}) ==
                  "cd '/src' && make",
              "cwd prepended");
    t::expect(sshmcp::read_command("/a b") == "cat '/a b'", "read");
    t::expect(sshmcp::write_command("/d/f.txt") ==
                  "mkdir -p '/d' && cat > '/d/f.txt'",
              "write with dir");
    t::expect(sshmcp::write_command("f.txt") == "cat > 'f.txt'",
              "write no dir");
    t::expect(sshmcp::write_command("/f.txt") == "cat > '/f.txt'",
              "write at root");
});

} // namespace
