# sshmcp v0.3 — Port Forwarding, pledge, Allowlist

Date: 2026-08-17
Status: approved (user approved scope; detail review waived)
Builds on: v0.1 + v0.2 specs (shipped through tag v0.7.3)

## 1. Purpose

Three additions: local port forwarding so the AI can start a dev
server remotely and the user browses it locally; OpenBSD `pledge`
hardening of the server process; an optional command allowlist as
an accident guardrail.

## 2. Port forwarding

- Tools (count goes 4 → 6):
  - `forward_open` `{local_port: integer, remote_port: integer,
    remote_host?: string = "localhost"}` → success text
    `forwarding 127.0.0.1:<lp> -> <rh>:<rp>`; tool error on
    failure with ssh's stderr attached.
  - `forward_close` `{local_port: integer}` → `closed forward
    <lp>` or error `no such forward: <lp>`.
- Mechanism: dedicated long-lived child per forward:
  `<ssh_exe> -N -o BatchMode=yes <ssh_args>
  -L 127.0.0.1:<lp>:<rh>:<rp> <target>`, spawned via the v0.2
  streaming surface (stdin unused). Loopback bind only.
- Health check: after spawn, read stderr in slices for up to
  500 ms. Child closed within that window → the forward failed
  (bad port, refused, auth): kill, return error with collected
  stderr. Still alive → success. (ssh prints nothing on a
  successful `-N` forward.)
- `forward_manager_t<PlatformType>` in
  `include/sshmcp/forward.hpp`: `open(local_port, remote_host,
  remote_port) -> std::expected<std::string, error_t>` (success
  text), `close(local_port) -> bool`. Map keyed by local port.
  `MAX_FORWARDS = 8`; opening a duplicate local port is an error
  naming it. No idle reap — forwards live until closed or server
  exit (stream destructors kill children).
- `context_t` gains `forward_open_fn_t forward_open` and
  `forward_close_fn_t forward_close` (injectable fakes).
- Port validation: 1..65535, integers only.

## 3. OpenBSD pledge

- CRTP base gains `harden()` → `Derived::harden_impl()`, called
  in `main` after config load, before serving.
- windows/linux: no-op. openbsd:
  `pledge("stdio rpath proc exec", nullptr)` — pipes/poll
  (stdio), execvp path resolution (rpath), fork (proc), exec ssh
  (exec). Failure to pledge: log to stderr at info, continue
  (older kernels); a runtime violation kills the process, which
  the devbox live smoke exercises.
- `unveil` intentionally omitted: it persists across exec and
  would break ssh's access to keys, known_hosts and resolver
  files. Documented here, revisit only with a curated path list.

## 4. Command allowlist

- `SSHMCP_ALLOW` env: comma-separated command names
  (e.g. `git,make,ls,cat,npm`). Unset or empty → no restriction
  (v0.2 behavior, byte-identical outputs).
- When set, `exec` (one-shot and session) validates the RAW
  command string before cwd-wrapping:
  1. reject if it contains compound/expansion syntax: `;`, `&&`,
     `||`, `|`, `&`, backtick, `$(`, or a newline — otherwise a
     first-token check is trivially chained around;
  2. first whitespace-delimited token must be in the list.
- Threat model honesty (README + here): this is a guardrail
  against accidental destructive commands, not a security
  boundary — the AI operator is trusted (v0.1 §10 unchanged).
- Pure helper in `include/sshmcp/allow.hpp`:
  `parse_allow(std::string_view) -> std::vector<std::string>`;
  `check_allowed(std::string_view command,
  std::vector<std::string> const& allow)
  -> std::expected<void, error_t>` with a message naming the
  offending token or syntax. `config_t` gains
  `std::vector<std::string> allow`.
- `read_file`/`write_file`/`session_close`/forward tools are not
  gated (they are structurally constrained already).

## 5. Testing

- Unit: `check_allowed` (empty list passes all; listed/unlisted
  tokens; each rejected syntax token; leading whitespace; tabs);
  forward argv construction; `forward_manager_t` against the
  v0.2 mock-platform pattern (open success needs a mock whose
  stderr stays open/silent; open failure via closed stderr with
  text; duplicate port; cap; close unknown). Exec gating via
  fake `context_t` (allow set, compound rejected, session path
  gated too).
- E2E: fake_ssh gains `-N` mode — logs argv, then sleeps until
  killed (success case); a `FAKE_SSH_FORWARD_FAIL` env makes it
  print to stderr and exit 1 (failure case). Cases:
  `forward_basic` (open, close, close-again-error),
  `forward_fail` (open against failing shim → isError with
  stderr text). tools/list golden re-blessed (6 tools).
- Live smoke (devbox, OpenBSD): pledge active; open forward to
  devbox sshd port 22, `curl`-less check via local TCP connect
  (python socket) proving the tunnel accepts; close it;
  sessions + files still green under pledge.

## 6. Policies

All prior policies unchanged. `feat:` commits, rolling release.
Windows `/WX` ci preset built locally before every push (process
rule from v0.2 fallout).
