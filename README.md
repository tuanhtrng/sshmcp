# sshmcp

An MCP (Model Context Protocol) stdio server that lets an AI agent
develop on a remote VPS over SSH. C++23, header-only, zero runtime
dependencies beyond your OpenSSH client.

Three tools: `exec` (run a command), `read_file`, `write_file`.
Every operation shells out to `ssh`, so authentication, host keys,
aliases, agents and `ProxyJump` all come from your existing
`~/.ssh/config` — sshmcp adds no auth machinery of its own.

## Requirements

- OpenSSH client (`ssh` on PATH, or point `SSHMCP_SSH_EXE` at one)
- To build: CMake 3.28+, Ninja, a C++23 toolchain
  (MSVC 19.38+, gcc 14+, or clang 19+)
- Python 3 for the regress suites and linters

## Build

```
cmake --preset windows-msvc-release
cmake --build --preset windows-msvc-release
```

Linux: use `linux-gcc-release` or `linux-clang-release`. OpenBSD:
`openbsd-clang-release`. See `CMakePresets.json` for the full
matrix (debug, ASan, CFI, `-march=native`).

## Claude Code configuration

`.mcp.json`:

```json
{
  "mcpServers": {
    "vps": {
      "command":
        "C:/Users/me/sshmcp/build/windows-msvc-release/sshmcp.exe",
      "env": {
        "SSHMCP_TARGET": "dev@my-vps",
        "SSHMCP_LOG": "info"
      }
    }
  }
}
```

## Environment variables

| Variable | Meaning | Default |
|---|---|---|
| `SSHMCP_TARGET` | `user@host` or ssh_config alias | required (or argv[1]) |
| `SSHMCP_SSH_EXE` | ssh command; whitespace-split, `"..."` groups tokens | `ssh` |
| `SSHMCP_SSH_ARGS` | extra args passed to ssh | empty |
| `SSHMCP_LOG` | `off` / `info` / `debug` (stderr) | `info` |

## Security notes

- `BatchMode=yes` is always passed: an unknown host key or a
  missing credential fails immediately instead of prompting.
  `known_hosts` is the trust root — make first contact with a new
  VPS once by hand with plain `ssh` before pointing sshmcp at it.
- At `info` log level only timings and exit codes go to stderr.
  Full command lines appear only at `debug`, because commands
  routinely contain secrets.
- The AI client is authorized to run arbitrary commands on the
  target — that is the product. There is no command allowlist in
  this version.

## Development

```
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug
```

That runs the unit suite (macro-free harness), the e2e suite
(a fake-ssh Python shim asserts the exact argv and stdin that
would reach OpenSSH), and five policy linters (header-only, no
macros/exceptions/`using namespace`, include hygiene,
clang-format, AAA + trailing-return style).

Enable the hooks once per clone:

```
git config core.hooksPath .githooks
```

## Versioning

Semver, rolling release. The version lives in `project(sshmcp
VERSION x.y.z)` in `CMakeLists.txt` only; a `commit-msg` hook
bumps it on every commit from the conventional-commit type
(`feat:` → minor, `!`/`BREAKING CHANGE` → major, otherwise
patch), and CI tags `vX.Y.Z` on green `main`.

## License

MIT. Vendored `include/nlohmann/json.hpp` is MIT (see
`include/nlohmann/LICENSE.MIT`).
