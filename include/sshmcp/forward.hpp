#pragma once

#include <sshmcp/ssh.hpp>
#include <sshmcp/types.hpp>

#include <chrono>
#include <cstdint>
#include <expected>
#include <format>
#include <map>
#include <string>
#include <string_view>
#include <utility>

namespace sshmcp {

template <typename PlatformType>
class forward_manager_t {
  public:
    forward_manager_t(PlatformType& platform, config_t config,
                      std::int64_t health_ms = 500) // style-ok
        : platform{platform}, config{std::move(config)}, health_ms{health_ms} {}

    auto open(int local_port, std::string_view remote_host, int remote_port)
        -> std::expected<std::string, error_t> {
        if (forwards.contains(local_port)) {
            return std::unexpected{
                error_t{std::format("forward already open: {}", local_port)}};
        }
        if (forwards.size() >= MAX_FORWARDS) {
            auto message = std::string{"forward limit reached; live:"};
            for (auto const& [port, id] : forwards) {
                message += std::format(" {}", port);
            }
            return std::unexpected{error_t{std::move(message)}};
        }
        auto spawned = platform.stream_spawn(
            build_forward_argv(config, local_port, remote_host, remote_port));
        if (!spawned) {
            return std::unexpected{spawned.error()};
        }
        // ssh -N is silent on success; an early exit means the
        // forward failed. Watch stderr briefly.
        auto const deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds{health_ms};
        auto stderr_text = std::string{};
        while (std::chrono::steady_clock::now() < deadline) {
            auto const chunk =
                platform.stream_read(*spawned, stream_t::STDERR, deadline);
            if (!chunk) {
                break;
            }
            stderr_text += chunk->data;
            if (chunk->is_closed) {
                platform.stream_kill(*spawned);
                return std::unexpected{
                    error_t{"forward failed: " + stderr_text}};
            }
        }
        forwards.emplace(local_port, *spawned);
        return std::format("forwarding 127.0.0.1:{} -> {}:{}", local_port,
                           remote_host, remote_port);
    }

    auto close(int local_port) -> bool {
        auto const found = forwards.find(local_port);
        if (found == forwards.end()) {
            return false;
        }
        platform.stream_kill(found->second);
        forwards.erase(found);
        return true;
    }

  private:
    PlatformType& platform;
    config_t config;
    std::int64_t health_ms;
    std::map<int, typename PlatformType::stream_id_t> forwards;
};

} // namespace sshmcp
