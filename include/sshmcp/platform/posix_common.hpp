#pragma once

#include <sshmcp/types.hpp>

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <expected>
#include <string>
#include <utility>
#include <vector>

namespace sshmcp {

inline auto posix_spawn_capture(spawn_request_t const& request)
    -> std::expected<spawn_result_t, error_t> {
    int in_pipe[2];
    int out_pipe[2];
    int err_pipe[2];
    if (pipe(in_pipe) != 0 || pipe(out_pipe) != 0 ||
        pipe(err_pipe) != 0) {
        return std::unexpected{error_t{"pipe() failed"}};
    }
    auto const pid = fork();
    if (pid < 0) {
        return std::unexpected{error_t{"fork() failed"}};
    }
    if (pid == 0) {
        dup2(in_pipe[0], 0);
        dup2(out_pipe[1], 1);
        dup2(err_pipe[1], 2);
        close(in_pipe[0]);
        close(in_pipe[1]);
        close(out_pipe[0]);
        close(out_pipe[1]);
        close(err_pipe[0]);
        close(err_pipe[1]);
        auto argv_c = std::vector<char*>{};
        argv_c.reserve(request.argv.size() + 1);
        for (auto const& arg : request.argv) {
            argv_c.push_back(const_cast<char*>(arg.c_str()));
        }
        argv_c.push_back(nullptr);
        execvp(argv_c[0], argv_c.data());
        _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);
    fcntl(in_pipe[1], F_SETFL, O_NONBLOCK);
    fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(err_pipe[0], F_SETFL, O_NONBLOCK);
    auto in_open = !request.stdin_data.empty();
    if (!in_open) {
        close(in_pipe[1]);
    }
    auto const deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds{request.timeout_ms};
    auto stdout_text = std::string{};
    auto stderr_text = std::string{};
    auto stdin_offset = std::size_t{0};
    auto out_open = true;
    auto err_open = true;
    auto is_timed_out = false;
    char buffer[4096];
    while (out_open || err_open) {
        auto const now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            is_timed_out = true;
            kill(pid, SIGKILL);
            break;
        }
        auto const wait_ms =
            std::chrono::duration_cast<
                std::chrono::milliseconds>(deadline - now)
                .count();
        pollfd fds[3] = {
            {in_open ? in_pipe[1] : -1, POLLOUT, 0},
            {out_open ? out_pipe[0] : -1, POLLIN, 0},
            {err_open ? err_pipe[0] : -1, POLLIN, 0}};
        if (poll(fds, 3, static_cast<int>(wait_ms)) < 0) {
            break;
        }
        if (in_open && (fds[0].revents & (POLLOUT | POLLERR))) {
            auto const n = write(
                in_pipe[1],
                request.stdin_data.data() + stdin_offset,
                request.stdin_data.size() - stdin_offset);
            if (n > 0) {
                stdin_offset += static_cast<std::size_t>(n);
            }
            if (n <= 0 ||
                stdin_offset >= request.stdin_data.size()) {
                close(in_pipe[1]);
                in_open = false;
            }
        }
        if (out_open && (fds[1].revents & (POLLIN | POLLHUP))) {
            auto const n =
                read(out_pipe[0], buffer, sizeof(buffer));
            if (n <= 0) {
                close(out_pipe[0]);
                out_open = false;
            } else {
                stdout_text.append(
                    buffer, static_cast<std::size_t>(n));
            }
        }
        if (err_open && (fds[2].revents & (POLLIN | POLLHUP))) {
            auto const n =
                read(err_pipe[0], buffer, sizeof(buffer));
            if (n <= 0) {
                close(err_pipe[0]);
                err_open = false;
            } else {
                stderr_text.append(
                    buffer, static_cast<std::size_t>(n));
            }
        }
    }
    if (in_open) {
        close(in_pipe[1]);
    }
    if (out_open) {
        close(out_pipe[0]);
    }
    if (err_open) {
        close(err_pipe[0]);
    }
    auto status = 0;
    waitpid(pid, &status, 0);
    auto const exit_code =
        WIFEXITED(status) ? WEXITSTATUS(status) : 128;
    return spawn_result_t{
        .stdout_text = std::move(stdout_text),
        .stderr_text = std::move(stderr_text),
        .exit_code = exit_code,
        .is_timed_out = is_timed_out};
}

}  // namespace sshmcp
