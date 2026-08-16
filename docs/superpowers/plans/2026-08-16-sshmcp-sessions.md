# sshmcp v0.2 Sessions + Binary Files Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Named persistent shell sessions routed through `exec`
(sentinel-framed, no PTY) plus base64 binary-safe file tools.

**Architecture:** Streaming surface added to the CRTP platform
layer; pure sentinel protocol helpers + `session_manager_t`
templated on the platform; tools consume injectable
`session_exec`/`session_close` functions on `context_t`; base64
codec local-only (ssh pipes are 8-bit clean — remote stays stock).

**Tech Stack:** unchanged from v0.1 (C++23 header-only, presets,
own harness, fake-ssh e2e, linters).

**Spec:** `docs/superpowers/specs/2026-08-16-sshmcp-sessions-design.md`

## Global Constraints

- All v0.1 policies (no macros, no exceptions, no `using`,
  east const/AAA/trailing return, 80 col, zero-condition
  CMakeLists, TDD, conventional commits).
- v0.1 one-shot outputs byte-frozen: session-less `exec` result
  JSON must not change; existing e2e goldens stay green except
  `tools/list` (schemas grow — re-bless with inspection).
- New constants: `MAX_BINARY_BYTES = 8'388'608`,
  `MAX_SESSIONS = 4`, `DEFAULT_SESSION_IDLE_MS = 1'800'000`.
- Sentinel format exactly: stdout `\n@sshmcp:<n>:<exit>@\n`,
  stderr `\n@sshmcp:<n>@\n`, `<n>` process-wide counter.
- Local dev gate: `ctest --preset windows-msvc-debug`.

---

### Task 1: base64 codec

**Files:**
- Create: `include/sshmcp/base64.hpp`,
  `regress/unit/test_base64.cpp`
- Modify: `CMakeLists.txt` (add test source to `sshmcp_unit`)

**Interfaces:**
- Produces: `sshmcp::base64_encode(std::string_view) ->
  std::string`; `sshmcp::base64_decode(std::string_view) ->
  std::expected<std::string, error_t>` (strict alphabet, padding
  required, rejects embedded whitespace).

- [ ] **Step 1: Failing tests**

`regress/unit/test_base64.cpp`:
```cpp
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
    t::expect(sshmcp::base64_encode("foobar") == "Zm9vYmFy",
              "rfc vector");
    auto const raw = std::string{"\x00\xff\x10", 3};
    t::expect(sshmcp::base64_encode(raw) == "AP8Q",
              "binary bytes");
});

[[maybe_unused]] auto const t2 = t::add_test("b64 decode", [] {
    t::expect(sshmcp::base64_decode("Zm9vYmFy").value() ==
                  "foobar",
              "round trip");
    t::expect(sshmcp::base64_decode("Zg==").value() == "f",
              "padding 2");
    t::expect(sshmcp::base64_decode("Zm8=").value() == "fo",
              "padding 1");
    t::expect(sshmcp::base64_decode("").value() == "", "empty");
    t::expect(!sshmcp::base64_decode("Zg=").has_value(),
              "bad length");
    t::expect(!sshmcp::base64_decode("Z!==").has_value(),
              "bad char");
    t::expect(!sshmcp::base64_decode("Zm9v YmFy").has_value(),
              "whitespace rejected");
    t::expect(!sshmcp::base64_decode("==AA").has_value(),
              "pad in front");
    auto const raw = std::string{"\x00\xff\x10", 3};
    t::expect(sshmcp::base64_decode("AP8Q").value() == raw,
              "binary round trip");
});

}  // namespace
```

Add `regress/unit/test_base64.cpp` to `sshmcp_unit` sources.

- [ ] **Step 2: Verify red** — build fails, `base64.hpp` missing.

- [ ] **Step 3: Implement**

`include/sshmcp/base64.hpp`:
```cpp
#pragma once

#include <sshmcp/types.hpp>

#include <array>
#include <expected>
#include <string>
#include <string_view>

namespace sshmcp {

inline constexpr auto BASE64_ALPHABET = std::string_view{
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz0123456789+/"};

inline auto base64_encode(std::string_view data) -> std::string {
    auto out = std::string{};
    out.reserve((data.size() + 2) / 3 * 4);
    auto index = std::size_t{0};
    while (index + 3 <= data.size()) {
        auto const a = static_cast<unsigned char>(data[index]);
        auto const b =
            static_cast<unsigned char>(data[index + 1]);
        auto const c =
            static_cast<unsigned char>(data[index + 2]);
        out.push_back(BASE64_ALPHABET[a >> 2]);
        out.push_back(
            BASE64_ALPHABET[((a & 0x03) << 4) | (b >> 4)]);
        out.push_back(
            BASE64_ALPHABET[((b & 0x0f) << 2) | (c >> 6)]);
        out.push_back(BASE64_ALPHABET[c & 0x3f]);
        index += 3;
    }
    auto const rest = data.size() - index;
    if (rest == 1) {
        auto const a = static_cast<unsigned char>(data[index]);
        out.push_back(BASE64_ALPHABET[a >> 2]);
        out.push_back(BASE64_ALPHABET[(a & 0x03) << 4]);
        out += "==";
    } else if (rest == 2) {
        auto const a = static_cast<unsigned char>(data[index]);
        auto const b =
            static_cast<unsigned char>(data[index + 1]);
        out.push_back(BASE64_ALPHABET[a >> 2]);
        out.push_back(
            BASE64_ALPHABET[((a & 0x03) << 4) | (b >> 4)]);
        out.push_back(BASE64_ALPHABET[(b & 0x0f) << 2]);
        out.push_back('=');
    }
    return out;
}

inline auto base64_value(char ch) -> int {
    auto const pos = BASE64_ALPHABET.find(ch);
    return pos == std::string_view::npos
               ? -1
               : static_cast<int>(pos);
}

inline auto base64_decode(std::string_view text)
    -> std::expected<std::string, error_t> {
    if (text.size() % 4 != 0) {
        return std::unexpected{
            error_t{"base64: length not multiple of 4"}};
    }
    auto out = std::string{};
    out.reserve(text.size() / 4 * 3);
    for (auto index = std::size_t{0}; index < text.size();
         index += 4) {
        auto const quad = text.substr(index, 4);
        auto const is_last = index + 4 == text.size();
        auto values = std::array<int, 4>{};
        auto pads = 0;
        for (auto slot = std::size_t{0}; slot < 4; ++slot) {
            auto const ch = quad[slot];
            if (ch == '=') {
                if (!is_last || slot < 2 ||
                    (slot == 2 && quad[3] != '=')) {
                    return std::unexpected{
                        error_t{"base64: bad padding"}};
                }
                ++pads;
                values[slot] = 0;
            } else {
                if (pads > 0) {
                    return std::unexpected{
                        error_t{"base64: data after padding"}};
                }
                values[slot] = base64_value(ch);
                if (values[slot] < 0) {
                    return std::unexpected{
                        error_t{"base64: invalid character"}};
                }
            }
        }
        out.push_back(static_cast<char>(
            (values[0] << 2) | (values[1] >> 4)));
        if (pads < 2) {
            out.push_back(static_cast<char>(
                ((values[1] & 0x0f) << 4) | (values[2] >> 2)));
        }
        if (pads < 1) {
            out.push_back(static_cast<char>(
                ((values[2] & 0x03) << 6) | values[3]));
        }
    }
    return out;
}

}  // namespace sshmcp
```

- [ ] **Step 4: Verify green** — full ctest passes.
- [ ] **Step 5: Commit** — `feat: strict base64 codec`

---

### Task 2: session protocol helpers

**Files:**
- Create: `include/sshmcp/session.hpp` (protocol part),
  `regress/unit/test_session_protocol.cpp`
- Modify: `CMakeLists.txt` (test source), `include/sshmcp/types.hpp`
  (new constants + `stream_t`, `chunk_t`, `session_result_t`)

**Interfaces:**
- Produces (types.hpp): `MAX_BINARY_BYTES=8'388'608`,
  `MAX_SESSIONS=4`, `DEFAULT_SESSION_IDLE_MS=1'800'000`;
  `enum class stream_t { OUT, ERR }`;
  `chunk_t{std::string data, bool is_closed}`;
  `session_result_t{spawn_result_t result, bool
  is_session_dead}`; `config_t` gains
  `std::int64_t session_idle_ms{DEFAULT_SESSION_IDLE_MS}`.
- Produces (session.hpp): `session_payload(std::uint64_t counter,
  std::string_view command) -> std::string`;
  `scan_result_t{bool is_found, std::string output, int
  exit_code}`; `scan_stdout(std::string_view buffer,
  std::uint64_t counter) -> scan_result_t`;
  `scan_stderr(std::string_view buffer, std::uint64_t counter)
  -> scan_result_t`.

- [ ] **Step 1: Failing tests**

`regress/unit/test_session_protocol.cpp`:
```cpp
#include "harness.hpp"

#include <sshmcp/session.hpp>

#include <string>

namespace {

namespace t = sshmcp::test;

[[maybe_unused]] auto const t1 = t::add_test("payload", [] {
    auto const payload =
        sshmcp::session_payload(7, "echo hi");
    t::expect(payload ==
                  "{\n"
                  "echo hi\n"
                  "} </dev/null\n"
                  "printf '\\n@sshmcp:7:%s@\\n' \"$?\"\n"
                  "printf '\\n@sshmcp:7@\\n' >&2\n",
              "exact payload");
});

[[maybe_unused]] auto const t2 = t::add_test("scan stdout", [] {
    auto const missing = sshmcp::scan_stdout("partial out", 3);
    t::expect(!missing.is_found, "not yet");

    auto const empty =
        sshmcp::scan_stdout("\n@sshmcp:3:0@\n", 3);
    t::expect(empty.is_found, "empty found");
    t::expect(empty.output == "", "empty output");
    t::expect(empty.exit_code == 0, "exit 0");

    auto const with_output =
        sshmcp::scan_stdout("hi\n\n@sshmcp:3:42@\n", 3);
    t::expect(with_output.is_found, "found");
    t::expect(with_output.output == "hi\n", "keeps newline");
    t::expect(with_output.exit_code == 42, "exit 42");

    auto const no_trailing_nl =
        sshmcp::scan_stdout("abc\n@sshmcp:3:1@\n", 3);
    t::expect(no_trailing_nl.output == "abc",
              "output without own newline");

    auto const partial_marker =
        sshmcp::scan_stdout("x\n@sshmcp:3:4", 3);
    t::expect(!partial_marker.is_found,
              "marker split across chunks");

    auto const wrong_counter =
        sshmcp::scan_stdout("\n@sshmcp:2:0@\n", 3);
    t::expect(!wrong_counter.is_found, "old counter ignored");

    auto const decoy = sshmcp::scan_stdout(
        "log @sshmcp:3:9@ inline\n\n@sshmcp:3:5@\n", 3);
    t::expect(decoy.is_found && decoy.exit_code == 5,
              "marker must start a line");
});

[[maybe_unused]] auto const t3 = t::add_test("scan stderr", [] {
    auto const found =
        sshmcp::scan_stderr("warn\n\n@sshmcp:9@\n", 9);
    t::expect(found.is_found, "found");
    t::expect(found.output == "warn\n", "stderr output");
    t::expect(!sshmcp::scan_stderr("\n@sshmcp:9:0@\n", 9)
                   .is_found,
              "stdout marker not stderr marker");
});

}  // namespace
```

- [ ] **Step 2: Verify red.**

- [ ] **Step 3: types.hpp additions**

Append to constants:
```cpp
inline constexpr auto MAX_BINARY_BYTES =
    std::size_t{8'388'608};
inline constexpr auto MAX_SESSIONS = std::size_t{4};
inline constexpr auto DEFAULT_SESSION_IDLE_MS =
    std::int64_t{1'800'000};
```
After `spawn_result_t`:
```cpp
enum class stream_t { OUT, ERR };

struct chunk_t {
    std::string data;
    bool is_closed{};
};

struct session_result_t {
    spawn_result_t result;
    bool is_session_dead{};
};
```
`config_t` gains member
`std::int64_t session_idle_ms{DEFAULT_SESSION_IDLE_MS};`.

- [ ] **Step 4: session.hpp protocol part**

```cpp
#pragma once

#include <sshmcp/types.hpp>

#include <charconv>
#include <cstdint>
#include <format>
#include <string>
#include <string_view>

namespace sshmcp {

inline auto session_payload(std::uint64_t counter,
                            std::string_view command)
    -> std::string {
    return std::format("{{\n"
                       "{}\n"
                       "}} </dev/null\n"
                       "printf '\\n@sshmcp:{}:%s@\\n' \"$?\"\n"
                       "printf '\\n@sshmcp:{}@\\n' >&2\n",
                       command, counter, counter);
}

struct scan_result_t {
    bool is_found{};
    std::string output;
    int exit_code{};
};

// Marker must begin a line: either at buffer start or after \n.
inline auto find_marker(std::string_view buffer,
                        std::string_view marker)
    -> std::size_t {
    auto from = std::size_t{0};
    while (true) {
        auto const pos = buffer.find(marker, from);
        if (pos == std::string_view::npos) {
            return std::string_view::npos;
        }
        if (pos == 0 || buffer[pos - 1] == '\n') {
            return pos;
        }
        from = pos + 1;
    }
}

inline auto strip_injected_newline(std::string_view buffer,
                                   std::size_t marker_pos)
    -> std::string {
    // The payload printf always injects one leading \n which is
    // the char just before the marker line; drop exactly it.
    auto output = buffer.substr(0, marker_pos);
    if (!output.empty() && output.back() == '\n') {
        output.remove_suffix(1);
    }
    return std::string{output};
}

inline auto scan_stdout(std::string_view buffer,
                        std::uint64_t counter)
    -> scan_result_t {
    auto const head =
        std::format("@sshmcp:{}:", counter);
    auto const pos = find_marker(buffer, head);
    if (pos == std::string_view::npos) {
        return {};
    }
    auto const digits_at = pos + head.size();
    auto exit_code = 0;
    auto const parsed = std::from_chars(
        buffer.data() + digits_at,
        buffer.data() + buffer.size(), exit_code);
    auto const tail =
        std::string_view{parsed.ptr,
                         buffer.data() + buffer.size()};
    if (parsed.ec != std::errc{} || !tail.starts_with("@\n")) {
        return {};
    }
    return {.is_found = true,
            .output = strip_injected_newline(buffer, pos),
            .exit_code = exit_code};
}

inline auto scan_stderr(std::string_view buffer,
                        std::uint64_t counter)
    -> scan_result_t {
    auto const marker =
        std::format("@sshmcp:{}@\n", counter);
    auto const pos = find_marker(buffer, marker);
    if (pos == std::string_view::npos) {
        return {};
    }
    return {.is_found = true,
            .output = strip_injected_newline(buffer, pos),
            .exit_code = 0};
}

}  // namespace sshmcp
```

Note: `strip_injected_newline` also swallows the newline in the
"output without own newline" case (`abc\n@marker`) — there the
`\n` before the marker IS the injected one, so behavior is
correct for both fixtures.

- [ ] **Step 5: Verify green.**
- [ ] **Step 6: Commit** — `feat: session sentinel protocol`

---

### Task 3: streaming platform surface

**Files:**
- Modify: `include/sshmcp/subprocess_base.hpp` (public wrappers),
  `include/sshmcp/platform/windows/impl.hpp`,
  `include/sshmcp/platform/posix_common.hpp`,
  `include/sshmcp/platform/linux/impl.hpp`,
  `include/sshmcp/platform/openbsd/impl.hpp`,
  `regress/unit/platform/windows/test_impl.cpp`,
  `regress/unit/platform/linux/test_impl.cpp`,
  `regress/unit/platform/openbsd/test_impl.cpp`

**Interfaces:**
- Produces on every `platform_impl_t` (via CRTP base wrappers):
  - `using stream_id_t = std::shared_ptr<stream_state_t>;`
  - `stream_spawn(std::vector<std::string> const& argv) ->
    std::expected<stream_id_t, error_t>`
  - `stream_write(stream_id_t const&, std::string_view) -> bool`
  - `stream_read(stream_id_t const&, stream_t,
    std::chrono::steady_clock::time_point deadline) ->
    std::expected<chunk_t, error_t>` — blocks until data, close,
    or deadline; empty `data` + `is_closed==false` = deadline
    slice.
  - `stream_kill(stream_id_t const&) -> void` (idempotent).

- [ ] **Step 1: Failing platform tests**

Append to `regress/unit/platform/windows/test_impl.cpp` (inside
the anonymous namespace) — the echo child is
`python -u -c "import sys;[print('E:'+l,end='',flush=True) for l in sys.stdin]"`:
```cpp
auto python_argv(std::string code)
    -> std::vector<std::string> {
    return {"python", "-u", "-c", std::move(code)};
}

[[maybe_unused]] auto const s1 = t::add_test("stream echo", [] {
    auto impl =
        sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const spawned = impl.stream_spawn(python_argv(
        "import sys;[print('E:'+l,end='',flush=True) "
        "for l in sys.stdin]"));
    t::expect(spawned.has_value(), "spawn ok");
    auto const id = *spawned;
    t::expect(impl.stream_write(id, "hello\n"), "write ok");
    auto text = std::string{};
    auto const deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds{20};
    while (!text.contains("E:hello\n")) {
        auto const chunk = impl.stream_read(
            id, sshmcp::stream_t::OUT, deadline);
        t::expect(chunk.has_value(), "read ok");
        if (!chunk.has_value() ||
            (chunk->data.empty() && chunk->is_closed)) {
            break;
        }
        text += chunk->data;
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
    }
    t::expect(text == "E:hello\n", "echo round trip");
    impl.stream_kill(id);
    impl.stream_kill(id);  // idempotent
});

[[maybe_unused]] auto const s2 = t::add_test("stream close", [] {
    auto impl =
        sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const spawned =
        impl.stream_spawn(python_argv("print('bye')"));
    t::expect(spawned.has_value(), "spawn ok");
    auto const deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds{20};
    auto text = std::string{};
    auto closed = false;
    while (!closed) {
        auto const chunk = (*spawned)->use_count() &&
                           false
                               ? sshmcp::chunk_t{}
                               : *impl.stream_read(
                                     *spawned,
                                     sshmcp::stream_t::OUT,
                                     deadline);
        text += chunk.data;
        closed = chunk.is_closed;
        if (std::chrono::steady_clock::now() >= deadline) {
            break;
        }
    }
    t::expect(text.contains("bye"), "output before close");
    t::expect(closed, "close observed");
    impl.stream_kill(*spawned);
});

[[maybe_unused]] auto const s3 = t::add_test("stream slice", [] {
    auto impl =
        sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const spawned = impl.stream_spawn(python_argv(
        "import time;time.sleep(30)"));
    t::expect(spawned.has_value(), "spawn ok");
    auto const chunk = impl.stream_read(
        *spawned, sshmcp::stream_t::OUT,
        std::chrono::steady_clock::now() +
            std::chrono::milliseconds{300});
    t::expect(chunk.has_value(), "read ok");
    t::expect(chunk->data.empty() && !chunk->is_closed,
              "deadline slice is empty and open");
    impl.stream_kill(*spawned);
});
```

The `s2` ternary noise above is a plan artifact — write it
plainly in the real file:
```cpp
auto const chunk = impl.stream_read(
    *spawned, sshmcp::stream_t::OUT, deadline);
t::expect(chunk.has_value(), "read ok");
text += chunk->data;
closed = chunk->is_closed;
```

POSIX test files get the same three cases with
`python_argv` starting `{"python3", "-u", "-c", ...}` and a
fallback: if `stream_spawn` fails with python3, retry with
`python` (OpenBSD installs `python3`; ubuntu CI has `python3`).

- [ ] **Step 2: Verify red** — `stream_spawn` unknown member.

- [ ] **Step 3: CRTP base wrappers**

Append to `subprocess_base_t` public section:
```cpp
auto stream_spawn(std::vector<std::string> const& argv) {
    if (log_level == log_level_t::DEBUG) {
        std::println(stderr, "sshmcp: stream spawn: {}",
                     argv.empty() ? "" : argv.front());
    }
    return static_cast<Derived*>(this)->stream_spawn_impl(argv);
}

template<typename StreamId>
auto stream_write(StreamId const& id, std::string_view data)
    -> bool {
    return static_cast<Derived*>(this)->stream_write_impl(id,
                                                          data);
}

template<typename StreamId>
auto stream_read(StreamId const& id, stream_t which,
                 std::chrono::steady_clock::time_point deadline)
    -> std::expected<chunk_t, error_t> {
    return static_cast<Derived*>(this)->stream_read_impl(
        id, which, deadline);
}

template<typename StreamId>
auto stream_kill(StreamId const& id) -> void {
    static_cast<Derived*>(this)->stream_kill_impl(id);
}
```

- [ ] **Step 4: Windows streaming impl**

Add to `include/sshmcp/platform/windows/impl.hpp` (includes gain
`<condition_variable>`, `<memory>`, `<mutex>`, `<chrono>`):
```cpp
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
```
Inside `platform_impl_t`:
```cpp
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
        nullptr, line.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup,
        &process);
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
    auto const pump =
        [](HANDLE handle,
           stream_state_t::pipe_buffer_t& buffer) {
            char local[4096];
            auto count = DWORD{};
            while (ReadFile(handle, local, sizeof(local),
                            &count, nullptr) &&
                   count > 0) {
                auto lock =
                    std::lock_guard{buffer.mutex};
                buffer.data.append(local, count);
                buffer.ready.notify_all();
            }
            {
                auto lock =
                    std::lock_guard{buffer.mutex};
                buffer.is_closed = true;
                buffer.ready.notify_all();
            }
            CloseHandle(handle);
        };
    state->out_reader = std::jthread{
        [state = state.get(), out_read, pump] {
            pump(out_read, state->out);
        }};
    state->err_reader = std::jthread{
        [state = state.get(), err_read, pump] {
            pump(err_read, state->err);
        }};
    return state;
}

auto stream_write_impl(stream_id_t const& id,
                       std::string_view data) -> bool {
    auto offset = std::size_t{0};
    while (offset < data.size()) {
        auto written = DWORD{};
        if (!WriteFile(id->stdin_write, data.data() + offset,
                       static_cast<DWORD>(data.size() - offset),
                       &written, nullptr)) {
            return false;
        }
        offset += written;
    }
    return true;
}

auto stream_read_impl(
    stream_id_t const& id, stream_t which,
    std::chrono::steady_clock::time_point deadline)
    -> std::expected<chunk_t, error_t> {
    auto& buffer =
        which == stream_t::OUT ? id->out : id->err;
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
```
Note the destructor makes cleanup safe even without an explicit
kill; `is_killed` keeps kill idempotent, and the destructor's
`TerminateProcess` on an already-dead process is a harmless
no-op.

- [ ] **Step 5: POSIX streaming impl**

Append to `posix_common.hpp`:
```cpp
struct posix_stream_state_t {
    pid_t pid{-1};
    int stdin_fd{-1};
    int stdout_fd{-1};
    int stderr_fd{-1};
    bool is_killed{};

    ~posix_stream_state_t() {
        if (pid > 0 && !is_killed) {
            kill(pid, SIGKILL);
        }
        if (pid > 0) {
            auto status = 0;
            waitpid(pid, &status, 0);
        }
        for (auto const fd :
             {stdin_fd, stdout_fd, stderr_fd}) {
            if (fd >= 0) {
                close(fd);
            }
        }
    }
};

inline auto posix_stream_spawn(
    std::vector<std::string> const& argv)
    -> std::expected<std::shared_ptr<posix_stream_state_t>,
                     error_t> {
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
        argv_c.reserve(argv.size() + 1);
        for (auto const& arg : argv) {
            argv_c.push_back(const_cast<char*>(arg.c_str()));
        }
        argv_c.push_back(nullptr);
        execvp(argv_c[0], argv_c.data());
        _exit(127);
    }
    close(in_pipe[0]);
    close(out_pipe[1]);
    close(err_pipe[1]);
    fcntl(out_pipe[0], F_SETFL, O_NONBLOCK);
    fcntl(err_pipe[0], F_SETFL, O_NONBLOCK);
    auto state = std::make_shared<posix_stream_state_t>();
    state->pid = pid;
    state->stdin_fd = in_pipe[1];
    state->stdout_fd = out_pipe[0];
    state->stderr_fd = err_pipe[0];
    return state;
}

inline auto posix_stream_read(
    std::shared_ptr<posix_stream_state_t> const& id, int fd,
    std::chrono::steady_clock::time_point deadline)
    -> std::expected<chunk_t, error_t> {
    auto chunk = chunk_t{};
    if (fd < 0) {
        chunk.is_closed = true;
        return chunk;
    }
    while (true) {
        char local[4096];
        auto const count = read(fd, local, sizeof(local));
        if (count > 0) {
            chunk.data.append(local,
                              static_cast<std::size_t>(count));
            continue;  // drain what's available
        }
        if (count == 0) {
            chunk.is_closed = true;
            return chunk;
        }
        if (!chunk.data.empty()) {
            return chunk;  // got something this call
        }
        auto const now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return chunk;  // empty slice
        }
        auto const wait_ms =
            std::chrono::duration_cast<
                std::chrono::milliseconds>(deadline - now)
                .count();
        pollfd fds[1] = {{fd, POLLIN, 0}};
        if (poll(fds, 1, static_cast<int>(wait_ms)) < 0) {
            return std::unexpected{error_t{"poll() failed"}};
        }
        if ((fds[0].revents & (POLLIN | POLLHUP)) == 0) {
            return chunk;  // deadline
        }
    }
}
```
`linux/impl.hpp` and `openbsd/impl.hpp` both gain, inside
`platform_impl_t`:
```cpp
using stream_id_t = std::shared_ptr<posix_stream_state_t>;

auto stream_spawn_impl(std::vector<std::string> const& argv)
    -> std::expected<stream_id_t, error_t> {
    return posix_stream_spawn(argv);
}

auto stream_write_impl(stream_id_t const& id,
                       std::string_view data) -> bool {
    auto offset = std::size_t{0};
    while (offset < data.size()) {
        auto const count =
            write(id->stdin_fd, data.data() + offset,
                  data.size() - offset);
        if (count <= 0) {
            return false;
        }
        offset += static_cast<std::size_t>(count);
    }
    return true;
}

auto stream_read_impl(
    stream_id_t const& id, stream_t which,
    std::chrono::steady_clock::time_point deadline)
    -> std::expected<chunk_t, error_t> {
    return posix_stream_read(id,
                             which == stream_t::OUT
                                 ? id->stdout_fd
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
```
(after `stream_kill_impl`'s waitpid, the destructor's waitpid
would hang on a reaped pid — guard: destructor only waits when
`!is_killed`; adjust the destructor to skip both kill and wait
when `is_killed` is true.)

- [ ] **Step 6: Verify green locally** (windows cases).
- [ ] **Step 7: Commit** — `feat: streaming subprocess surface`

---

### Task 4: session manager

**Files:**
- Modify: `include/sshmcp/session.hpp` (append manager)
- Create: `regress/unit/test_session_manager.cpp`
- Modify: `CMakeLists.txt` (test source)

**Interfaces:**
- Consumes: protocol helpers (Task 2), streaming surface
  (Task 3), `build_ssh_argv` (v0.1).
- Produces:
```cpp
template<typename PlatformType>
class session_manager_t {
public:
    session_manager_t(PlatformType& platform, config_t config);
    auto exec(std::string_view name, std::string_view command,
              std::int64_t timeout_ms)
        -> std::expected<session_result_t, error_t>;
    auto close(std::string_view name) -> bool;
};
```
  Mock platforms need: `stream_id_t` type + `stream_spawn`,
  `stream_write`, `stream_read`, `stream_kill` (the public
  wrapper names — the manager calls wrappers, so mocks implement
  them directly).

- [ ] **Step 1: Failing tests with scripted mock**

`regress/unit/test_session_manager.cpp`:
```cpp
#include "harness.hpp"

#include <sshmcp/session.hpp>

#include <chrono>
#include <deque>
#include <expected>
#include <format>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace t = sshmcp::test;

// Scripted platform: every exec produces one stdout reply and
// one stderr reply, framed with the right counter, split into
// awkward chunks.
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
    bool hang{};  // never emit sentinels
    int spawn_count{};
    stream_id_t last;

    auto stream_spawn(std::vector<std::string> const&)
        -> std::expected<stream_id_t, sshmcp::error_t> {
        if (fail_spawn) {
            return std::unexpected{
                sshmcp::error_t{"spawn refused"}};
        }
        ++spawn_count;
        last = std::make_shared<state_t>();
        return last;
    }

    auto stream_write(stream_id_t const& id,
                      std::string_view data) -> bool {
        id->written.emplace_back(data);
        if (hang) {
            return true;
        }
        // Find the counter in the payload's printf line.
        auto const text = std::string{data};
        auto const tag = text.find("@sshmcp:");
        auto const colon = text.find(':', tag + 8);
        auto const counter =
            text.substr(tag + 8, colon - tag - 8);
        auto const out = next_stdout +
                         std::format("\n@sshmcp:{}:{}@\n",
                                     counter, next_exit);
        // Adversarial split: byte-by-byte for the marker head.
        id->out_chunks.push_back(
            out.substr(0, out.size() / 2));
        id->out_chunks.push_back(out.substr(out.size() / 2));
        id->err_chunks.push_back(
            std::format("\n@sshmcp:{}@\n", counter));
        return true;
    }

    auto stream_read(
        stream_id_t const& id, sshmcp::stream_t which,
        std::chrono::steady_clock::time_point deadline)
        -> std::expected<sshmcp::chunk_t, sshmcp::error_t> {
        auto& queue = which == sshmcp::stream_t::OUT
                          ? id->out_chunks
                          : id->err_chunks;
        if (queue.empty()) {
            if (hang &&
                std::chrono::steady_clock::now() < deadline) {
                // pretend we waited out the slice
            }
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
    auto manager = sshmcp::session_manager_t<mock_platform_t>{
        platform, config_of()};
    platform.next_stdout = "hi\n";
    platform.next_exit = 3;
    auto const result = manager.exec("dev", "echo hi", 5'000);
    t::expect(result.has_value(), "exec ok");
    t::expect(result->result.stdout_text == "hi\n", "stdout");
    t::expect(result->result.exit_code == 3, "exit");
    t::expect(!result->is_session_dead, "alive");
    t::expect(platform.spawn_count == 1, "one spawn");

    manager.exec("dev", "echo again", 5'000);
    t::expect(platform.spawn_count == 1,
              "session reused, no respawn");
    t::expect(platform.last->written.size() == 2,
              "two payloads written");
    t::expect(platform.last->written[1].contains(
                  "echo again"),
              "command in payload");
});

[[maybe_unused]] auto const t2 = t::add_test("session close", [] {
    auto platform = mock_platform_t{};
    auto manager = sshmcp::session_manager_t<mock_platform_t>{
        platform, config_of()};
    manager.exec("dev", "true", 5'000);
    t::expect(manager.close("dev"), "close known");
    t::expect(platform.last->is_killed, "killed");
    t::expect(!manager.close("dev"), "close unknown");
    manager.exec("dev", "true", 5'000);
    t::expect(platform.spawn_count == 2,
              "recreated after close");
});

[[maybe_unused]] auto const t3 = t::add_test("session cap", [] {
    auto platform = mock_platform_t{};
    auto manager = sshmcp::session_manager_t<mock_platform_t>{
        platform, config_of()};
    manager.exec("a", "true", 5'000);
    manager.exec("b", "true", 5'000);
    manager.exec("c", "true", 5'000);
    manager.exec("d", "true", 5'000);
    auto const fifth = manager.exec("e", "true", 5'000);
    t::expect(!fifth.has_value(), "cap enforced");
    t::expect(fifth.error().message.contains("a"),
              "error names live sessions");
});

[[maybe_unused]] auto const t4 = t::add_test("session timeout", [] {
    auto platform = mock_platform_t{};
    platform.hang = true;
    auto manager = sshmcp::session_manager_t<mock_platform_t>{
        platform, config_of()};
    auto const result = manager.exec("dev", "sleep 99", 200);
    t::expect(result.has_value(), "timeout is a result");
    t::expect(result->result.is_timed_out, "flagged");
    t::expect(result->result.exit_code == -1, "exit -1");
    t::expect(result->is_session_dead, "session dead");
    t::expect(platform.last->is_killed, "killed");
    manager.exec("dev", "true", 5'000);
    t::expect(platform.spawn_count == 2,
              "dead session recreated");
});

[[maybe_unused]] auto const t5 = t::add_test("spawn failure", [] {
    auto platform = mock_platform_t{};
    platform.fail_spawn = true;
    auto manager = sshmcp::session_manager_t<mock_platform_t>{
        platform, config_of()};
    auto const result = manager.exec("dev", "true", 5'000);
    t::expect(!result.has_value(), "spawn error surfaces");
});

}  // namespace
```

- [ ] **Step 2: Verify red.**

- [ ] **Step 3: Implement manager (append to session.hpp)**

Includes gain `<chrono>`, `<map>`, `<memory>`, `<vector>`,
`<sshmcp/ssh.hpp>`.
```cpp
template<typename PlatformType>
class session_manager_t {
public:
    session_manager_t(PlatformType& platform, config_t config)
        : platform{platform}, config{std::move(config)} {}

    auto exec(std::string_view name, std::string_view command,
              std::int64_t timeout_ms)
        -> std::expected<session_result_t, error_t> {
        reap();
        auto found = sessions.find(name);
        if (found == sessions.end()) {
            if (sessions.size() >= MAX_SESSIONS) {
                auto message = std::string{
                    "session limit reached; live:"};
                for (auto const& [live, entry] : sessions) {
                    message += ' ';
                    message += live;
                }
                return std::unexpected{
                    error_t{std::move(message)}};
            }
            auto spawned = platform.stream_spawn(
                build_ssh_argv(config, "sh"));
            if (!spawned) {
                return std::unexpected{spawned.error()};
            }
            found = sessions
                        .emplace(std::string{name},
                                 entry_t{.id = *spawned})
                        .first;
        }
        auto& entry = found->second;
        entry.last_used = std::chrono::steady_clock::now();
        auto const call = ++counter;
        if (!platform.stream_write(
                entry.id, session_payload(call, command))) {
            return kill_and_report(found, {}, {}, false);
        }
        auto const deadline =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds{timeout_ms};
        auto out_buffer = std::string{};
        auto err_buffer = std::string{};
        auto out_scan = scan_result_t{};
        auto err_scan = scan_result_t{};
        while (!out_scan.is_found || !err_scan.is_found) {
            auto const now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return kill_and_report(found, out_buffer,
                                       err_buffer, true);
            }
            auto const slice =
                std::min(deadline,
                         now + std::chrono::milliseconds{50});
            auto const which = !out_scan.is_found
                                   ? stream_t::OUT
                                   : stream_t::ERR;
            auto const chunk =
                platform.stream_read(entry.id, which, slice);
            if (!chunk) {
                return kill_and_report(found, out_buffer,
                                       err_buffer, false);
            }
            if (which == stream_t::OUT) {
                out_buffer += chunk->data;
                out_scan = scan_stdout(out_buffer, call);
            } else {
                err_buffer += chunk->data;
                err_scan = scan_stderr(err_buffer, call);
            }
            if (chunk->is_closed &&
                (!out_scan.is_found || !err_scan.is_found)) {
                return kill_and_report(found, out_buffer,
                                       err_buffer, false);
            }
        }
        entry.last_used = std::chrono::steady_clock::now();
        return session_result_t{
            .result =
                spawn_result_t{
                    .stdout_text = out_scan.output,
                    .stderr_text = err_scan.output,
                    .exit_code = out_scan.exit_code,
                    .is_timed_out = false},
            .is_session_dead = false};
    }

    auto close(std::string_view name) -> bool {
        reap();
        auto const found = sessions.find(name);
        if (found == sessions.end()) {
            return false;
        }
        platform.stream_kill(found->second.id);
        sessions.erase(found);
        return true;
    }

private:
    struct entry_t {
        typename PlatformType::stream_id_t id;
        std::chrono::steady_clock::time_point last_used{
            std::chrono::steady_clock::now()};
    };

    auto reap() -> void {
        auto const now = std::chrono::steady_clock::now();
        auto const idle =
            std::chrono::milliseconds{config.session_idle_ms};
        for (auto it = sessions.begin();
             it != sessions.end();) {
            if (now - it->second.last_used > idle) {
                platform.stream_kill(it->second.id);
                it = sessions.erase(it);
            } else {
                ++it;
            }
        }
    }

    auto kill_and_report(
        typename std::map<std::string, entry_t,
                          std::less<>>::iterator it,
        std::string out_buffer, std::string err_buffer,
        bool is_timed_out)
        -> std::expected<session_result_t, error_t> {
        platform.stream_kill(it->second.id);
        sessions.erase(it);
        return session_result_t{
            .result =
                spawn_result_t{
                    .stdout_text = std::move(out_buffer),
                    .stderr_text = std::move(err_buffer),
                    .exit_code = -1,
                    .is_timed_out = is_timed_out},
            .is_session_dead = true};
    }

    PlatformType& platform;
    config_t config;
    std::map<std::string, entry_t, std::less<>> sessions;
    std::uint64_t counter{};
};
```
Note the mock's `stream_read` returning empty chunks forever plus
`hang` relies on the manager's deadline check — the mock never
sleeps, so the timeout test spins until the 200 ms deadline
passes (fast enough for a unit test).

- [ ] **Step 4: Verify green.**
- [ ] **Step 5: Commit** — `feat: session manager with reap and cap`

---

### Task 5: exec session routing + session_close tool

**Files:**
- Modify: `include/sshmcp/types.hpp` (context fns),
  `include/sshmcp/tools.hpp` (exec routing + new tool),
  `regress/unit/test_tools.cpp`, `regress/unit/test_registry.cpp`,
  `regress/unit/test_mcp.cpp` (4th tool in lists), `app/main.cpp`
  (wire manager), `CMakeLists.txt` (nothing new)

**Interfaces:**
- Produces (types.hpp):
```cpp
using session_exec_fn_t = std::function<std::expected<
    session_result_t, error_t>(
    std::string_view, std::string_view, std::int64_t)>;
using session_close_fn_t =
    std::function<bool(std::string_view)>;
// context_t gains (defaulted, so v0.1-style construction
// still compiles):
session_exec_fn_t session_exec;
session_close_fn_t session_close;
```
- Produces (tools.hpp): `session_close_tool_t` (NAME
  `"session_close"`, schema requires `session`); `exec_tool_t`
  schema gains optional `session` string; when present the result
  JSON gains `"session"` and `"is_session_dead"` keys and routes
  through `context.session_exec`.
- `app/main.cpp` registers
  `server_t<exec_tool_t, read_file_tool_t, write_file_tool_t,
  session_close_tool_t>` and wires a
  `session_manager_t<platform_impl_t>`.

- [ ] **Step 1: Failing tests**

Append to `regress/unit/test_tools.cpp`:
```cpp
[[maybe_unused]] auto const t6 = t::add_test("exec session", [] {
    auto seen_name = std::string{};
    auto seen_command = std::string{};
    auto context = sshmcp::context_t{
        .config = {},
        .spawn = {},
        .session_exec =
            [&](std::string_view name,
                std::string_view command, std::int64_t)
            -> std::expected<sshmcp::session_result_t,
                             sshmcp::error_t> {
            seen_name = name;
            seen_command = command;
            return sshmcp::session_result_t{
                .result = {.stdout_text = "in session\n",
                           .exit_code = 0},
                .is_session_dead = false};
        },
        .session_close = {}};
    auto const result = sshmcp::exec_tool_t::invoke(
        context, {{"command", "make"},
                  {"cwd", "/src"},
                  {"session", "dev"}});
    t::expect(!result.is_error, "ok");
    t::expect(seen_name == "dev", "session name");
    t::expect(seen_command == "cd '/src' && make",
              "cwd folded into command");
    auto const body = nlohmann::json::parse(result.text);
    t::expect(body["session"] == "dev", "session echoed");
    t::expect(body["is_session_dead"] == false, "alive");
    t::expect(body["stdout"] == "in session\n", "stdout");

    auto missing = sshmcp::context_t{.config = {}, .spawn = {}};
    auto const no_backend = sshmcp::exec_tool_t::invoke(
        missing, {{"command", "x"}, {"session", "s"}});
    t::expect(no_backend.is_error, "no session backend");
});

[[maybe_unused]] auto const t7 = t::add_test("session dead", [] {
    auto context = sshmcp::context_t{
        .config = {},
        .spawn = {},
        .session_exec =
            [](std::string_view, std::string_view,
               std::int64_t)
            -> std::expected<sshmcp::session_result_t,
                             sshmcp::error_t> {
            return sshmcp::session_result_t{
                .result = {.stdout_text = "partial",
                           .exit_code = -1,
                           .is_timed_out = true},
                .is_session_dead = true};
        },
        .session_close = {}};
    auto const result = sshmcp::exec_tool_t::invoke(
        context,
        {{"command", "sleep 99"}, {"session", "dev"}});
    auto const body = nlohmann::json::parse(result.text);
    t::expect(body["is_session_dead"] == true, "dead");
    t::expect(body["is_timed_out"] == true, "timed out");
    t::expect(body["exit_code"] == -1, "exit -1");
});

[[maybe_unused]] auto const t8 = t::add_test("close tool", [] {
    auto closed = std::vector<std::string>{};
    auto context = sshmcp::context_t{
        .config = {},
        .spawn = {},
        .session_exec = {},
        .session_close = [&](std::string_view name) -> bool {
            closed.emplace_back(name);
            return name == "known";
        }};
    auto const hit = sshmcp::session_close_tool_t::invoke(
        context, {{"session", "known"}});
    t::expect(!hit.is_error, "close ok");
    t::expect(hit.text == "closed known", "message");
    auto const miss = sshmcp::session_close_tool_t::invoke(
        context, {{"session", "ghost"}});
    t::expect(miss.is_error, "unknown errors");
    t::expect(miss.text == "no such session: ghost",
              "unknown message");
});
```
In `test_registry.cpp` and `test_mcp.cpp` change `tools_t` /
`server_t` alias to include `sshmcp::session_close_tool_t` and
bump the expected `tools.size()` to 4 with
`tools[3]["name"] == "session_close"`.

- [ ] **Step 2: Verify red.**

- [ ] **Step 3: Implement**

types.hpp — after `session_result_t`:
```cpp
using session_exec_fn_t = std::function<std::expected<
    session_result_t, error_t>(std::string_view,
                               std::string_view,
                               std::int64_t)>;
using session_close_fn_t =
    std::function<bool(std::string_view)>;
```
and extend `context_t`:
```cpp
struct context_t {
    config_t config;
    spawn_fn_t spawn;
    session_exec_fn_t session_exec;
    session_close_fn_t session_close;
};
```

tools.hpp — inside `exec_tool_t::invoke`, after `timeout_ms`
computation, replace the spawn block with:
```cpp
        if (arguments.contains("session") &&
            arguments["session"].is_string()) {
            return invoke_session(
                context,
                arguments["session"].get<std::string>(),
                exec_command(command, cwd), timeout_ms);
        }
```
and add to `exec_tool_t`:
```cpp
    static auto invoke_session(context_t& context,
                               std::string const& name,
                               std::string const& command,
                               std::int64_t timeout_ms)
        -> tool_result_t {
        if (!context.session_exec) {
            return error_result(
                "exec: sessions unavailable in this build");
        }
        auto const spawned =
            context.session_exec(name, command, timeout_ms);
        if (!spawned) {
            return error_result("exec: " +
                                spawned.error().message);
        }
        auto const& inner = spawned->result;
        auto const out = truncate_middle(inner.stdout_text,
                                         MAX_OUTPUT_CHARS);
        auto const err = truncate_middle(inner.stderr_text,
                                         MAX_OUTPUT_CHARS);
        return {.text = dump_json(
                    {{"stdout", out.text},
                     {"stderr", err.text},
                     {"exit_code", inner.is_timed_out
                                       ? -1
                                       : inner.exit_code},
                     {"is_truncated",
                      out.is_truncated || err.is_truncated},
                     {"is_timed_out", inner.is_timed_out},
                     {"session", name},
                     {"is_session_dead",
                      spawned->is_session_dead}}),
                .is_error = false};
    }
```
`exec_tool_t::schema()` properties gain:
```cpp
{"session",
 {{"type", "string"},
  {"description",
   "named persistent shell; cwd/env persist across calls "
   "with the same name (auto-created, idle-reaped)"}}}
```
New tool at the end of tools.hpp:
```cpp
struct session_close_tool_t {
    static constexpr auto NAME =
        std::string_view{"session_close"};
    static constexpr auto DESCRIPTION = std::string_view{
        "Close a named persistent session opened via exec."};

    static auto schema() -> nlohmann::json {
        return {{"type", "object"},
                {"properties",
                 {{"session",
                   {{"type", "string"},
                    {"description", "session name"}}}}},
                {"required",
                 nlohmann::json::array({"session"})}};
    }

    static auto invoke(context_t& context,
                       nlohmann::json const& arguments)
        -> tool_result_t {
        if (!arguments.contains("session") ||
            !arguments["session"].is_string()) {
            return error_result(
                "session_close: 'session' (string) is "
                "required");
        }
        if (!context.session_close) {
            return error_result(
                "session_close: sessions unavailable");
        }
        auto const name =
            arguments["session"].get<std::string>();
        if (!context.session_close(name)) {
            return error_result("no such session: " + name);
        }
        return {.text = "closed " + name, .is_error = false};
    }
};
```
app/main.cpp — include `<sshmcp/session.hpp>`, build manager
before the server:
```cpp
    auto impl = sshmcp::platform_impl_t{config->log_level};
    impl.init_stdio();
    auto manager =
        sshmcp::session_manager_t<sshmcp::platform_impl_t>{
            impl, *config};
    auto server = sshmcp::server_t<
        sshmcp::exec_tool_t, sshmcp::read_file_tool_t,
        sshmcp::write_file_tool_t,
        sshmcp::session_close_tool_t>{sshmcp::context_t{
        .config = *config,
        .spawn =
            [&impl](sshmcp::spawn_request_t const& request) {
                return impl.run(request);
            },
        .session_exec =
            [&manager](std::string_view name,
                       std::string_view command,
                       std::int64_t timeout_ms) {
                return manager.exec(name, command,
                                    timeout_ms);
            },
        .session_close =
            [&manager](std::string_view name) {
                return manager.close(name);
            }}};
```

- [ ] **Step 4: Verify green.**
- [ ] **Step 5: Commit** — `feat: session routing in exec, session_close tool`

---

### Task 6: binary-safe file ops

**Files:**
- Modify: `include/sshmcp/tools.hpp` (encoding param),
  `regress/unit/test_tools.cpp`

**Interfaces:**
- `read_file`/`write_file` schemas gain optional `encoding`
  (`"text"` default | `"base64"`). Behavior per spec §4; text
  path byte-identical to v0.1.

- [ ] **Step 1: Failing tests**

Append to `regress/unit/test_tools.cpp`:
```cpp
[[maybe_unused]] auto const t9 = t::add_test("binary read", [] {
    auto captured = sshmcp::spawn_request_t{};
    auto const raw = std::string{"\x00\xff\x10hi", 5};
    auto context = context_with(
        {.stdout_text = raw, .exit_code = 0}, captured);
    auto const result = sshmcp::read_file_tool_t::invoke(
        context, {{"path", "/bin/x"}, {"encoding", "base64"}});
    t::expect(!result.is_error, "ok");
    t::expect(sshmcp::base64_decode(result.text).value() ==
                  raw,
              "encoded round trip");

    auto big = context_with(
        {.stdout_text =
             std::string(sshmcp::MAX_BINARY_BYTES + 1, 'x'),
         .exit_code = 0},
        captured);
    t::expect(sshmcp::read_file_tool_t::invoke(
                  big, {{"path", "/big"},
                        {"encoding", "base64"}})
                  .is_error,
              "8 MiB cap");

    auto text_ok = context_with(
        {.stdout_text =
             std::string(sshmcp::MAX_READ_BYTES + 1, 'x'),
         .exit_code = 0},
        captured);
    t::expect(!sshmcp::read_file_tool_t::invoke(
                   text_ok, {{"path", "/t"},
                             {"encoding", "base64"}})
                   .is_error,
              "base64 mode allows >1MiB");
});

[[maybe_unused]] auto const t10 = t::add_test("binary write", [] {
    auto captured = sshmcp::spawn_request_t{};
    auto context = context_with({.exit_code = 0}, captured);
    auto const raw = std::string{"\x01\x02\x00zz", 5};
    auto const result = sshmcp::write_file_tool_t::invoke(
        context, {{"path", "/bin/x"},
                  {"content", sshmcp::base64_encode(raw)},
                  {"encoding", "base64"}});
    t::expect(!result.is_error, "ok");
    t::expect(captured.stdin_data == raw,
              "raw bytes piped");
    t::expect(result.text.contains("5 bytes"),
              "decoded size reported");

    auto const bad = sshmcp::write_file_tool_t::invoke(
        context, {{"path", "/x"},
                  {"content", "!!!!"},
                  {"encoding", "base64"}});
    t::expect(bad.is_error, "invalid base64 rejected");
    t::expect(bad.text.contains("base64"), "clear error");
});
```
Add `#include <sshmcp/base64.hpp>` to the test's includes.

- [ ] **Step 2: Verify red.**

- [ ] **Step 3: Implement**

tools.hpp includes `<sshmcp/base64.hpp>`. Shared helper above
the tools:
```cpp
inline auto parse_encoding(nlohmann::json const& arguments)
    -> std::expected<bool, error_t> {  // true = base64
    if (!arguments.contains("encoding")) {
        return false;
    }
    if (!arguments["encoding"].is_string()) {
        return std::unexpected{
            error_t{"'encoding' must be a string"}};
    }
    auto const encoding =
        arguments["encoding"].get<std::string>();
    if (encoding == "text") {
        return false;
    }
    if (encoding == "base64") {
        return true;
    }
    return std::unexpected{
        error_t{"'encoding' must be \"text\" or \"base64\""}};
}
```
read_file: schema gains `encoding` property (enum-style
description). In `invoke`, after the exit-code check:
```cpp
        auto const is_base64 = parse_encoding(arguments);
        if (!is_base64) {
            return error_result("read_file: " +
                                is_base64.error().message);
        }
        auto const cap =
            *is_base64 ? MAX_BINARY_BYTES : MAX_READ_BYTES;
        if (spawned->stdout_text.size() > cap) {
            return error_result(std::format(
                "read_file: {} exceeds {} bytes ({} bytes)",
                path, cap, spawned->stdout_text.size()));
        }
        return {.text = *is_base64
                            ? base64_encode(
                                  spawned->stdout_text)
                            : spawned->stdout_text,
                .is_error = false};
```
(the encoding parse moves BEFORE the spawn so bad arguments never
spawn — place it right after the `path` validation).
write_file `invoke`, after content validation:
```cpp
        auto const is_base64 = parse_encoding(arguments);
        if (!is_base64) {
            return error_result("write_file: " +
                                is_base64.error().message);
        }
        auto content =
            arguments["content"].get<std::string>();
        if (*is_base64) {
            auto decoded = base64_decode(content);
            if (!decoded) {
                return error_result("write_file: " +
                                    decoded.error().message);
            }
            content = std::move(*decoded);
        }
        if (content.size() > MAX_BINARY_BYTES) {
            return error_result(
                "write_file: content exceeds 8 MiB");
        }
```
`"wrote {} bytes"` keeps reporting the decoded size.

- [ ] **Step 4: Verify green** (existing text fixtures must stay
  green untouched).
- [ ] **Step 5: Commit** — `feat: base64 encoding for file tools`

---

### Task 7: e2e — session mode fake ssh + cases

**Files:**
- Modify: `regress/e2e/fake_ssh.py`,
  `regress/e2e/cases/basic/*` (only `golden.jsonl` via re-bless —
  tools/list grew), `CMakeLists.txt` (two `add_test` rows)
- Create: `regress/e2e/cases/session_basic/{transcript,replies}.jsonl`,
  `regress/e2e/cases/session_timeout/{transcript,replies}.jsonl`
  (+ blessed goldens/expected_calls)

**Interfaces:**
- fake_ssh session mode activates when the last argv token is
  `sh`: process stays alive, parses protocol blocks, answers from
  `replies.jsonl` in order, honors `sleep_ms` before answering,
  logs `{"session_command": [...lines...]}` per block.

- [ ] **Step 1: fake_ssh session mode**

Replace `fake_ssh.py`'s `main` dispatch:
```python
def main() -> int:
    if sys.argv[1:] and sys.argv[-1] == "sh":
        return session_mode()
    return oneshot_mode()
```
(rename the existing body to `oneshot_mode`). Add:
```python
def emit(reply: dict, counter: str) -> None:
    time.sleep(reply.get("sleep_ms", 0) / 1000.0)
    out = reply.get("stdout", "") + \
        f"\n@sshmcp:{counter}:{reply.get('exit', 0)}@\n"
    err = reply.get("stderr", "") + f"\n@sshmcp:{counter}@\n"
    sys.stdout.buffer.write(out.encode("utf-8"))
    sys.stdout.buffer.flush()
    sys.stderr.buffer.write(err.encode("utf-8"))
    sys.stderr.buffer.flush()


def session_mode() -> int:
    import re
    log_path = os.environ["FAKE_SSH_LOG"]
    with open(os.environ["FAKE_SSH_REPLIES"],
              encoding="utf-8") as f:
        replies = [json.loads(x) for x in f if x.strip()]
    with open(log_path, "a", encoding="utf-8") as log:
        log.write(json.dumps(
            {"argv": sys.argv[1:], "stdin": ""}) + "\n")
    index = 0
    command: list[str] = []
    collecting = False
    for raw in sys.stdin.buffer:
        line = raw.decode("utf-8").rstrip("\n")
        if line == "{":
            collecting = True
            command = []
            continue
        if line == "} </dev/null":
            collecting = False
            continue
        found = re.match(
            r"printf '\\n@sshmcp:(\d+):%s@\\n' \"\$\?\"",
            line)
        if found:
            with open(log_path, "a",
                      encoding="utf-8") as log:
                log.write(json.dumps(
                    {"session_command": command}) + "\n")
            reply = replies[index] if index < len(replies) \
                else {"stdout": "",
                      "stderr": "fake_ssh: no reply",
                      "exit": 1}
            index += 1
            emit(reply, found.group(1))
            continue
        if collecting:
            command.append(line)
    return 0
```
(the stderr-printf line falls through unmatched and is ignored —
`collecting` is already False by then.)

- [ ] **Step 2: Cases**

`regress/e2e/cases/session_basic/transcript.jsonl`:
```
{"jsonrpc":"2.0","id":0,"method":"initialize","params":{}}
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"exec","arguments":{"command":"pwd","session":"dev"}}}
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"exec","arguments":{"command":"echo second","session":"dev"}}}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"session_close","arguments":{"session":"dev"}}}
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"session_close","arguments":{"session":"dev"}}}
```
`replies.jsonl`:
```
{"stdout":"/home/dev\n","stderr":"","exit":0}
{"stdout":"second\n","stderr":"","exit":0}
```
`regress/e2e/cases/session_timeout/transcript.jsonl`:
```
{"jsonrpc":"2.0","id":0,"method":"initialize","params":{}}
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"exec","arguments":{"command":"sleep 30","session":"dev","timeout_ms":500}}}
```
`replies.jsonl`:
```
{"stdout":"late\n","stderr":"","exit":0,"sleep_ms":10000}
```
CMake:
```cmake
add_test(NAME e2e_session_basic COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/driver.py
    $<TARGET_FILE:sshmcp_app>
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/cases/session_basic)
add_test(NAME e2e_session_timeout COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/driver.py
    $<TARGET_FILE:sshmcp_app>
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/cases/session_timeout)
```

- [ ] **Step 3: Bless + inspect**

Bless the two new cases plus `basic` (tools/list grew a 4th
tool). MANUALLY VERIFY: session_basic golden shows both exec
results with `"session":"dev"`, `"is_session_dead":false`,
`closed dev` then error `no such session: dev`; expected_calls
shows ONE ssh spawn (argv ending `sh`) and two
`session_command` records; timeout golden shows `exit_code:-1`,
`is_timed_out:true`, `is_session_dead:true`. `errors` and
`timeout` v0.1 cases must NOT need re-blessing.

- [ ] **Step 4: Full gate + commit** —
  `test: session e2e via stateful fake ssh`

---

### Task 8: docs, CI, live smoke, merge

**Files:**
- Modify: `README.md` (sessions + encoding docs)

- [ ] **Step 1: README** — add "Sessions" section (implicit via
  `exec.session`, 4-session cap, 30 min idle reap,
  `SSHMCP_SESSION_IDLE_MS`, stdin/heredoc limitation,
  `session_close`) and extend the file-tools paragraph + env
  table with `encoding: "base64"` and the 8 MiB cap.
- [ ] **Step 2: Full local gate** — debug + release presets,
  all ctest suites.
- [ ] **Step 3: Live smoke vs devbox** (OpenBSD):
```bash
printf '%s\n' \
 '{"jsonrpc":"2.0","id":0,"method":"initialize","params":{}}' \
 '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"exec","arguments":{"command":"cd /tmp && export MARK=v2","session":"s"}}}' \
 '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"exec","arguments":{"command":"pwd && echo $MARK","session":"s"}}}' \
 '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"session_close","arguments":{"session":"s"}}}' \
 | SSHMCP_TARGET=me@devbox.mshome.net SSHMCP_LOG=info \
   ./build/windows-msvc-release/sshmcp.exe
```
Expected: second exec prints `/tmp` and `v2` (state persisted),
first session call ~1 s, second <150 ms. Then binary round-trip:
write 64 random bytes base64 → read back → compare locally.
- [ ] **Step 4: Commit docs** — `docs: sessions and binary file ops`
- [ ] **Step 5: Merge to main, push, watch CI green, confirm
  autotag `v0.2.x`.**

---

## Self-review notes (applied)

- Spec §3.1 payload/sentinels = Task 2 exactly; §3.2 routing =
  Task 5; §3.3 lifecycle = Task 4; §4 = Tasks 1+6; §5 platform =
  Task 3; §6 testing = Tasks 2-7 + live smoke Task 8.
- Type names consistent: `stream_t`, `chunk_t`,
  `session_result_t`, `stream_id_t`, `session_manager_t`,
  `session_exec_fn_t`, `session_close_fn_t`,
  `session_close_tool_t`, `parse_encoding`, `base64_encode`,
  `base64_decode`.
- v0.1 goldens: only `basic` re-blessed (tools/list), `errors` +
  `timeout` untouched — session-less exec JSON unchanged.
