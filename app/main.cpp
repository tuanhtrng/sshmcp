#include <sshmcp/version.hpp>

#include <print>

auto main() -> int {
    std::println(stderr, "sshmcp {} scaffold", sshmcp::VERSION);
    return 0;
}
