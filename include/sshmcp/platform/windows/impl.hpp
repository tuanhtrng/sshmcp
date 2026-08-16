#pragma once

#include <sshmcp/subprocess_base.hpp>
#include <sshmcp/types.hpp>

#include <windows.h>

#include <fcntl.h>
#include <io.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <expected>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace sshmcp {

struct stream_state_t {
    struct pipe_buffer_t {
        std::mutex mutex;
        std::condition_variable ready;
        std::string data;
        bool is_closed{};
    };
    HANDLE process{};
    HANDLE thread{};
    HANDLE stdin_write{};
    pipe_buffer_t out;
    pipe_buffer_t err;
    std::jthread out_reader;
    std::jthread err_reader;
    bool is_killed{};

    ~stream_state_t() {
        if (process != nullptr) {
            TerminateProcess(process, 1);
        }
        if (stdin_write != nullptr) {
            CloseHandle(stdin_write);
            stdin_write = nullptr;
        }
        if (out_reader.joinable()) {
            out_reader.join();
        }
        if (err_reader.joinable()) {
            err_reader.join();
        }
        if (process != nullptr) {
            CloseHandle(process);
        }
        if (thread != nullptr) {
            CloseHandle(thread);
        }
    }
};

inline auto utf8_to_wide(std::string_view text) -> std::wstring {
    if (text.empty()) {
        return {};
    }
    auto const size = MultiByteToWideChar(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    auto wide = std::wstring(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()),
                        wide.data(), size);
    return wide;
}

inline auto quote_win_arg(std::wstring_view arg) -> std::wstring {
    if (!arg.empty() &&
        arg.find_first_of(L" \t\n\v\"") == std::wstring_view::npos) {
        return std::wstring{arg};
    }
    auto out = std::wstring{L"\""};
    auto backslashes = std::size_t{0};
    for (auto const ch : arg) {
        if (ch == L'\\') {
            ++backslashes;
            continue;
        }
        if (ch == L'"') {
            out.append(backslashes * 2 + 1, L'\\');
            out.push_back(L'"');
        } else {
            out.append(backslashes, L'\\');
            out.push_back(ch);
        }
        backslashes = 0;
    }
    out.append(backslashes * 2, L'\\');
    out.push_back(L'"');
    return out;
}

inline auto build_command_line(std::vector<std::string> const& argv)
    -> std::wstring {
    auto line = std::wstring{};
    for (auto const& arg : argv) {
        if (!line.empty()) {
            line.push_back(L' ');
        }
        line += quote_win_arg(utf8_to_wide(arg));
    }
    return line;
}

class platform_impl_t : public subprocess_base_t<platform_impl_t> {
  public:
    using subprocess_base_t<platform_impl_t>::subprocess_base_t;

    auto init_stdio_impl() -> void {
        _setmode(_fileno(stdin), _O_BINARY);
        _setmode(_fileno(stdout), _O_BINARY);
    }

    auto spawn_impl(spawn_request_t const& request)
        -> std::expected<spawn_result_t, error_t> {
        auto security = SECURITY_ATTRIBUTES{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;
        auto in_read = HANDLE{};
        auto in_write = HANDLE{};
        auto out_read = HANDLE{};
        auto out_write = HANDLE{};
        auto err_read = HANDLE{};
        auto err_write = HANDLE{};
        if (!CreatePipe(&in_read, &in_write, &security, 0) ||
            !CreatePipe(&out_read, &out_write, &security, 0) ||
            !CreatePipe(&err_read, &err_write, &security, 0)) {
            return std::unexpected{error_t{"CreatePipe failed"}};
        }
        SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);
        auto startup = STARTUPINFOW{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = in_read;
        startup.hStdOutput = out_write;
        startup.hStdError = err_write;
        auto process = PROCESS_INFORMATION{};
        auto line = build_command_line(request.argv);
        auto const created = CreateProcessW(
            nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &process);
        CloseHandle(in_read);
        CloseHandle(out_write);
        CloseHandle(err_write);
        if (!created) {
            CloseHandle(in_write);
            CloseHandle(out_read);
            CloseHandle(err_read);
            return std::unexpected{
                error_t{"CreateProcess failed: " + request.argv.front()}};
        }
        if (request.stdin_data.empty()) {
            CloseHandle(in_write);
        }
        auto writer = std::jthread{[&] {
            if (request.stdin_data.empty()) {
                return;
            }
            auto offset = std::size_t{0};
            while (offset < request.stdin_data.size()) {
                auto written = DWORD{};
                if (!WriteFile(
                        in_write, request.stdin_data.data() + offset,
                        static_cast<DWORD>(request.stdin_data.size() - offset),
                        &written, nullptr)) {
                    break;
                }
                offset += written;
            }
            CloseHandle(in_write);
        }};
        auto stdout_text = std::string{};
        auto stderr_text = std::string{};
        auto const read_all = [](HANDLE handle) -> std::string {
            auto text = std::string{};
            char buffer[4096];
            auto count = DWORD{};
            while (ReadFile(handle, buffer, sizeof(buffer), &count, nullptr) &&
                   count > 0) {
                text.append(buffer, count);
            }
            return text;
        };
        auto out_reader =
            std::jthread{[&] { stdout_text = read_all(out_read); }};
        auto err_reader =
            std::jthread{[&] { stderr_text = read_all(err_read); }};
        auto const wait = WaitForSingleObject(
            process.hProcess, static_cast<DWORD>(request.timeout_ms));
        auto is_timed_out = false;
        if (wait == WAIT_TIMEOUT) {
            is_timed_out = true;
            TerminateProcess(process.hProcess, 1);
            WaitForSingleObject(process.hProcess, INFINITE);
        }
        auto exit_code = DWORD{};
        GetExitCodeProcess(process.hProcess, &exit_code);
        out_reader.join();
        err_reader.join();
        writer.join();
        CloseHandle(out_read);
        CloseHandle(err_read);
        CloseHandle(process.hProcess);
        CloseHandle(process.hThread);
        return spawn_result_t{.stdout_text = std::move(stdout_text),
                              .stderr_text = std::move(stderr_text),
                              .exit_code = static_cast<int>(exit_code),
                              .is_timed_out = is_timed_out};
    }

    using stream_id_t = std::shared_ptr<stream_state_t>;

    auto stream_spawn_impl(std::vector<std::string> const& argv)
        -> std::expected<stream_id_t, error_t> {
        auto security = SECURITY_ATTRIBUTES{};
        security.nLength = sizeof(security);
        security.bInheritHandle = TRUE;
        auto in_read = HANDLE{};
        auto in_write = HANDLE{};
        auto out_read = HANDLE{};
        auto out_write = HANDLE{};
        auto err_read = HANDLE{};
        auto err_write = HANDLE{};
        if (!CreatePipe(&in_read, &in_write, &security, 0) ||
            !CreatePipe(&out_read, &out_write, &security, 0) ||
            !CreatePipe(&err_read, &err_write, &security, 0)) {
            return std::unexpected{error_t{"CreatePipe failed"}};
        }
        SetHandleInformation(in_write, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_read, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(err_read, HANDLE_FLAG_INHERIT, 0);
        auto startup = STARTUPINFOW{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = in_read;
        startup.hStdOutput = out_write;
        startup.hStdError = err_write;
        auto process = PROCESS_INFORMATION{};
        auto line = build_command_line(argv);
        auto const created = CreateProcessW(
            nullptr, line.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW,
            nullptr, nullptr, &startup, &process);
        CloseHandle(in_read);
        CloseHandle(out_write);
        CloseHandle(err_write);
        if (!created) {
            CloseHandle(in_write);
            CloseHandle(out_read);
            CloseHandle(err_read);
            return std::unexpected{
                error_t{"CreateProcess failed: " + argv.front()}};
        }
        auto state = std::make_shared<stream_state_t>();
        state->process = process.hProcess;
        state->thread = process.hThread;
        state->stdin_write = in_write;
        auto const pump = [](HANDLE handle,
                             stream_state_t::pipe_buffer_t& buffer) {
            char local[4096];
            auto count = DWORD{};
            while (ReadFile(handle, local, sizeof(local), &count, nullptr) &&
                   count > 0) {
                auto lock = std::lock_guard{buffer.mutex};
                buffer.data.append(local, count);
                buffer.ready.notify_all();
            }
            {
                auto lock = std::lock_guard{buffer.mutex};
                buffer.is_closed = true;
                buffer.ready.notify_all();
            }
            CloseHandle(handle);
        };
        state->out_reader = std::jthread{[state = state.get(), out_read, pump] {
            pump(out_read, state->out);
        }};
        state->err_reader = std::jthread{[state = state.get(), err_read, pump] {
            pump(err_read, state->err);
        }};
        return state;
    }

    auto stream_write_impl(stream_id_t const& id, std::string_view data)
        -> bool {
        auto offset = std::size_t{0};
        while (offset < data.size()) {
            auto written = DWORD{};
            if (!WriteFile(id->stdin_write, data.data() + offset,
                           static_cast<DWORD>(data.size() - offset), &written,
                           nullptr)) {
                return false;
            }
            offset += written;
        }
        return true;
    }

    auto stream_read_impl(stream_id_t const& id, stream_t which,
                          std::chrono::steady_clock::time_point deadline)
        -> std::expected<chunk_t, error_t> {
        auto& buffer = which == stream_t::STDOUT ? id->out : id->err;
        auto lock = std::unique_lock{buffer.mutex};
        buffer.ready.wait_until(lock, deadline, [&buffer] {
            return !buffer.data.empty() || buffer.is_closed;
        });
        auto chunk = chunk_t{.data = std::move(buffer.data),
                             .is_closed = buffer.is_closed};
        buffer.data.clear();
        return chunk;
    }

    auto stream_kill_impl(stream_id_t const& id) -> void {
        if (id->is_killed) {
            return;
        }
        id->is_killed = true;
        TerminateProcess(id->process, 1);
        if (id->stdin_write != nullptr) {
            CloseHandle(id->stdin_write);
            id->stdin_write = nullptr;
        }
    }
};

} // namespace sshmcp
