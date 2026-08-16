#include "harness.hpp"

#include <sshmcp/base64.hpp>

#include <string>

namespace {

namespace t = sshmcp::test;

[[maybe_unused]] auto const t1 = t::add_test("b64 encode", [] {
    t::expect(sshmcp::base64_encode("") == "", "empty");
    t::expect(sshmcp::base64_encode("f") == "Zg==", "1 byte");
    t::expect(sshmcp::base64_encode("fo") == "Zm8=", "2 bytes");
    t::expect(sshmcp::base64_encode("foo") == "Zm9v", "3 bytes");
    t::expect(sshmcp::base64_encode("foobar") == "Zm9vYmFy", "rfc vector");
    auto const raw = std::string{"\x00\xff\x10", 3};
    t::expect(sshmcp::base64_encode(raw) == "AP8Q", "binary bytes");
});

[[maybe_unused]] auto const t2 = t::add_test("b64 decode", [] {
    t::expect(sshmcp::base64_decode("Zm9vYmFy").value() == "foobar",
              "round trip");
    t::expect(sshmcp::base64_decode("Zg==").value() == "f", "padding 2");
    t::expect(sshmcp::base64_decode("Zm8=").value() == "fo", "padding 1");
    t::expect(sshmcp::base64_decode("").value() == "", "empty");
    t::expect(!sshmcp::base64_decode("Zg=").has_value(), "bad length");
    t::expect(!sshmcp::base64_decode("Z!==").has_value(), "bad char");
    t::expect(!sshmcp::base64_decode("Zm9v YmFy").has_value(),
              "whitespace rejected");
    t::expect(!sshmcp::base64_decode("==AA").has_value(), "pad in front");
    auto const raw = std::string{"\x00\xff\x10", 3};
    t::expect(sshmcp::base64_decode("AP8Q").value() == raw,
              "binary round trip");
});

} // namespace
