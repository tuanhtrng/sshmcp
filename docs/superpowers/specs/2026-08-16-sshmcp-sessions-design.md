# sshmcp v0.2 — Persistent Sessions + Binary File Ops

Date: 2026-08-16
Status: approved (user waived final review)
Builds on: `2026-08-16-sshmcp-design.md` (v0.1, shipped)

## 1. Purpose

Two gaps in v0.1: every `exec` pays a full SSH handshake (~1 s to
the devbox) and loses shell state (cwd, env, venv); file tools are
UTF-8-text-only. v0.2 adds named persistent sessions routed through
`exec`, and binary-safe `read_file`/`write_file`.

## 2. Non-goals

PTY sessions, prompt detection, port forwarding, multi-host,
command allowlist, pledge/unveil, background/async exec. All parked
for v0.3+.

## 3. Sessions

### 3.1 Mechanism — sentinel-framed plain shell

Per session: one long-lived child
`<ssh_exe> -T -o BatchMode=yes <ssh_args> <target> sh`.
Each exec writes one protocol block to its stdin (`<n>` is a
process-wide monotonically increasing call counter):

```sh
{ <command>
} </dev/null
printf '\n@sshmcp:<n>:%s@\n' "$?"
printf '\n@sshmcp:<n>@\n' >&2
```

- stdout is read until a line `@sshmcp:<n>:<exit>@` appears; the
  exit code is taken from it. stderr is read until its
  `@sshmcp:<n>@` line. Both sentinel lines and the one injected
  leading `\n` are stripped from the captured output.
- No PTY: no echo, no prompts, deterministic framing.
- `</dev/null` prevents commands from consuming protocol lines.
- Documented limitations: session commands must not read stdin
  (use `write_file`) and must not emit the literal sentinel
  string. The AI operator is trusted (v0.1 threat model), so
  sentinel spoofing is not defended against.

### 3.2 Interface — implicit via exec

- `exec` input gains optional `session: string` (name). Absent →
  v0.1 one-shot path, byte-for-byte unchanged output. Present →
  routed to that session, auto-created on first use.
- When `session` is present the result JSON additionally carries
  `"session"` (name) and `"is_session_dead"` (bool). Session-less
  results are unchanged (existing e2e goldens stay frozen).
- New tool `session_close` `{session: string}` → text result
  `closed <name>` or `no such session: <name>`. Tool count: 4.

### 3.3 Lifecycle

- Registry caps live sessions at `MAX_SESSIONS = 4`; opening a
  5th returns a tool error naming the live sessions.
- Lazy reaping: before handling each `tools/call`, sessions idle
  longer than `SSHMCP_SESSION_IDLE_MS` (default 1'800'000) are
  killed. No background threads.
- Timeout mid-command leaves remote shell state unknown → the
  whole session is killed; the result carries partial output,
  `is_timed_out: true`, `exit_code: -1`, `is_session_dead: true`.
- Session child died (ssh dropped) → same flag, error text in
  stderr field; next exec with that name auto-recreates.

## 4. Binary-safe file ops

Key invariant: ssh pipes are 8-bit clean, so no encoder is needed
on the remote — stock OpenBSD works.

- `read_file`/`write_file` gain `encoding: "text" | "base64"`
  (default `"text"`; text behavior and its 1 MiB cap unchanged).
- `write_file` base64: decode locally, pipe raw bytes to
  `cat > <path>`. Decoded size cap `MAX_BINARY_BYTES = 8 MiB`.
  Bad base64 → tool error, nothing spawned.
- `read_file` base64: raw bytes from `cat <path>`, encoded
  locally into the result text. Raw size cap 8 MiB.
- New header `include/sshmcp/base64.hpp`: `base64_encode(span) ->
  string`, `base64_decode(string_view) -> expected<string,
  error_t>` (strict: rejects invalid chars, bad padding).

## 5. Architecture changes

- **Platform layer (CRTP) grows a streaming surface** beside the
  one-shot `run()`. Concept sketch — each `platform_impl_t` adds:
  - `stream_spawn_impl(argv) -> expected<stream_id, error_t>`
  - `stream_write_impl(id, bytes) -> bool`
  - `stream_read_impl(id, which, deadline) ->
    expected<chunk_t, error_t>` where
    `chunk_t{std::string data, bool is_closed}` — returns
    accumulated bytes (possibly empty on deadline slice)
  - `stream_kill_impl(id) -> void`
  Windows: one reader thread per pipe feeding a mutex+condvar
  buffer; `stream_read_impl` waits with deadline. POSIX:
  non-blocking fds + `poll`.
- **`include/sshmcp/session.hpp`**: pure protocol helpers
  (`session_payload(counter, command)`, incremental
  `sentinel_scan` over accumulating buffers) + `session_manager_t
  <PlatformType>` (name→session map, counter, reap, exec, close).
  Protocol helpers are pure and unit-tested without any process.
- **`context_t`** gains
  `session_exec: std::function<expected<session_result_t,
  error_t>(std::string_view name, std::string_view command,
  std::int64_t timeout_ms)>` and
  `session_close: std::function<bool(std::string_view name)>`,
  both injectable fakes in unit tests.
  `session_result_t = spawn_result_t + bool is_session_dead`.
- `app/main.cpp` wires `session_manager_t<platform_impl_t>` into
  the context. MCP loop stays serial (unchanged from v0.1).

## 6. Testing

- Unit: base64 round-trips + strict-decode rejections; payload
  builder; sentinel scanner fed adversarial chunk splits (sentinel
  across chunk boundary, output containing partial sentinels,
  missing trailing newline); session manager against a scripted
  in-memory fake platform (satisfies the streaming concept);
  tool-layer session routing via fake `context_t` functions.
- Platform tests: streaming spawn against a local
  `python -u` echo-loop child — write/read/deadline/kill per OS.
- E2E: `fake_ssh.py` gains session mode (process stays alive,
  parses protocol blocks from stdin, replies from `replies.jsonl`
  and emits correct sentinels). New cases: `session_basic`
  (create, two execs, close), `session_timeout` (sleep reply →
  killed session, `is_session_dead`). Existing v0.1 cases and
  goldens untouched except `tools/list` (schemas grew) — re-bless
  with manual inspection.
- Live smoke vs devbox (OpenBSD): cd persistence across two
  session execs, venv-style env persistence, binary round-trip
  (`write_file` base64 of random bytes, `read_file` back, byte
  compare locally).

## 7. Policies

All v0.1 policies carry over unchanged (no macros, no exceptions,
no `using`, zero-condition CMake, TDD, 80 col, linters, rolling
release). This release is `feat:` → version 0.2.0 via hook.
