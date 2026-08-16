#include "harness.hpp"

#include <sshmcp/session.hpp>

#include <chrono>
#include <deque>
#include <expected>
#include <format>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace t = sshmcp::test;

// Scripted platform: every payload write produces one framed
// stdout reply and one framed stderr reply, split into awkward
// chunks.
struct mock_platform_t {
    struct state_t {
        std::deque<std::string> out_chunks;
        std::deque<std::string> err_chunks;
        std::vector<std::string> written;
        bool is_killed{};
    };
    using stream_id_t = std::shared_ptr<state_t>;

    std::string next_stdout{"ok\n"};
    int next_exit{0};
    bool fail_spawn{};
    bool hang{}; // never emit sentinels
    int spawn_count{};
    stream_id_t last;

    auto stream_spawn(std::vector<std::string> const&)
        -> std::expected<stream_id_t, sshmcp::error_t> {
        if (fail_spawn) {
            return std::unexpected{sshmcp::error_t{"spawn refused"}};
        }
        ++spawn_count;
        last = std::make_shared<state_t>();
        return last;
    }

    auto stream_write(stream_id_t const& id, std::string_view data) -> bool {
        id->written.emplace_back(data);
        if (hang) {
            return true;
        }
        auto const text = std::string{data};
        auto const tag = text.find("@sshmcp:");
        auto const colon = text.find(':', tag + 8);
        auto const counter = text.substr(tag + 8, colon - tag - 8);
        auto const out =
            next_stdout + std::format("\n@sshmcp:{}:{}@\n", counter, next_exit);
        id->out_chunks.push_back(out.substr(0, out.size() / 2));
        id->out_chunks.push_back(out.substr(out.size() / 2));
        id->err_chunks.push_back(std::format("\n@sshmcp:{}@\n", counter));
        return true;
    }

    auto stream_read(stream_id_t const& id, sshmcp::stream_t which,
                     std::chrono::steady_clock::time_point)
        -> std::expected<sshmcp::chunk_t, sshmcp::error_t> {
        auto& queue =
            which == sshmcp::stream_t::STDOUT ? id->out_chunks : id->err_chunks;
        if (queue.empty()) {
            return sshmcp::chunk_t{};
        }
        auto chunk = sshmcp::chunk_t{.data = queue.front()};
        queue.pop_front();
        return chunk;
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

[[maybe_unused]] auto const t1 = t::add_test("session exec", [] {
    auto platform = mock_platform_t{};
    auto manager =
        sshmcp::session_manager_t<mock_platform_t>{platform, config_of()};
    platform.next_stdout = "hi\n";
    platform.next_exit = 3;
    auto const result = manager.exec("dev", "echo hi", 5'000);
    t::expect(result.has_value(), "exec ok");
    t::expect(result->result.stdout_text == "hi\n", "stdout");
    t::expect(result->result.exit_code == 3, "exit");
    t::expect(!result->is_session_dead, "alive");
    t::expect(platform.spawn_count == 1, "one spawn");

    manager.exec("dev", "echo again", 5'000);
    t::expect(platform.spawn_count == 1, "session reused, no respawn");
    t::expect(platform.last->written.size() == 2, "two payloads written");
    t::expect(platform.last->written[1].contains("echo again"),
              "command in payload");
});

[[maybe_unused]] auto const t2 = t::add_test("session close", [] {
    auto platform = mock_platform_t{};
    auto manager =
        sshmcp::session_manager_t<mock_platform_t>{platform, config_of()};
    manager.exec("dev", "true", 5'000);
    t::expect(manager.close("dev"), "close known");
    t::expect(platform.last->is_killed, "killed");
    t::expect(!manager.close("dev"), "close unknown");
    manager.exec("dev", "true", 5'000);
    t::expect(platform.spawn_count == 2, "recreated after close");
});

[[maybe_unused]] auto const t3 = t::add_test("session cap", [] {
    auto platform = mock_platform_t{};
    auto manager =
        sshmcp::session_manager_t<mock_platform_t>{platform, config_of()};
    manager.exec("a", "true", 5'000);
    manager.exec("b", "true", 5'000);
    manager.exec("c", "true", 5'000);
    manager.exec("d", "true", 5'000);
    auto const fifth = manager.exec("e", "true", 5'000);
    t::expect(!fifth.has_value(), "cap enforced");
    t::expect(fifth.error().message.contains("a"), "error names live sessions");
});

[[maybe_unused]] auto const t4 = t::add_test("session timeout", [] {
    auto platform = mock_platform_t{};
    platform.hang = true;
    auto manager =
        sshmcp::session_manager_t<mock_platform_t>{platform, config_of()};
    auto const result = manager.exec("dev", "sleep 99", 200);
    t::expect(result.has_value(), "timeout is a result");
    t::expect(result->result.is_timed_out, "flagged");
    t::expect(result->result.exit_code == -1, "exit -1");
    t::expect(result->is_session_dead, "session dead");
    t::expect(platform.last->is_killed, "killed");
    manager.exec("dev", "true", 5'000);
    t::expect(platform.spawn_count == 2, "dead session recreated");
});

[[maybe_unused]] auto const t5 = t::add_test("spawn failure", [] {
    auto platform = mock_platform_t{};
    platform.fail_spawn = true;
    auto manager =
        sshmcp::session_manager_t<mock_platform_t>{platform, config_of()};
    auto const result = manager.exec("dev", "true", 5'000);
    t::expect(!result.has_value(), "spawn error surfaces");
});

} // namespace
