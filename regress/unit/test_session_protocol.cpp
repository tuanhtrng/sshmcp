#include "harness.hpp"

#include <sshmcp/session.hpp>

#include <string>

namespace {

namespace t = sshmcp::test;

[[maybe_unused]] auto const t1 = t::add_test("payload", [] {
    auto const payload = sshmcp::session_payload(7, "echo hi");
    t::expect(payload == "{\n"
                         "echo hi\n"
                         "} </dev/null\n"
                         "printf '\\n@sshmcp:7:%s@\\n' \"$?\"\n"
                         "printf '\\n@sshmcp:7@\\n' >&2\n",
              "exact payload");
});

[[maybe_unused]] auto const t2 = t::add_test("scan stdout", [] {
    auto const missing = sshmcp::scan_stdout("partial out", 3);
    t::expect(!missing.is_found, "not yet");

    auto const empty = sshmcp::scan_stdout("\n@sshmcp:3:0@\n", 3);
    t::expect(empty.is_found, "empty found");
    t::expect(empty.output == "", "empty output");
    t::expect(empty.exit_code == 0, "exit 0");

    auto const with_output = sshmcp::scan_stdout("hi\n\n@sshmcp:3:42@\n", 3);
    t::expect(with_output.is_found, "found");
    t::expect(with_output.output == "hi\n", "keeps newline");
    t::expect(with_output.exit_code == 42, "exit 42");

    auto const no_trailing_nl = sshmcp::scan_stdout("abc\n@sshmcp:3:1@\n", 3);
    t::expect(no_trailing_nl.output == "abc", "output without own newline");

    auto const partial_marker = sshmcp::scan_stdout("x\n@sshmcp:3:4", 3);
    t::expect(!partial_marker.is_found, "marker split across chunks");

    auto const wrong_counter = sshmcp::scan_stdout("\n@sshmcp:2:0@\n", 3);
    t::expect(!wrong_counter.is_found, "old counter ignored");

    auto const decoy =
        sshmcp::scan_stdout("log @sshmcp:3:9@ inline\n\n@sshmcp:3:5@\n", 3);
    t::expect(decoy.is_found && decoy.exit_code == 5,
              "marker must start a line");
});

[[maybe_unused]] auto const t3 = t::add_test("scan stderr", [] {
    auto const found = sshmcp::scan_stderr("warn\n\n@sshmcp:9@\n", 9);
    t::expect(found.is_found, "found");
    t::expect(found.output == "warn\n", "stderr output");
    t::expect(!sshmcp::scan_stderr("\n@sshmcp:9:0@\n", 9).is_found,
              "stdout marker not stderr marker");
});

} // namespace
