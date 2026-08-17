#include "harness.hpp"

#include <sshmcp/forward.hpp>

#include <chrono>
#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace t = sshmcp::test;

struct mock_platform_t {
    struct state_t {
        std::vector<std::string> argv;
        bool is_killed{};
    };
    using stream_id_t = std::shared_ptr<state_t>;

    bool fail_forward{};
    std::string fail_text{"bind: address in use"};
    int spawn_count{};
    stream_id_t last;

    auto stream_spawn(std::vector<std::string> const& argv)
        -> std::expected<stream_id_t, sshmcp::error_t> {
        ++spawn_count;
        last = std::make_shared<state_t>();
        last->argv = argv;
        return last;
    }

    auto stream_read(stream_id_t const&, sshmcp::stream_t,
                     std::chrono::steady_clock::time_point deadline)
        -> std::expected<sshmcp::chunk_t, sshmcp::error_t> {
        if (fail_forward) {
            return sshmcp::chunk_t{.data = fail_text, .is_closed = true};
        }
        // Healthy forward: silent stderr; simulate waiting out
        // the health window.
        while (std::chrono::steady_clock::now() < deadline) {
        }
        return sshmcp::chunk_t{};
    }

    auto stream_write(stream_id_t const&, std::string_view) -> bool {
        return true;
    }

    auto stream_kill(stream_id_t const& id) -> void {
        id->is_killed = true;
    }
};

auto config_of() -> sshmcp::config_t {
    auto config = sshmcp::config_t{};
    config.target = "dev@host";
    return config;
}

[[maybe_unused]] auto const t1 = t::add_test("forward open", [] {
    auto platform = mock_platform_t{};
    auto manager =
        sshmcp::forward_manager_t<mock_platform_t>{platform, config_of(), 10};
    auto const opened = manager.open(18080, "localhost", 3000);
    t::expect(opened.has_value(), "opens");
    t::expect(*opened == "forwarding 127.0.0.1:18080 -> "
                         "localhost:3000",
              "success text");
    auto const want = std::vector<std::string>{
        "ssh",           "-N", "-o",
        "BatchMode=yes", "-L", "127.0.0.1:18080:localhost:3000",
        "dev@host"};
    t::expect(platform.last->argv == want, "argv layout");

    auto const duplicate = manager.open(18080, "localhost", 80);
    t::expect(!duplicate.has_value(), "duplicate rejected");
    t::expect(duplicate.error().message.contains("18080"), "names port");
});

[[maybe_unused]] auto const t2 = t::add_test("forward fail", [] {
    auto platform = mock_platform_t{};
    platform.fail_forward = true;
    auto manager =
        sshmcp::forward_manager_t<mock_platform_t>{platform, config_of(), 10};
    auto const opened = manager.open(18080, "localhost", 3000);
    t::expect(!opened.has_value(), "fails");
    t::expect(opened.error().message.contains("bind: address"),
              "stderr surfaced");
    t::expect(platform.last->is_killed, "child killed");
});

[[maybe_unused]] auto const t3 = t::add_test("forward close", [] {
    auto platform = mock_platform_t{};
    auto manager =
        sshmcp::forward_manager_t<mock_platform_t>{platform, config_of(), 10};
    t::expect(manager.open(18081, "localhost", 3000).has_value(), "open");
    t::expect(manager.close(18081), "close known");
    t::expect(platform.last->is_killed, "killed");
    t::expect(!manager.close(18081), "close unknown");
    t::expect(manager.open(18081, "localhost", 3000).has_value(),
              "reopen after close");
});

[[maybe_unused]] auto const t4 = t::add_test("forward cap", [] {
    auto platform = mock_platform_t{};
    auto manager =
        sshmcp::forward_manager_t<mock_platform_t>{platform, config_of(), 10};
    for (auto port = 19000; port < 19008; ++port) {
        t::expect(manager.open(port, "localhost", 80).has_value(),
                  "open under cap");
    }
    auto const ninth = manager.open(19008, "localhost", 80);
    t::expect(!ninth.has_value(), "cap enforced");
    t::expect(ninth.error().message.contains("19000"), "names live ports");
});

} // namespace
