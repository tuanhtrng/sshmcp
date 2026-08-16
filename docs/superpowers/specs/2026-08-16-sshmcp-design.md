# sshmcp — SSH MCP Server, Design Spec

Date: 2026-08-16
Status: approved (user waived final review)

## 1. Purpose

An MCP (Model Context Protocol) server that lets an AI agent (Claude
Code) develop on a remote VPS over SSH. The server runs locally, is
spawned by the MCP client over stdio, and executes work on the VPS by
shelling out to the user's OpenSSH client.

MVP goal: an AI can run commands, read files, and write files on one
VPS, reliably enough for real development work.

## 2. Non-goals (MVP)

- Persistent interactive shell sessions (PTY, prompt detection)
- Multiple hosts per server instance (run one server entry per host)
- Port forwarding, SFTP protocol, SCP
- Password authentication (OpenSSH config/agent/keys handle auth)
- ssh_config parsing (OpenSSH resolves aliases itself)
- ControlMaster multiplexing (unsupported on Windows OpenSSH)
- Command allowlists / sandboxing of what the AI may run (see §10)

## 3. Architecture

- **Language / shape:** C++23, header-only library in
  `include/sshmcp/`, thin executable entry in `app/main.cpp`.
- **Transport:** MCP stdio framing (newline-delimited JSON-RPC 2.0).
  stdout carries protocol messages only; all logging goes to stderr.
  On Windows, stdin/stdout are set to binary mode at startup to
  prevent `\n` → `\r\n` translation.
- **SSH transport:** every operation spawns the OpenSSH CLI (`ssh`).
  This inherits the user's `~/.ssh/config`, agent, keys,
  `known_hosts`, and ProxyJump for free. Each call pays a full SSH
  handshake (~300 ms); acceptable for MVP.
- **MCP protocol subset:** `initialize` (reports `serverInfo` with
  name and version), `notifications/initialized`, `tools/list`,
  `tools/call`, `ping`. Unknown methods get JSON-RPC
  `method not found`.
- **JSON:** vendored nlohmann/json single header at
  `include/nlohmann/json.hpp` (MIT notice retained). Used in
  non-throwing mode (`parse(..., nullptr, false)`).

### Configuration (environment variables)

| Variable | Meaning | Default |
|---|---|---|
| `SSHMCP_TARGET` | `user@host` or ssh_config alias | required (or argv[1]) |
| `SSHMCP_SSH_EXE` | ssh command to spawn; split into an argv prefix on whitespace, double quotes group tokens (`"C:\Program Files\Python314\python" fake_ssh.py`) | `ssh` |
| `SSHMCP_SSH_ARGS` | extra args passed through to ssh | empty |
| `SSHMCP_LOG` | `off` / `info` / `debug` | `info` |

## 4. Tools

Three tools. A non-zero remote exit code is a **normal result**, not
a tool error — the AI must see it. Tool errors are reserved for
transport failures (spawn failure, ssh connection/auth failure,
timeout).

### 4.1 `exec`

Input: `{ command: string, cwd?: string, timeout_ms?: integer }`

- Spawns:
  `ssh -T -o BatchMode=yes <SSHMCP_SSH_ARGS> <target> <command>`
- `cwd` prepends `cd <single-quoted cwd> && ` to the command.
- Timeout: default 60 000 ms, cap 600 000 ms. On expiry the child
  process is killed and partial output is returned with
  `is_timed_out: true`.
- Output:
  `{ stdout, stderr, exit_code, is_truncated, is_timed_out }`.
  stdout and stderr each truncated to 50 000 chars, keeping head and
  tail with an explicit truncation marker.

### 4.2 `read_file`

Input: `{ path: string }`

- Spawns:
  `ssh -T -o BatchMode=yes <target> cat <single-quoted path>`
- Refuses content larger than 1 MiB with a clear error. UTF-8
  assumed.

### 4.3 `write_file`

Input: `{ path: string, content: string }`

- Spawns: `ssh -T -o BatchMode=yes <target>
  "mkdir -p <quoted dir> && cat > <quoted path>"`
- Content is piped to the child's stdin — exact bytes, no heredoc
  quoting hazards.

### Remote path quoting

One pure function: wrap in single quotes, escape embedded single
quotes as `'\''`. Unit-tested exhaustively; the e2e harness also
asserts the exact argv ssh receives.

## 5. Core abstractions

### 5.1 Tool registry (concepts)

```cpp
template<typename ToolType>
concept tool = requires(context_t& ctx, nlohmann::json const& args) {
    { ToolType::NAME }
        -> std::convertible_to<std::string_view>;
    { ToolType::DESCRIPTION }
        -> std::convertible_to<std::string_view>;
    { ToolType::schema() } -> std::same_as<nlohmann::json>;
    { ToolType::invoke(ctx, args) } -> std::same_as<tool_result_t>;
};

template<tool... ToolTypes>
class tool_set_t;  // tools/list; dispatches tools/call by name
```

No reflection in C++23, so each tool hand-writes its input schema
JSON.

### 5.2 Platform layer (CRTP, preset-selected)

```
include/sshmcp/platform/windows/impl.hpp
include/sshmcp/platform/linux/impl.hpp
include/sshmcp/platform/openbsd/impl.hpp
```

Each defines
`struct platform_impl_t : subprocess_base_t<platform_impl_t>`.

- The CMake preset sets cache variable `SSHMCP_PLATFORM`
  (`windows`/`linux`/`openbsd`); `CMakeLists.txt` appends
  `include/sshmcp/platform/${SSHMCP_PLATFORM}` to the include path.
  `subprocess.hpp` includes `<impl.hpp>`, resolved by include path.
  **No preprocessor conditionals anywhere.**
- CRTP base = template method: `run()` owns argv logging, timing,
  timeout policy, result assembly; the derived type owns raw
  `spawn_impl` / `wait_impl` / `kill_impl`.
- Subprocess contract:
  `run(argv, stdin_data, timeout_ms)
   -> std::expected<spawn_result_t, error_t>`
  where
  `spawn_result_t = { stdout, stderr, exit_code, is_timed_out }`.
- Windows: `CreateProcessW` + anonymous pipes. Linux/OpenBSD:
  `fork`/`exec` + pipes, shared code in
  `platform/posix_common.hpp`, derived structs differ only where the
  kernels do. OpenBSD `pledge`/`unveil` hardening is a v2 extension
  point.

## 6. Code style

- **80-column limit** — code, docs, all text files. clang-format
  `ColumnLimit: 80`; .editorconfig `max_line_length = 80`. Markdown
  tables exempt where unwrappable.
- **AAA:** `auto variable = type{};`
- **Trailing return everywhere:** `auto function() -> type`
- **East const:** `auto const&`, `std::string const*`
- Types `something_t`; variables, functions, members plain
  `snake_case` (members unmarked; `this->` where ambiguous);
  `bool is_something`
- Template parameters `PascalCase`; concepts bare `snake_case`
- `inline constexpr` constants `SCREAMING_SNAKE`;
  `enum class something_t { VALUE }`
- Enumerator names must avoid known Windows macro collisions
  (`ERROR`, `IGNORE`, `DELETE`, `SMALL`, …) — linter-enforced list
- Enforcement: clang-format `QualifierAlignment: Right`;
  `share/linter/style.py` heuristics for AAA and trailing return
  with `// style-ok` escape comment

## 7. Hard policies

- **No macros.** Zero `#define` and zero `#if` in our sources. Only
  `#pragma once` and `#include` are permitted preprocessor
  directives. Platform selection is include-path based (§5.2);
  `WIN32_LEAN_AND_MEAN`, `NOMINMAX`, `NOGDI`, `_FORTIFY_SOURCE`
  live in preset flags. Vendored nlohmann is exempt.
  Linter-enforced.
- **No exceptions from our code.** Fallible operations return
  `std::expected`; nlohmann runs in non-throwing parse mode.
- **Modern C++23 floor:** `std::expected`, `std::print`/`println`
  (stderr logging), `std::source_location`, ranges, designated
  initializers, `std::span`. Toolchains: MSVC 19.38+, gcc 14+,
  clang 19+ (clang 18 lacks __cpp_concepts 202002, gating std::expected in libstdc++).
- **Zero conditions in CMakeLists.txt.** No `if()`, no
  `$<CXX_COMPILER_ID>` generator expressions. All platform/compiler
  variance lives in `CMakePresets.json` (cache variables, flags,
  preset inheritance via hidden base presets).
- **TDD.** Red-green per unit; e2e transcripts as the acceptance
  layer.
- **Versioning:** semver, rolling release — every commit on `main`
  releasable. Single source of truth: `project(sshmcp VERSION
  X.Y.Z)` in `CMakeLists.txt`. `configure_file` generates
  `<build>/generated/sshmcp/version.hpp` containing
  `inline constexpr std::string_view VERSION` (constexpr, not a
  macro), reported via MCP `initialize`. A `commit-msg` hook
  (`.githooks/` + `core.hooksPath`, `share/version_bump.py`)
  bumps the version on every commit from the conventional type:
  `!`/`BREAKING CHANGE` → major, `feat` → minor, else patch;
  `[no bump]` or an explicit staged version change skips it. CI
  auto-tags `vX.Y.Z` on `main` when the CMakeLists version
  changes — every main commit is a tagged release. E2E goldens
  normalize `serverInfo.version`. Starts at `0.1.0`.
- **Conventional commits** (`feat:` / `fix:` / `chore:` / …).
- **License:** MIT.

## 8. Build

- `CMakeLists.txt`: `add_library(sshmcp INTERFACE)` +
  `target_compile_features(sshmcp INTERFACE cxx_std_23)` + include
  dirs + `add_executable(sshmcp_app app/main.cpp)` +
  `enable_testing()` + `add_test` rows. Nothing conditional.
- `CMakePresets.json`: Ninja generator throughout; hidden base
  presets for shared settings; configure + build + test presets per
  platform × compiler:

| Preset | Notes |
|---|---|
| `windows-msvc-debug/-release` | `/W4 /permissive-`; release: `/O2 /guard:cf /sdl /CETCOMPAT /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT` + `/DWIN32_LEAN_AND_MEAN /DNOMINMAX /DNOGDI` |
| `windows-clang-cl-debug` | parity check |
| `linux-gcc-debug/-release` | `-Wall -Wextra -Wpedantic`; release: `-O3 -fhardened -fstack-clash-protection -fcf-protection=full -ftrivial-auto-var-init=zero -Wl,-z,noexecstack` |
| `linux-clang-debug/-release` | hardening spelled out (`-D_FORTIFY_SOURCE=3 -fstack-protector-strong -fPIE -pie -Wl,-z,relro,-z,now`, …) |
| `linux-clang-cfi` | `-fsanitize=cfi -flto=thin -fvisibility=hidden` |
| `linux-clang-asan` | ASan + UBSan, debug |
| `openbsd-clang-release` | system defaults already PIE/retguard/W^X; `-O3` |
| `release-native` | adds `-march=native` |

- **LTO:** `CMAKE_INTERPROCEDURAL_OPTIMIZATION=ON` in all release
  presets (MSVC `/GL /LTCG` via CMake IPO; gcc/clang `-flto`; CFI
  preset uses ThinLTO).
- CI presets add warnings-as-errors; local presets do not.

## 9. Testing

- **Unit (`regress/unit/`):** own macro-free micro harness
  (~100 lines):
  `expect(bool, what, std::source_location = current())`, lambda
  registration, no `#define`. Targets: quoting, truncation, JSON-RPC
  framing, registry dispatch, params parsing, config parsing.
- **E2E (`regress/e2e/`):** Python driver runs the real
  `sshmcp_app` binary with `SSHMCP_SSH_EXE` pointed at a fake-ssh
  Python shim. The shim records argv + stdin to a log and replies
  with canned stdout/exit codes. Driver feeds JSON-RPC transcript
  files to the server's stdin and diffs responses against golden
  files; also asserts the exact ssh argv (catches quoting
  regressions unit tests miss). Full handshake + all three tools,
  zero network.
- **Linters (`share/linter/*.py`), all ctest jobs:**
  - `header_only.py` — no `.cpp` outside `app/` and `regress/`
    (test runners are executables)
  - `banned_tokens.py` — `system(`, `popen`, `std::regex`, raw
    `new`/`delete`, `throw`/`try`/`catch`, any `using namespace`
    and any using-declaration (`using ns::name;`); only the alias
    form `using name = type;` and namespace aliases
    (`namespace t = a::b;`) are allowed; forbidden enumerator
    names; and the no-macro rule (`#define`/`#if` count zero
    outside vendored code)
  - `include_hygiene.py` — `#pragma once` present; include order
    (own, sshmcp, nlohmann, std)
  - `check_format.py` — clang-format `--dry-run --Werror`
  - `style.py` — AAA + trailing-return heuristics, `// style-ok`
    escape
- **CI (GitHub Actions):** matrix over presets (MSVC, gcc, clang,
  asan), full ctest (units + e2e + linters) + clang-tidy job
  (`bugprone-*`, `cert-*`, `clang-analyzer-security-*`,
  `modernize-*`). OpenBSD via `vmactions/openbsd-vm`,
  best-effort/optional. Rolling release requires green `main`.

## 10. Security

- **Trust root:** the user's `known_hosts`. `BatchMode=yes` turns
  unknown-host-key prompts into hard failures. No
  `StrictHostKeyChecking` relaxation. First contact with a new VPS
  is done once by hand with plain `ssh`.
- **Log hygiene:** `info` logs timings and exit codes only; full
  command lines and argv appear only at `debug` (commands routinely
  contain secrets).
- **Quoting correctness** is the main injection surface; guarded by
  unit tests plus e2e argv assertions.
- **Hardened binary:** flags in §8.
- **Honest threat model:** the AI client is authorized to run
  arbitrary commands on the VPS — that is the product. This design
  secures the transport, the trust decisions, the logs, and the
  binary. It does not restrict what commands run; a command
  allowlist/sandbox would be a separate v2 feature.

## 11. Layout

```
app/main.cpp
cmake/version.hpp.in     (configure_file input; generated header
                          lands in <build>/generated/sshmcp/)
include/sshmcp/          types.hpp  ssh.hpp  util.hpp  config.hpp
                         jsonrpc.hpp  registry.hpp  tools.hpp
                         mcp.hpp  subprocess_base.hpp
                         subprocess.hpp
                         platform/{windows,linux,openbsd}/impl.hpp
                         platform/posix_common.hpp
include/nlohmann/json.hpp  (vendored, MIT)
share/linter/*.py
regress/unit/  regress/e2e/
docs/superpowers/specs/  docs/superpowers/plans/
CMakeLists.txt  CMakePresets.json  .clang-format  .editorconfig
.clang-tidy  .gitattributes  LICENSE  README.md
.github/workflows/ci.yml
```
