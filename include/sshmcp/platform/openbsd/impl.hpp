#pragma once

#include <sshmcp/platform/posix_common.hpp>
#include <sshmcp/subprocess_base.hpp>
#include <sshmcp/types.hpp>

#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstdio>
#include <expected>
#include <memory>
#include <print>
#include <string>
#include <string_view>
#include <vector>

namespace sshmcp {

class platform_impl_t : public subprocess_base_t<platform_impl_t> {
  public:
    using subprocess_base_t<platform_impl_t>::subprocess_base_t;

    auto init_stdio_impl() -> void {
        signal(SIGPIPE, SIG_IGN);
    }

    auto harden_impl() -> void {
        if (pledge("stdio rpath proc exec", nullptr) != 0) {
            std::println(stderr, "sshmcp: pledge unavailable, continuing");
        }
    }

    auto spawn_impl(spawn_request_t const& request)
        -> std::expected<spawn_result_t, error_t> {
        return posix_spawn_capture(request);
    }

    using stream_id_t = std::shared_ptr<posix_stream_state_t>;

    auto stream_spawn_impl(std::vector<std::string> const& argv)
        -> std::expected<stream_id_t, error_t> {
        return posix_stream_spawn(argv);
    }

    auto stream_write_impl(stream_id_t const& id, std::string_view data)
        -> bool {
        auto offset = std::size_t{0};
        while (offset < data.size()) {
            auto const count =
                write(id->stdin_fd, data.data() + offset, data.size() - offset);
            if (count <= 0) {
                return false;
            }
            offset += static_cast<std::size_t>(count);
        }
        return true;
    }

    auto stream_read_impl(stream_id_t const& id, stream_t which,
                          std::chrono::steady_clock::time_point deadline)
        -> std::expected<chunk_t, error_t> {
        return posix_stream_read(which == stream_t::STDOUT ? id->stdout_fd
                                                           : id->stderr_fd,
                                 deadline);
    }

    auto stream_kill_impl(stream_id_t const& id) -> void {
        if (id->is_killed) {
            return;
        }
        id->is_killed = true;
        kill(id->pid, SIGKILL);
        auto status = 0;
        waitpid(id->pid, &status, 0);
    }
};

} // namespace sshmcp
