# sshmcp MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use
> superpowers:subagent-driven-development (recommended) or
> superpowers:executing-plans to implement this plan task-by-task.
> Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** C++23 header-only MCP stdio server that lets an AI dev on a
VPS by spawning the OpenSSH CLI for exec/read_file/write_file.

**Architecture:** Header-only lib in `include/sshmcp/`; concepts-based
tool registry; CRTP platform subprocess layer selected by CMake preset
include path; all fallible paths return `std::expected`.

**Tech Stack:** C++23 (MSVC 19.51 local; gcc-14/clang-18 CI), CMake
3.28+/Ninja, vendored nlohmann/json 3.11.3, Python 3 for e2e + lint.

**Spec:** `docs/superpowers/specs/2026-08-16-sshmcp-design.md`

## Global Constraints

- No `#define`, no `#if*` in our sources; only `#pragma once` and
  `#include`. Vendored `include/nlohmann/json.hpp` exempt.
- No exceptions thrown/caught by our code; `std::expected` for
  fallible ops; nlohmann parse non-throwing; `dump()` always with
  `error_handler_t::replace`.
- Zero conditions in CMakeLists.txt: no `if()`, no conditional
  generator expressions (`$<TARGET_FILE:...>` is allowed).
- Style: AAA, trailing return, east const, `something_t` types,
  snake_case, `PascalCase` template params, bare snake_case concepts,
  `SCREAMING_SNAKE` constexpr + enumerators, 80 columns.
- No `using namespace` and no using-declarations (`using ns::name;`)
  anywhere. Only alias forms allowed: `using x = y;` and
  `namespace t = a::b;`. Qualify names fully otherwise.
- TDD: failing test first, then minimal code, per task.
- Conventional commits. Local dev preset: `windows-msvc-debug`.
- Constants: timeout default 60000 ms cap 600000 ms; output truncation
  50000 chars per stream; read_file cap 1 MiB (1048576).
- Version starts `0.1.0` in `include/sshmcp/version.hpp` only.

---

### Task 1: Scaffold

**Files:**
- Create: `.gitattributes`, `.editorconfig`, `.clang-format`,
  `.clang-tidy`, `LICENSE`, `README.md`, `CMakeLists.txt`,
  `CMakePresets.json`, `app/main.cpp`, `include/nlohmann/json.hpp`
  (download), `include/nlohmann/LICENSE.MIT` (download),
  `cmake/version.hpp.in`

**Interfaces:**
- Produces: `sshmcp::VERSION` (`std::string_view`, from
  `project(VERSION)`, generated into
  `<build>/generated/sshmcp/version.hpp`); CMake targets `sshmcp`
  (INTERFACE) and `sshmcp_app`; presets `windows-msvc-debug` etc.

- [ ] **Step 1: Repo plumbing files**

`.gitattributes`:
```
* text=auto eol=lf
```

`.editorconfig`:
```ini
root = true

[*]
charset = utf-8
end_of_line = lf
insert_final_newline = true
indent_style = space
indent_size = 4
max_line_length = 80
trim_trailing_whitespace = true
```

`.clang-format`:
```yaml
BasedOnStyle: LLVM
IndentWidth: 4
ColumnLimit: 80
QualifierAlignment: Right
PointerAlignment: Left
DerivePointerAlignment: false
AllowShortFunctionsOnASingleLine: Empty
AlwaysBreakTemplateDeclarations: Yes
```

`.clang-tidy`:
```yaml
Checks: >
  bugprone-*,
  cert-*,
  clang-analyzer-security-*,
  modernize-*,
  -bugprone-easily-swappable-parameters,
  -modernize-use-nodiscard
WarningsAsErrors: ''
```

`LICENSE`: MIT text, copyright `2026 sshmcp contributors`.

`README.md` stub:
```markdown
# sshmcp

SSH MCP server: lets an AI agent develop on a VPS over OpenSSH.
C++23, header-only. See docs/superpowers/specs/ for the design.
```

- [ ] **Step 2: Vendor nlohmann/json 3.11.3**

```bash
mkdir -p include/nlohmann app include/sshmcp
curl -fsSL -o include/nlohmann/json.hpp \
  https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
curl -fsSL -o include/nlohmann/LICENSE.MIT \
  https://raw.githubusercontent.com/nlohmann/json/v3.11.3/LICENSE.MIT
```

- [ ] **Step 3: version template + stub main**

`cmake/version.hpp.in` (configure_file input; the generated header
is constexpr — no macro):
```cpp
#pragma once

#include <string_view>

namespace sshmcp {

inline constexpr auto VERSION = std::string_view{"@PROJECT_VERSION@"};

}  // namespace sshmcp
```

`app/main.cpp` (stub, replaced in Task 11):
```cpp
#include <sshmcp/version.hpp>

#include <print>

auto main() -> int {
    std::println(stderr, "sshmcp {} scaffold", sshmcp::VERSION);
    return 0;
}
```

- [ ] **Step 4: CMakeLists.txt (zero conditions)**

```cmake
cmake_minimum_required(VERSION 3.28)
project(sshmcp VERSION 0.1.0 LANGUAGES CXX)

find_package(Python3 REQUIRED COMPONENTS Interpreter)

configure_file(cmake/version.hpp.in
    ${CMAKE_CURRENT_BINARY_DIR}/generated/sshmcp/version.hpp @ONLY)

add_library(sshmcp INTERFACE)
target_compile_features(sshmcp INTERFACE cxx_std_23)
target_include_directories(sshmcp INTERFACE
    ${CMAKE_CURRENT_BINARY_DIR}/generated
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/sshmcp/platform/${SSHMCP_PLATFORM})

add_executable(sshmcp_app app/main.cpp)
target_link_libraries(sshmcp_app PRIVATE sshmcp)
set_target_properties(sshmcp_app PROPERTIES OUTPUT_NAME sshmcp)

enable_testing()
```

- [ ] **Step 5: CMakePresets.json**

Concrete presets carry complete flag strings (preset inheritance
overrides, never merges, so no partial flag strings in bases).

```json
{
  "version": 6,
  "cmakeMinimumRequired": {"major": 3, "minor": 28, "patch": 0},
  "configurePresets": [
    {"name": "base", "hidden": true, "generator": "Ninja",
     "binaryDir": "${sourceDir}/build/${presetName}"},
    {"name": "windows", "hidden": true, "inherits": "base",
     "cacheVariables": {"SSHMCP_PLATFORM": "windows"}},
    {"name": "linux", "hidden": true, "inherits": "base",
     "cacheVariables": {"SSHMCP_PLATFORM": "linux"}},
    {"name": "openbsd", "hidden": true, "inherits": "base",
     "cacheVariables": {"SSHMCP_PLATFORM": "openbsd"}},

    {"name": "windows-msvc-debug", "inherits": "windows",
     "cacheVariables": {
       "CMAKE_BUILD_TYPE": "Debug",
       "CMAKE_CXX_COMPILER": "cl",
       "CMAKE_CXX_FLAGS": "/W4 /permissive- /EHsc /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DNOGDI"}},
    {"name": "windows-msvc-release", "inherits": "windows",
     "cacheVariables": {
       "CMAKE_BUILD_TYPE": "Release",
       "CMAKE_CXX_COMPILER": "cl",
       "CMAKE_INTERPROCEDURAL_OPTIMIZATION": "ON",
       "CMAKE_CXX_FLAGS": "/W4 /permissive- /EHsc /utf-8 /guard:cf /sdl /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DNOGDI",
       "CMAKE_EXE_LINKER_FLAGS": "/guard:cf /CETCOMPAT /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT"}},
    {"name": "windows-clang-cl-debug", "inherits": "windows",
     "cacheVariables": {
       "CMAKE_BUILD_TYPE": "Debug",
       "CMAKE_CXX_COMPILER": "clang-cl",
       "CMAKE_CXX_FLAGS": "/W4 /EHsc /utf-8 /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DNOGDI"}},

    {"name": "linux-gcc-debug", "inherits": "linux",
     "cacheVariables": {
       "CMAKE_BUILD_TYPE": "Debug",
       "CMAKE_CXX_COMPILER": "g++-14",
       "CMAKE_CXX_FLAGS": "-Wall -Wextra -Wpedantic"}},
    {"name": "linux-gcc-release", "inherits": "linux",
     "cacheVariables": {
       "CMAKE_BUILD_TYPE": "Release",
       "CMAKE_CXX_COMPILER": "g++-14",
       "CMAKE_INTERPROCEDURAL_OPTIMIZATION": "ON",
       "CMAKE_CXX_FLAGS": "-Wall -Wextra -Wpedantic -fhardened -fstack-clash-protection -fcf-protection=full -ftrivial-auto-var-init=zero",
       "CMAKE_EXE_LINKER_FLAGS": "-Wl,-z,noexecstack"}},
    {"name": "linux-clang-debug", "inherits": "linux",
     "cacheVariables": {
       "CMAKE_BUILD_TYPE": "Debug",
       "CMAKE_CXX_COMPILER": "clang++-18",
       "CMAKE_CXX_FLAGS": "-Wall -Wextra -Wpedantic"}},
    {"name": "linux-clang-release", "inherits": "linux",
     "cacheVariables": {
       "CMAKE_BUILD_TYPE": "Release",
       "CMAKE_CXX_COMPILER": "clang++-18",
       "CMAKE_INTERPROCEDURAL_OPTIMIZATION": "ON",
       "CMAKE_CXX_FLAGS": "-Wall -Wextra -Wpedantic -D_FORTIFY_SOURCE=3 -fstack-protector-strong -fstack-clash-protection -fcf-protection=full -ftrivial-auto-var-init=zero -fPIE",
       "CMAKE_EXE_LINKER_FLAGS": "-pie -Wl,-z,relro,-z,now -Wl,-z,noexecstack"}},
    {"name": "linux-clang-cfi", "inherits": "linux",
     "cacheVariables": {
       "CMAKE_BUILD_TYPE": "Release",
       "CMAKE_CXX_COMPILER": "clang++-18",
       "CMAKE_INTERPROCEDURAL_OPTIMIZATION": "ON",
       "CMAKE_CXX_FLAGS": "-Wall -Wextra -Wpedantic -D_FORTIFY_SOURCE=3 -fstack-protector-strong -fstack-clash-protection -fcf-protection=full -ftrivial-auto-var-init=zero -fPIE -fsanitize=cfi -fvisibility=hidden",
       "CMAKE_EXE_LINKER_FLAGS": "-pie -Wl,-z,relro,-z,now -Wl,-z,noexecstack -fsanitize=cfi"}},
    {"name": "linux-clang-asan", "inherits": "linux",
     "cacheVariables": {
       "CMAKE_BUILD_TYPE": "Debug",
       "CMAKE_CXX_COMPILER": "clang++-18",
       "CMAKE_CXX_FLAGS": "-Wall -Wextra -Wpedantic -fsanitize=address,undefined -fno-omit-frame-pointer",
       "CMAKE_EXE_LINKER_FLAGS": "-fsanitize=address,undefined"}},
    {"name": "openbsd-clang-release", "inherits": "openbsd",
     "cacheVariables": {
       "CMAKE_BUILD_TYPE": "Release",
       "CMAKE_CXX_COMPILER": "clang++",
       "CMAKE_INTERPROCEDURAL_OPTIMIZATION": "ON",
       "CMAKE_CXX_FLAGS": "-Wall -Wextra -Wpedantic"}},
    {"name": "release-native", "inherits": "linux",
     "cacheVariables": {
       "CMAKE_BUILD_TYPE": "Release",
       "CMAKE_CXX_COMPILER": "g++-14",
       "CMAKE_INTERPROCEDURAL_OPTIMIZATION": "ON",
       "CMAKE_CXX_FLAGS": "-Wall -Wextra -Wpedantic -march=native -fhardened -fstack-clash-protection -fcf-protection=full -ftrivial-auto-var-init=zero",
       "CMAKE_EXE_LINKER_FLAGS": "-Wl,-z,noexecstack"}},

    {"name": "ci-windows-msvc", "inherits": "windows",
     "cacheVariables": {
       "CMAKE_BUILD_TYPE": "Release",
       "CMAKE_CXX_COMPILER": "cl",
       "CMAKE_INTERPROCEDURAL_OPTIMIZATION": "ON",
       "CMAKE_CXX_FLAGS": "/W4 /permissive- /EHsc /utf-8 /WX /guard:cf /sdl /DWIN32_LEAN_AND_MEAN /DNOMINMAX /DNOGDI",
       "CMAKE_EXE_LINKER_FLAGS": "/guard:cf /CETCOMPAT /DYNAMICBASE /HIGHENTROPYVA /NXCOMPAT"}},
    {"name": "ci-linux-gcc", "inherits": "linux",
     "cacheVariables": {
       "CMAKE_BUILD_TYPE": "Release",
       "CMAKE_CXX_COMPILER": "g++-14",
       "CMAKE_INTERPROCEDURAL_OPTIMIZATION": "ON",
       "CMAKE_CXX_FLAGS": "-Wall -Wextra -Wpedantic -Werror -fhardened -fstack-clash-protection -fcf-protection=full -ftrivial-auto-var-init=zero",
       "CMAKE_EXE_LINKER_FLAGS": "-Wl,-z,noexecstack"}},
    {"name": "ci-linux-clang", "inherits": "linux",
     "cacheVariables": {
       "CMAKE_BUILD_TYPE": "Release",
       "CMAKE_CXX_COMPILER": "clang++-18",
       "CMAKE_INTERPROCEDURAL_OPTIMIZATION": "ON",
       "CMAKE_CXX_FLAGS": "-Wall -Wextra -Wpedantic -Werror -D_FORTIFY_SOURCE=3 -fstack-protector-strong -fstack-clash-protection -fcf-protection=full -ftrivial-auto-var-init=zero -fPIE",
       "CMAKE_EXE_LINKER_FLAGS": "-pie -Wl,-z,relro,-z,now -Wl,-z,noexecstack"}},
    {"name": "ci-linux-clang-asan", "inherits": "linux",
     "cacheVariables": {
       "CMAKE_BUILD_TYPE": "Debug",
       "CMAKE_CXX_COMPILER": "clang++-18",
       "CMAKE_CXX_FLAGS": "-Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer",
       "CMAKE_EXE_LINKER_FLAGS": "-fsanitize=address,undefined"}}
  ],
  "buildPresets": [
    {"name": "windows-msvc-debug", "configurePreset": "windows-msvc-debug"},
    {"name": "windows-msvc-release", "configurePreset": "windows-msvc-release"},
    {"name": "windows-clang-cl-debug", "configurePreset": "windows-clang-cl-debug"},
    {"name": "linux-gcc-debug", "configurePreset": "linux-gcc-debug"},
    {"name": "linux-gcc-release", "configurePreset": "linux-gcc-release"},
    {"name": "linux-clang-debug", "configurePreset": "linux-clang-debug"},
    {"name": "linux-clang-release", "configurePreset": "linux-clang-release"},
    {"name": "linux-clang-cfi", "configurePreset": "linux-clang-cfi"},
    {"name": "linux-clang-asan", "configurePreset": "linux-clang-asan"},
    {"name": "openbsd-clang-release", "configurePreset": "openbsd-clang-release"},
    {"name": "release-native", "configurePreset": "release-native"},
    {"name": "ci-windows-msvc", "configurePreset": "ci-windows-msvc"},
    {"name": "ci-linux-gcc", "configurePreset": "ci-linux-gcc"},
    {"name": "ci-linux-clang", "configurePreset": "ci-linux-clang"},
    {"name": "ci-linux-clang-asan", "configurePreset": "ci-linux-clang-asan"}
  ],
  "testPresets": [
    {"name": "windows-msvc-debug", "configurePreset": "windows-msvc-debug",
     "output": {"outputOnFailure": true}},
    {"name": "windows-msvc-release", "configurePreset": "windows-msvc-release",
     "output": {"outputOnFailure": true}},
    {"name": "linux-gcc-release", "configurePreset": "linux-gcc-release",
     "output": {"outputOnFailure": true}},
    {"name": "linux-clang-release", "configurePreset": "linux-clang-release",
     "output": {"outputOnFailure": true}},
    {"name": "linux-clang-asan", "configurePreset": "linux-clang-asan",
     "output": {"outputOnFailure": true}},
    {"name": "openbsd-clang-release", "configurePreset": "openbsd-clang-release",
     "output": {"outputOnFailure": true}},
    {"name": "ci-windows-msvc", "configurePreset": "ci-windows-msvc",
     "output": {"outputOnFailure": true}},
    {"name": "ci-linux-gcc", "configurePreset": "ci-linux-gcc",
     "output": {"outputOnFailure": true}},
    {"name": "ci-linux-clang", "configurePreset": "ci-linux-clang",
     "output": {"outputOnFailure": true}},
    {"name": "ci-linux-clang-asan", "configurePreset": "ci-linux-clang-asan",
     "output": {"outputOnFailure": true}}
  ]
}
```

- [ ] **Step 6: Verify build**

Run: `cmake --preset windows-msvc-debug` then
`cmake --build --preset windows-msvc-debug`
Expected: configure + build succeed;
`build/windows-msvc-debug/sshmcp.exe` exists and prints
`sshmcp 0.1.0 scaffold` when run.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "chore: scaffold build, presets, vendored json"
```

---

### Task 2: Unit test harness (macro-free)

**Files:**
- Create: `regress/unit/harness.hpp`, `regress/unit/main.cpp`,
  `regress/unit/test_harness.cpp`
- Modify: `CMakeLists.txt` (add `sshmcp_unit` target + test)

**Interfaces:**
- Produces: `sshmcp::test::add_test(std::string_view,
  std::function<void()>) -> bool`;
  `sshmcp::test::expect(bool, std::string_view,
  std::source_location = current()) -> void`;
  `sshmcp::test::run_all() -> int`. Test files register cases as
  `[[maybe_unused]] auto const tN = add_test("name", []{...});`
  at anonymous-namespace scope.

- [ ] **Step 1: Write harness**

`regress/unit/harness.hpp`:
```cpp
#pragma once

#include <functional>
#include <print>
#include <source_location>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace sshmcp::test {

struct case_t {
    std::string name;
    std::function<void()> body;
};

struct state_t {
    std::vector<case_t> cases;
    int checks_total{};
    int checks_failed{};
};

inline auto state() -> state_t& {
    static auto instance = state_t{};
    return instance;
}

inline auto add_test(std::string_view name,
                     std::function<void()> body) -> bool {
    state().cases.push_back(
        {std::string{name}, std::move(body)});
    return true;
}

inline auto expect(bool is_ok, std::string_view what,
                   std::source_location loc =
                       std::source_location::current())
    -> void {
    ++state().checks_total;
    if (!is_ok) {
        ++state().checks_failed;
        std::println(stderr, "FAIL {}:{}: {}",
                     loc.file_name(), loc.line(), what);
    }
}

inline auto run_all() -> int {
    for (auto const& test_case : state().cases) {
        test_case.body();
    }
    std::println(stderr, "{} cases, {} checks, {} failed",
                 state().cases.size(), state().checks_total,
                 state().checks_failed);
    return state().checks_failed == 0 ? 0 : 1;
}

}  // namespace sshmcp::test
```

`regress/unit/main.cpp`:
```cpp
#include "harness.hpp"

auto main() -> int { return sshmcp::test::run_all(); }
```

- [ ] **Step 2: Self-test**

`regress/unit/test_harness.cpp`:
```cpp
#include "harness.hpp"

namespace {

namespace t = sshmcp::test;

[[maybe_unused]] auto const t1 =
    t::add_test("harness registers and counts", [] {
        t::expect(true, "trivially true");
        t::expect(!t::state().cases.empty(),
                  "case registered");
    });

}  // namespace
```

- [ ] **Step 3: Wire into CMake**

Append to `CMakeLists.txt`:
```cmake
add_executable(sshmcp_unit
    regress/unit/main.cpp
    regress/unit/test_harness.cpp)
target_link_libraries(sshmcp_unit PRIVATE sshmcp)
add_test(NAME unit COMMAND sshmcp_unit)
```

- [ ] **Step 4: Run**

Run: `cmake --build --preset windows-msvc-debug` then
`ctest --preset windows-msvc-debug`
Expected: `unit` test PASSES, prints `1 cases, 2 checks, 0 failed`.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "test: macro-free unit harness via source_location"
```

---

### Task 3: Core types + ssh command building

**Files:**
- Create: `include/sshmcp/types.hpp`, `include/sshmcp/ssh.hpp`,
  `regress/unit/test_ssh.cpp`
- Modify: `CMakeLists.txt` (add `regress/unit/test_ssh.cpp` to
  `sshmcp_unit` sources)

**Interfaces:**
- Produces (types.hpp): `log_level_t {OFF, INFO, DEBUG}`;
  `error_t{std::string message}`;
  `spawn_request_t{std::vector<std::string> argv,
  std::string stdin_data, std::int64_t timeout_ms}`;
  `spawn_result_t{std::string stdout_text, std::string
  stderr_text, int exit_code, bool is_timed_out}`;
  `spawn_fn_t = std::function<std::expected<spawn_result_t,
  error_t>(spawn_request_t const&)>`;
  `config_t{std::string target, std::vector<std::string>
  ssh_exe{"ssh"}, std::vector<std::string> ssh_args,
  log_level_t log_level{INFO}}`;
  `context_t{config_t config, spawn_fn_t spawn}`;
  `tool_result_t{std::string text, bool is_error}`;
  constants `DEFAULT_TIMEOUT_MS=60'000`, `MAX_TIMEOUT_MS=600'000`,
  `MAX_OUTPUT_CHARS=50'000`, `MAX_READ_BYTES=1'048'576`.
- Produces (ssh.hpp): `quote_posix(std::string_view) ->
  std::string`; `split_command(std::string_view) ->
  std::vector<std::string>` (whitespace split, double quotes
  group); `build_ssh_argv(config_t const&, std::string) ->
  std::vector<std::string>`; `exec_command(std::string_view,
  std::optional<std::string> const&) -> std::string`;
  `read_command(std::string_view) -> std::string`;
  `write_command(std::string_view) -> std::string`;
  `parent_dir(std::string_view) -> std::optional<std::string>`.

- [ ] **Step 1: Write failing tests**

`regress/unit/test_ssh.cpp`:
```cpp
#include "harness.hpp"

#include <sshmcp/ssh.hpp>

#include <string>
#include <vector>

namespace {

namespace t = sshmcp::test;

[[maybe_unused]] auto const t1 = t::add_test("quote_posix", [] {
    t::expect(sshmcp::quote_posix("abc") == "'abc'", "plain");
    t::expect(sshmcp::quote_posix("a b") == "'a b'", "space");
    t::expect(sshmcp::quote_posix("a'b") == "'a'\\''b'",
              "embedded quote");
    t::expect(sshmcp::quote_posix("") == "''", "empty");
});

[[maybe_unused]] auto const t2 = t::add_test("split_command", [] {
    t::expect(sshmcp::split_command("ssh") ==
                  std::vector<std::string>{"ssh"},
              "single");
    t::expect(sshmcp::split_command("a  b\tc") ==
                  std::vector<std::string>{"a", "b", "c"},
              "whitespace runs");
    t::expect(sshmcp::split_command(
                  "\"C:\\Program Files\\p\" x.py") ==
                  std::vector<std::string>{
                      "C:\\Program Files\\p", "x.py"},
              "quoted token");
    t::expect(sshmcp::split_command("\"\"").size() == 1,
              "empty quoted token survives");
    t::expect(sshmcp::split_command("").empty(), "empty");
    t::expect(sshmcp::split_command("  \t ").empty(), "blank");
});

[[maybe_unused]] auto const t3 = t::add_test("build_ssh_argv", [] {
    auto config = sshmcp::config_t{};
    config.target = "dev@host";
    config.ssh_args = {"-p", "2222"};
    auto const argv = sshmcp::build_ssh_argv(config, "echo hi");
    auto const want = std::vector<std::string>{
        "ssh", "-T", "-o", "BatchMode=yes", "-p", "2222",
        "dev@host", "echo hi"};
    t::expect(argv == want, "argv layout");
});

[[maybe_unused]] auto const t4 = t::add_test("remote commands", [] {
    t::expect(sshmcp::exec_command("make", std::nullopt) ==
                  "make",
              "no cwd");
    t::expect(sshmcp::exec_command(
                  "make", std::string{"/src"}) ==
                  "cd '/src' && make",
              "cwd prepended");
    t::expect(sshmcp::read_command("/a b") == "cat '/a b'",
              "read");
    t::expect(sshmcp::write_command("/d/f.txt") ==
                  "mkdir -p '/d' && cat > '/d/f.txt'",
              "write with dir");
    t::expect(sshmcp::write_command("f.txt") ==
                  "cat > 'f.txt'",
              "write no dir");
    t::expect(sshmcp::write_command("/f.txt") ==
                  "cat > '/f.txt'",
              "write at root");
});

}  // namespace
```

Add `regress/unit/test_ssh.cpp` to `sshmcp_unit` sources in
`CMakeLists.txt`.

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build --preset windows-msvc-debug`
Expected: FAILS to compile — `sshmcp/ssh.hpp` not found.

- [ ] **Step 3: Implement types.hpp**

`include/sshmcp/types.hpp`:
```cpp
#pragma once

#include <cstdint>
#include <expected>
#include <functional>
#include <string>
#include <vector>

namespace sshmcp {

inline constexpr auto DEFAULT_TIMEOUT_MS = std::int64_t{60'000};
inline constexpr auto MAX_TIMEOUT_MS = std::int64_t{600'000};
inline constexpr auto MAX_OUTPUT_CHARS = std::size_t{50'000};
inline constexpr auto MAX_READ_BYTES = std::size_t{1'048'576};

enum class log_level_t { OFF, INFO, DEBUG };

struct error_t {
    std::string message;
};

struct spawn_request_t {
    std::vector<std::string> argv;
    std::string stdin_data;
    std::int64_t timeout_ms{DEFAULT_TIMEOUT_MS};
};

struct spawn_result_t {
    std::string stdout_text;
    std::string stderr_text;
    int exit_code{};
    bool is_timed_out{};
};

using spawn_fn_t = std::function<std::expected<
    spawn_result_t, error_t>(spawn_request_t const&)>;

struct config_t {
    std::string target;
    std::vector<std::string> ssh_exe{"ssh"};
    std::vector<std::string> ssh_args;
    log_level_t log_level{log_level_t::INFO};
};

struct context_t {
    config_t config;
    spawn_fn_t spawn;
};

struct tool_result_t {
    std::string text;
    bool is_error{};
};

}  // namespace sshmcp
```

- [ ] **Step 4: Implement ssh.hpp**

`include/sshmcp/ssh.hpp`:
```cpp
#pragma once

#include <sshmcp/types.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace sshmcp {

inline auto quote_posix(std::string_view text) -> std::string {
    auto out = std::string{"'"};
    for (auto const ch : text) {
        if (ch == '\'') {
            out += "'\\''";
        } else {
            out.push_back(ch);
        }
    }
    out.push_back('\'');
    return out;
}

inline auto split_command(std::string_view text)
    -> std::vector<std::string> {
    auto tokens = std::vector<std::string>{};
    auto current = std::string{};
    auto in_quotes = false;
    auto has_token = false;
    for (auto const ch : text) {
        if (ch == '"') {
            in_quotes = !in_quotes;
            has_token = true;
            continue;
        }
        if (!in_quotes && (ch == ' ' || ch == '\t')) {
            if (has_token) {
                tokens.push_back(current);
                current.clear();
                has_token = false;
            }
            continue;
        }
        current.push_back(ch);
        has_token = true;
    }
    if (has_token) {
        tokens.push_back(current);
    }
    return tokens;
}

inline auto build_ssh_argv(config_t const& config,
                           std::string remote_command)
    -> std::vector<std::string> {
    auto argv = config.ssh_exe;
    argv.push_back("-T");
    argv.push_back("-o");
    argv.push_back("BatchMode=yes");
    argv.insert(argv.end(), config.ssh_args.begin(),
                config.ssh_args.end());
    argv.push_back(config.target);
    argv.push_back(std::move(remote_command));
    return argv;
}

inline auto exec_command(
    std::string_view command,
    std::optional<std::string> const& cwd) -> std::string {
    if (cwd) {
        return "cd " + quote_posix(*cwd) + " && " +
               std::string{command};
    }
    return std::string{command};
}

inline auto read_command(std::string_view path)
    -> std::string {
    return "cat " + quote_posix(path);
}

inline auto parent_dir(std::string_view path)
    -> std::optional<std::string> {
    auto const pos = path.rfind('/');
    if (pos == std::string_view::npos || pos == 0) {
        return std::nullopt;
    }
    return std::string{path.substr(0, pos)};
}

inline auto write_command(std::string_view path)
    -> std::string {
    auto const dir = parent_dir(path);
    if (dir) {
        return "mkdir -p " + quote_posix(*dir) +
               " && cat > " + quote_posix(path);
    }
    return "cat > " + quote_posix(path);
}

}  // namespace sshmcp
```

- [ ] **Step 5: Run to verify pass**

Run: `cmake --build --preset windows-msvc-debug` then
`ctest --preset windows-msvc-debug`
Expected: PASS, 0 failed.

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "feat: core types, posix quoting, ssh argv builders"
```

---

### Task 4: Output truncation

**Files:**
- Create: `include/sshmcp/util.hpp`, `regress/unit/test_util.cpp`
- Modify: `CMakeLists.txt` (add test source)

**Interfaces:**
- Produces: `truncated_t{std::string text, bool is_truncated}`;
  `truncate_middle(std::string_view, std::size_t max_chars) ->
  truncated_t` — keeps head and tail halves, inserts marker
  `\n...[sshmcp: N chars truncated]...\n`.

- [ ] **Step 1: Write failing tests**

`regress/unit/test_util.cpp`:
```cpp
#include "harness.hpp"

#include <sshmcp/util.hpp>

#include <string>

namespace {

namespace t = sshmcp::test;

[[maybe_unused]] auto const t1 = t::add_test("truncate", [] {
    auto const small = sshmcp::truncate_middle("abc", 10);
    t::expect(small.text == "abc", "small unchanged");
    t::expect(!small.is_truncated, "small flag");

    auto const big =
        sshmcp::truncate_middle(std::string(100, 'x'), 10);
    t::expect(big.is_truncated, "big flag");
    t::expect(big.text.starts_with("xxxxx"), "head kept");
    t::expect(big.text.ends_with("xxxxx"), "tail kept");
    t::expect(big.text.contains(
                  "[sshmcp: 90 chars truncated]"),
              "marker");

    auto const exact =
        sshmcp::truncate_middle(std::string(10, 'y'), 10);
    t::expect(!exact.is_truncated, "exact fits");
});

}  // namespace
```

- [ ] **Step 2: Run to verify failure**

Run: `cmake --build --preset windows-msvc-debug`
Expected: FAILS — `sshmcp/util.hpp` not found.

- [ ] **Step 3: Implement**

`include/sshmcp/util.hpp`:
```cpp
#pragma once

#include <sshmcp/types.hpp>

#include <format>
#include <string>
#include <string_view>

namespace sshmcp {

struct truncated_t {
    std::string text;
    bool is_truncated{};
};

inline auto truncate_middle(std::string_view text,
                            std::size_t max_chars)
    -> truncated_t {
    if (text.size() <= max_chars) {
        return {std::string{text}, false};
    }
    auto const half = max_chars / 2;
    auto const dropped = text.size() - 2 * half;
    auto out = std::string{text.substr(0, half)};
    out += std::format("\n...[sshmcp: {} chars truncated]...\n",
                       dropped);
    out += text.substr(text.size() - half);
    return {std::move(out), true};
}

}  // namespace sshmcp
```

- [ ] **Step 4: Run to verify pass**

Run: build + `ctest --preset windows-msvc-debug`
Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: head/tail output truncation"
```

---

### Task 5: Config from environment

**Files:**
- Create: `include/sshmcp/config.hpp`,
  `regress/unit/test_config.cpp`
- Modify: `CMakeLists.txt` (add test source)

**Interfaces:**
- Consumes: `split_command` (Task 3), `config_t`, `error_t`.
- Produces: `get_env_fn_t = std::function<char const*(char
  const*)>`; `parse_log_level(std::string_view) -> log_level_t`;
  `load_config(get_env_fn_t const&, std::span<std::string const>)
  -> std::expected<config_t, error_t>`.

- [ ] **Step 1: Write failing tests**

`regress/unit/test_config.cpp`:
```cpp
#include "harness.hpp"

#include <sshmcp/config.hpp>

#include <map>
#include <string>
#include <vector>

namespace {

namespace t = sshmcp::test;

auto env_of(std::map<std::string, std::string> values)
    -> sshmcp::get_env_fn_t {
    return [values = std::move(values)](
               char const* name) -> char const* {
        auto const found = values.find(name);
        return found == values.end() ? nullptr
                                     : found->second.c_str();
    };
}

[[maybe_unused]] auto const t1 = t::add_test("target sources", [] {
    auto const args = std::vector<std::string>{};
    auto const from_env = sshmcp::load_config(
        env_of({{"SSHMCP_TARGET", "dev@host"}}), args);
    t::expect(from_env.has_value(), "env target ok");
    t::expect(from_env->target == "dev@host", "env target");

    auto const arg_list =
        std::vector<std::string>{"me@vps"};
    auto const from_args =
        sshmcp::load_config(env_of({}), arg_list);
    t::expect(from_args.has_value(), "args target ok");
    t::expect(from_args->target == "me@vps", "args target");

    auto const missing = sshmcp::load_config(env_of({}), args);
    t::expect(!missing.has_value(), "missing target fails");
});

[[maybe_unused]] auto const t2 = t::add_test("ssh exe + args", [] {
    auto const args = std::vector<std::string>{"a@b"};
    auto const config = sshmcp::load_config(
        env_of({{"SSHMCP_SSH_EXE",
                 "\"C:\\Program Files\\Python\\python\" f.py"},
                {"SSHMCP_SSH_ARGS", "-p 2222"}}),
        args);
    t::expect(config.has_value(), "loads");
    auto const want_exe = std::vector<std::string>{
        "C:\\Program Files\\Python\\python", "f.py"};
    t::expect(config->ssh_exe == want_exe, "exe split");
    auto const want_args =
        std::vector<std::string>{"-p", "2222"};
    t::expect(config->ssh_args == want_args, "args split");

    auto const blank = sshmcp::load_config(
        env_of({{"SSHMCP_SSH_EXE", "  "}}), args);
    t::expect(!blank.has_value(), "blank exe fails");
});

[[maybe_unused]] auto const t3 = t::add_test("log level", [] {
    auto const args = std::vector<std::string>{"a@b"};
    auto const debug = sshmcp::load_config(
        env_of({{"SSHMCP_LOG", "debug"}}), args);
    t::expect(debug->log_level == sshmcp::log_level_t::DEBUG,
              "debug");
    auto const off = sshmcp::load_config(
        env_of({{"SSHMCP_LOG", "off"}}), args);
    t::expect(off->log_level == sshmcp::log_level_t::OFF,
              "off");
    auto const fallback = sshmcp::load_config(env_of({}), args);
    t::expect(fallback->log_level == sshmcp::log_level_t::INFO,
              "default info");
});

}  // namespace
```

- [ ] **Step 2: Run to verify failure**

Expected: compile FAIL — `sshmcp/config.hpp` not found.

- [ ] **Step 3: Implement**

`include/sshmcp/config.hpp`:
```cpp
#pragma once

#include <sshmcp/ssh.hpp>
#include <sshmcp/types.hpp>

#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>

namespace sshmcp {

using get_env_fn_t =
    std::function<char const*(char const*)>;

inline auto parse_log_level(std::string_view text)
    -> log_level_t {
    if (text == "off") {
        return log_level_t::OFF;
    }
    if (text == "debug") {
        return log_level_t::DEBUG;
    }
    return log_level_t::INFO;
}

inline auto load_config(get_env_fn_t const& get_env,
                        std::span<std::string const> args)
    -> std::expected<config_t, error_t> {
    auto config = config_t{};
    auto const* target = get_env("SSHMCP_TARGET");
    if (target != nullptr && *target != '\0') {
        config.target = target;
    } else if (!args.empty()) {
        config.target = args[0];
    } else {
        return std::unexpected{error_t{
            "no target: set SSHMCP_TARGET or pass "
            "user@host as the first argument"}};
    }
    auto const* ssh_exe = get_env("SSHMCP_SSH_EXE");
    if (ssh_exe != nullptr && *ssh_exe != '\0') {
        config.ssh_exe = split_command(ssh_exe);
        if (config.ssh_exe.empty()) {
            return std::unexpected{
                error_t{"SSHMCP_SSH_EXE is blank"}};
        }
    }
    auto const* ssh_args = get_env("SSHMCP_SSH_ARGS");
    if (ssh_args != nullptr) {
        config.ssh_args = split_command(ssh_args);
    }
    auto const* log_level = get_env("SSHMCP_LOG");
    if (log_level != nullptr) {
        config.log_level = parse_log_level(log_level);
    }
    return config;
}

}  // namespace sshmcp
```

- [ ] **Step 4: Run to verify pass**

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: env config loading with injectable getenv"
```

---

### Task 6: JSON-RPC framing

**Files:**
- Create: `include/sshmcp/jsonrpc.hpp`,
  `regress/unit/test_jsonrpc.cpp`
- Modify: `CMakeLists.txt` (add test source)

**Interfaces:**
- Produces: constants `PARSE_ERROR=-32700`,
  `INVALID_REQUEST=-32600`, `METHOD_NOT_FOUND=-32601`,
  `INVALID_PARAMS=-32602`; `request_t{nlohmann::json id,
  std::string method, nlohmann::json params, bool
  is_notification}`; `parse_request(std::string_view) ->
  std::expected<request_t, error_t>` (params normalized to an
  object); `make_result(nlohmann::json const& id, nlohmann::json
  const& result) -> std::string`; `make_error(nlohmann::json
  const& id, int code, std::string_view message) -> std::string`.
  Both serializers use
  `dump(-1, ' ', false, nlohmann::json::error_handler_t::replace)`
  so invalid UTF-8 from remote output can never throw.

- [ ] **Step 1: Write failing tests**

`regress/unit/test_jsonrpc.cpp`:
```cpp
#include "harness.hpp"

#include <sshmcp/jsonrpc.hpp>

#include <nlohmann/json.hpp>

namespace {

namespace t = sshmcp::test;

[[maybe_unused]] auto const t1 = t::add_test("parse request", [] {
    auto const request = sshmcp::parse_request(
        R"({"jsonrpc":"2.0","id":7,"method":"ping"})");
    t::expect(request.has_value(), "parses");
    t::expect(request->method == "ping", "method");
    t::expect(request->id == 7, "id");
    t::expect(!request->is_notification, "not notification");
    t::expect(request->params.is_object(),
              "params normalized to object");
});

[[maybe_unused]] auto const t2 = t::add_test("notification", [] {
    auto const request = sshmcp::parse_request(
        R"({"jsonrpc":"2.0","method":"notifications/x"})");
    t::expect(request.has_value(), "parses");
    t::expect(request->is_notification, "flagged");
});

[[maybe_unused]] auto const t3 = t::add_test("bad input", [] {
    t::expect(!sshmcp::parse_request("not json").has_value(),
              "invalid json");
    t::expect(!sshmcp::parse_request(R"({"id":1})").has_value(),
              "missing method");
    t::expect(!sshmcp::parse_request(
                   R"({"jsonrpc":"1.0","id":1,"method":"m"})")
                   .has_value(),
              "wrong version");
});

[[maybe_unused]] auto const t4 = t::add_test("serialize", [] {
    auto const result = nlohmann::json::parse(
        sshmcp::make_result(3, {{"ok", true}}));
    t::expect(result["jsonrpc"] == "2.0", "result envelope");
    t::expect(result["id"] == 3, "result id");
    t::expect(result["result"]["ok"] == true, "result body");

    auto const error = nlohmann::json::parse(
        sshmcp::make_error(nlohmann::json{},
                           sshmcp::PARSE_ERROR, "bad"));
    t::expect(error["error"]["code"] == sshmcp::PARSE_ERROR,
              "error code");
    t::expect(error["error"]["message"] == "bad",
              "error message");
    t::expect(error["id"].is_null(), "null id");
});

}  // namespace
```

- [ ] **Step 2: Run to verify failure**

Expected: compile FAIL — `sshmcp/jsonrpc.hpp` not found.

- [ ] **Step 3: Implement**

`include/sshmcp/jsonrpc.hpp`:
```cpp
#pragma once

#include <sshmcp/types.hpp>

#include <nlohmann/json.hpp>

#include <expected>
#include <string>
#include <string_view>

namespace sshmcp {

inline constexpr auto PARSE_ERROR = -32700;
inline constexpr auto INVALID_REQUEST = -32600;
inline constexpr auto METHOD_NOT_FOUND = -32601;
inline constexpr auto INVALID_PARAMS = -32602;

struct request_t {
    nlohmann::json id;
    std::string method;
    nlohmann::json params;
    bool is_notification{};
};

inline auto dump_json(nlohmann::json const& value)
    -> std::string {
    return value.dump(
        -1, ' ', false,
        nlohmann::json::error_handler_t::replace);
}

inline auto parse_request(std::string_view line)
    -> std::expected<request_t, error_t> {
    auto const parsed =
        nlohmann::json::parse(line, nullptr, false);
    if (parsed.is_discarded()) {
        return std::unexpected{error_t{"invalid JSON"}};
    }
    if (!parsed.is_object() ||
        parsed.value("jsonrpc", "") != "2.0" ||
        !parsed.contains("method") ||
        !parsed["method"].is_string()) {
        return std::unexpected{
            error_t{"invalid JSON-RPC request"}};
    }
    auto request = request_t{};
    request.method = parsed["method"].get<std::string>();
    request.is_notification = !parsed.contains("id");
    if (!request.is_notification) {
        request.id = parsed["id"];
    }
    request.params =
        parsed.value("params", nlohmann::json::object());
    if (!request.params.is_object()) {
        request.params = nlohmann::json::object();
    }
    return request;
}

inline auto make_result(nlohmann::json const& id,
                        nlohmann::json const& result)
    -> std::string {
    return dump_json({{"jsonrpc", "2.0"},
                      {"id", id},
                      {"result", result}});
}

inline auto make_error(nlohmann::json const& id, int code,
                       std::string_view message)
    -> std::string {
    return dump_json(
        {{"jsonrpc", "2.0"},
         {"id", id},
         {"error", {{"code", code},
                    {"message", std::string{message}}}}});
}

}  // namespace sshmcp
```

- [ ] **Step 4: Run to verify pass**

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: non-throwing JSON-RPC parse and serialize"
```

---

### Task 7: Tools (exec, read_file, write_file)

**Files:**
- Create: `include/sshmcp/tools.hpp`,
  `regress/unit/test_tools.cpp`
- Modify: `CMakeLists.txt` (add test source)

**Interfaces:**
- Consumes: `context_t`, `spawn_request_t`, `tool_result_t`,
  `build_ssh_argv`, `exec_command`, `read_command`,
  `write_command`, `truncate_middle`, `dump_json`, constants.
- Produces: `exec_tool_t`, `read_file_tool_t`,
  `write_file_tool_t`, each with `static constexpr
  std::string_view NAME/DESCRIPTION`, `static schema() ->
  nlohmann::json`, `static invoke(context_t&, nlohmann::json
  const&) -> tool_result_t`; helper `error_result(std::string)
  -> tool_result_t`. When a spawn times out, exec reports
  `exit_code: -1` (normalized — the killed process's exit status
  is platform noise).

- [ ] **Step 1: Write failing tests**

`regress/unit/test_tools.cpp`:
```cpp
#include "harness.hpp"

#include <sshmcp/tools.hpp>

#include <nlohmann/json.hpp>

#include <expected>
#include <string>

namespace {

namespace t = sshmcp::test;

auto context_with(sshmcp::spawn_result_t reply,
                  sshmcp::spawn_request_t& captured)
    -> sshmcp::context_t {
    auto config = sshmcp::config_t{};
    config.target = "dev@host";
    return sshmcp::context_t{
        .config = config,
        .spawn = [reply = std::move(reply), &captured](
                     sshmcp::spawn_request_t const& request)
            -> std::expected<sshmcp::spawn_result_t,
                             sshmcp::error_t> {
            captured = request;
            return reply;
        }};
}

[[maybe_unused]] auto const t1 = t::add_test("exec ok", [] {
    auto captured = sshmcp::spawn_request_t{};
    auto context = context_with(
        {.stdout_text = "hi\n", .exit_code = 0}, captured);
    auto const result = sshmcp::exec_tool_t::invoke(
        context, {{"command", "echo hi"}});
    t::expect(!result.is_error, "not error");
    t::expect(captured.argv.back() == "echo hi", "command");
    t::expect(captured.argv.front() == "ssh", "exe");
    auto const body = nlohmann::json::parse(result.text);
    t::expect(body["stdout"] == "hi\n", "stdout");
    t::expect(body["exit_code"] == 0, "exit code");
    t::expect(body["is_timed_out"] == false, "no timeout");
});

[[maybe_unused]] auto const t2 = t::add_test("exec args", [] {
    auto captured = sshmcp::spawn_request_t{};
    auto context = context_with({.exit_code = 1}, captured);
    auto const missing =
        sshmcp::exec_tool_t::invoke(context, {});
    t::expect(missing.is_error, "missing command errors");

    auto const clamped = sshmcp::exec_tool_t::invoke(
        context, {{"command", "x"},
                  {"timeout_ms", 999999999}});
    t::expect(captured.timeout_ms == sshmcp::MAX_TIMEOUT_MS,
              "timeout clamped");
    t::expect(!clamped.is_error, "nonzero exit not error");

    sshmcp::exec_tool_t::invoke(
        context, {{"command", "make"}, {"cwd", "/src"}});
    t::expect(captured.argv.back() == "cd '/src' && make",
              "cwd prepended");
});

[[maybe_unused]] auto const t3 = t::add_test("exec timeout", [] {
    auto captured = sshmcp::spawn_request_t{};
    auto context = context_with(
        {.exit_code = 9, .is_timed_out = true}, captured);
    auto const result = sshmcp::exec_tool_t::invoke(
        context, {{"command", "sleep 99"}});
    auto const body = nlohmann::json::parse(result.text);
    t::expect(body["is_timed_out"] == true, "flag");
    t::expect(body["exit_code"] == -1,
              "timeout exit normalized");
});

[[maybe_unused]] auto const t4 = t::add_test("read_file", [] {
    auto captured = sshmcp::spawn_request_t{};
    auto context = context_with(
        {.stdout_text = "content", .exit_code = 0}, captured);
    auto const result = sshmcp::read_file_tool_t::invoke(
        context, {{"path", "/a b"}});
    t::expect(!result.is_error, "ok");
    t::expect(result.text == "content", "raw text");
    t::expect(captured.argv.back() == "cat '/a b'",
              "quoted");

    auto failing = context_with(
        {.stderr_text = "cat: no such file",
         .exit_code = 1},
        captured);
    auto const missing = sshmcp::read_file_tool_t::invoke(
        failing, {{"path", "/nope"}});
    t::expect(missing.is_error, "nonzero exit errors");
    t::expect(missing.text.contains("no such file"),
              "stderr surfaced");

    auto big = context_with(
        {.stdout_text =
             std::string(sshmcp::MAX_READ_BYTES + 1, 'x'),
         .exit_code = 0},
        captured);
    auto const too_big = sshmcp::read_file_tool_t::invoke(
        big, {{"path", "/big"}});
    t::expect(too_big.is_error, "size cap");
});

[[maybe_unused]] auto const t5 = t::add_test("write_file", [] {
    auto captured = sshmcp::spawn_request_t{};
    auto context = context_with({.exit_code = 0}, captured);
    auto const result = sshmcp::write_file_tool_t::invoke(
        context,
        {{"path", "/d/f.txt"}, {"content", "hi\n"}});
    t::expect(!result.is_error, "ok");
    t::expect(captured.stdin_data == "hi\n", "content piped");
    t::expect(captured.argv.back() ==
                  "mkdir -p '/d' && cat > '/d/f.txt'",
              "remote command");
    t::expect(result.text.contains("3 bytes"),
              "byte count reported");

    auto const no_content = sshmcp::write_file_tool_t::invoke(
        context, {{"path", "/x"}});
    t::expect(no_content.is_error, "content required");
});

}  // namespace
```

- [ ] **Step 2: Run to verify failure**

Expected: compile FAIL — `sshmcp/tools.hpp` not found.

- [ ] **Step 3: Implement**

`include/sshmcp/tools.hpp`:
```cpp
#pragma once

#include <sshmcp/jsonrpc.hpp>
#include <sshmcp/ssh.hpp>
#include <sshmcp/types.hpp>
#include <sshmcp/util.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <format>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace sshmcp {

inline auto error_result(std::string message)
    -> tool_result_t {
    return {.text = std::move(message), .is_error = true};
}

struct exec_tool_t {
    static constexpr auto NAME = std::string_view{"exec"};
    static constexpr auto DESCRIPTION = std::string_view{
        "Run a shell command on the remote host over ssh. "
        "Returns stdout, stderr and exit_code; a non-zero "
        "exit_code is a normal result, not an error."};

    static auto schema() -> nlohmann::json {
        return {{"type", "object"},
                {"properties",
                 {{"command",
                   {{"type", "string"},
                    {"description",
                     "shell command to run remotely"}}},
                  {"cwd",
                   {{"type", "string"},
                    {"description",
                     "remote directory to run in"}}},
                  {"timeout_ms",
                   {{"type", "integer"},
                    {"description",
                     "timeout in milliseconds "
                     "(default 60000, max 600000)"}}}}},
                {"required",
                 nlohmann::json::array({"command"})}};
    }

    static auto invoke(context_t& context,
                       nlohmann::json const& arguments)
        -> tool_result_t {
        if (!arguments.contains("command") ||
            !arguments["command"].is_string()) {
            return error_result(
                "exec: 'command' (string) is required");
        }
        auto const command =
            arguments["command"].get<std::string>();
        auto cwd = std::optional<std::string>{};
        if (arguments.contains("cwd") &&
            arguments["cwd"].is_string()) {
            cwd = arguments["cwd"].get<std::string>();
        }
        auto timeout_ms = DEFAULT_TIMEOUT_MS;
        if (arguments.contains("timeout_ms") &&
            arguments["timeout_ms"].is_number_integer()) {
            timeout_ms = std::clamp(
                arguments["timeout_ms"].get<std::int64_t>(),
                std::int64_t{1}, MAX_TIMEOUT_MS);
        }
        auto const spawned = context.spawn(spawn_request_t{
            .argv = build_ssh_argv(context.config,
                                   exec_command(command, cwd)),
            .stdin_data = {},
            .timeout_ms = timeout_ms});
        if (!spawned) {
            return error_result("exec: " +
                                spawned.error().message);
        }
        auto const out = truncate_middle(
            spawned->stdout_text, MAX_OUTPUT_CHARS);
        auto const err = truncate_middle(
            spawned->stderr_text, MAX_OUTPUT_CHARS);
        return {.text = dump_json(
                    {{"stdout", out.text},
                     {"stderr", err.text},
                     {"exit_code", spawned->is_timed_out
                                       ? -1
                                       : spawned->exit_code},
                     {"is_truncated", out.is_truncated ||
                                          err.is_truncated},
                     {"is_timed_out",
                      spawned->is_timed_out}}),
                .is_error = false};
    }
};

struct read_file_tool_t {
    static constexpr auto NAME =
        std::string_view{"read_file"};
    static constexpr auto DESCRIPTION = std::string_view{
        "Read a UTF-8 text file from the remote host "
        "(max 1 MiB)."};

    static auto schema() -> nlohmann::json {
        return {{"type", "object"},
                {"properties",
                 {{"path",
                   {{"type", "string"},
                    {"description",
                     "absolute or home-relative remote "
                     "path"}}}}},
                {"required",
                 nlohmann::json::array({"path"})}};
    }

    static auto invoke(context_t& context,
                       nlohmann::json const& arguments)
        -> tool_result_t {
        if (!arguments.contains("path") ||
            !arguments["path"].is_string()) {
            return error_result(
                "read_file: 'path' (string) is required");
        }
        auto const path =
            arguments["path"].get<std::string>();
        auto const spawned = context.spawn(spawn_request_t{
            .argv = build_ssh_argv(context.config,
                                   read_command(path)),
            .stdin_data = {},
            .timeout_ms = DEFAULT_TIMEOUT_MS});
        if (!spawned) {
            return error_result("read_file: " +
                                spawned.error().message);
        }
        if (spawned->is_timed_out) {
            return error_result("read_file: timed out");
        }
        if (spawned->exit_code != 0) {
            return error_result(
                "read_file failed: " +
                truncate_middle(spawned->stderr_text, 1000)
                    .text);
        }
        if (spawned->stdout_text.size() > MAX_READ_BYTES) {
            return error_result(std::format(
                "read_file: {} exceeds 1 MiB ({} bytes)",
                path, spawned->stdout_text.size()));
        }
        return {.text = spawned->stdout_text,
                .is_error = false};
    }
};

struct write_file_tool_t {
    static constexpr auto NAME =
        std::string_view{"write_file"};
    static constexpr auto DESCRIPTION = std::string_view{
        "Write exact bytes to a file on the remote host, "
        "creating parent directories."};

    static auto schema() -> nlohmann::json {
        return {{"type", "object"},
                {"properties",
                 {{"path",
                   {{"type", "string"},
                    {"description", "remote file path"}}},
                  {"content",
                   {{"type", "string"},
                    {"description",
                     "full file content to write"}}}}},
                {"required", nlohmann::json::array(
                                 {"path", "content"})}};
    }

    static auto invoke(context_t& context,
                       nlohmann::json const& arguments)
        -> tool_result_t {
        if (!arguments.contains("path") ||
            !arguments["path"].is_string()) {
            return error_result(
                "write_file: 'path' (string) is required");
        }
        if (!arguments.contains("content") ||
            !arguments["content"].is_string()) {
            return error_result(
                "write_file: 'content' (string) is "
                "required");
        }
        auto const path =
            arguments["path"].get<std::string>();
        auto content =
            arguments["content"].get<std::string>();
        auto const size = content.size();
        auto const spawned = context.spawn(spawn_request_t{
            .argv = build_ssh_argv(context.config,
                                   write_command(path)),
            .stdin_data = std::move(content),
            .timeout_ms = DEFAULT_TIMEOUT_MS});
        if (!spawned) {
            return error_result("write_file: " +
                                spawned.error().message);
        }
        if (spawned->is_timed_out) {
            return error_result("write_file: timed out");
        }
        if (spawned->exit_code != 0) {
            return error_result(
                "write_file failed: " +
                truncate_middle(spawned->stderr_text, 1000)
                    .text);
        }
        return {.text = std::format(
                    "wrote {} bytes to {}", size, path),
                .is_error = false};
    }
};

}  // namespace sshmcp
```

- [ ] **Step 4: Run to verify pass**

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: exec, read_file, write_file tools"
```

---

### Task 8: Concept + tool registry

**Files:**
- Create: `include/sshmcp/registry.hpp`,
  `regress/unit/test_registry.cpp`
- Modify: `CMakeLists.txt` (add test source)

**Interfaces:**
- Consumes: the three tool structs (Task 7), `context_t`,
  `tool_result_t`.
- Produces: concept `tool` (requires `NAME`, `DESCRIPTION`,
  `schema()`, `invoke()`); `tool_set_t<ToolTypes...>` with
  `static list() -> nlohmann::json` (array of
  `{name, description, inputSchema}`) and `static call(context_t&,
  std::string_view, nlohmann::json const&) ->
  std::optional<tool_result_t>` (nullopt = unknown tool name).

- [ ] **Step 1: Write failing tests**

`regress/unit/test_registry.cpp`:
```cpp
#include "harness.hpp"

#include <sshmcp/registry.hpp>
#include <sshmcp/tools.hpp>

#include <nlohmann/json.hpp>

#include <expected>

namespace {

namespace t = sshmcp::test;

static_assert(sshmcp::tool<sshmcp::exec_tool_t>);
static_assert(sshmcp::tool<sshmcp::read_file_tool_t>);
static_assert(sshmcp::tool<sshmcp::write_file_tool_t>);

using tools_t = sshmcp::tool_set_t<sshmcp::exec_tool_t,
                                   sshmcp::read_file_tool_t,
                                   sshmcp::write_file_tool_t>;

[[maybe_unused]] auto const t1 = t::add_test("list", [] {
    auto const tools = tools_t::list();
    t::expect(tools.size() == 3, "three tools");
    t::expect(tools[0]["name"] == "exec", "exec first");
    t::expect(tools[1]["name"] == "read_file", "read");
    t::expect(tools[2]["name"] == "write_file", "write");
    t::expect(tools[0].contains("inputSchema"), "schema");
    t::expect(tools[0].contains("description"),
              "description");
});

[[maybe_unused]] auto const t2 = t::add_test("dispatch", [] {
    auto context = sshmcp::context_t{
        .config = {},
        .spawn = [](sshmcp::spawn_request_t const&)
            -> std::expected<sshmcp::spawn_result_t,
                             sshmcp::error_t> {
            return sshmcp::spawn_result_t{
                .stdout_text = "ok\n", .exit_code = 0};
        }};
    auto const known = tools_t::call(
        context, "exec", {{"command", "true"}});
    t::expect(known.has_value(), "known dispatches");
    t::expect(!known->is_error, "invoked");

    auto const unknown =
        tools_t::call(context, "nope", {});
    t::expect(!unknown.has_value(), "unknown is nullopt");
});

}  // namespace
```

- [ ] **Step 2: Run to verify failure**

Expected: compile FAIL — `sshmcp/registry.hpp` not found.

- [ ] **Step 3: Implement**

`include/sshmcp/registry.hpp`:
```cpp
#pragma once

#include <sshmcp/types.hpp>

#include <nlohmann/json.hpp>

#include <concepts>
#include <optional>
#include <string_view>

namespace sshmcp {

template<typename ToolType>
concept tool = requires(context_t& context,
                        nlohmann::json const& arguments) {
    { ToolType::NAME }
        -> std::convertible_to<std::string_view>;
    { ToolType::DESCRIPTION }
        -> std::convertible_to<std::string_view>;
    { ToolType::schema() }
        -> std::same_as<nlohmann::json>;
    { ToolType::invoke(context, arguments) }
        -> std::same_as<tool_result_t>;
};

template<tool... ToolTypes>
class tool_set_t {
public:
    static auto list() -> nlohmann::json {
        auto tools = nlohmann::json::array();
        (tools.push_back(
             {{"name", ToolTypes::NAME},
              {"description", ToolTypes::DESCRIPTION},
              {"inputSchema", ToolTypes::schema()}}),
         ...);
        return tools;
    }

    static auto call(context_t& context,
                     std::string_view name,
                     nlohmann::json const& arguments)
        -> std::optional<tool_result_t> {
        auto result = std::optional<tool_result_t>{};
        ((name == ToolTypes::NAME
              ? (result = ToolTypes::invoke(context,
                                            arguments),
                 true)
              : false) ||
         ...);
        return result;
    }
};

}  // namespace sshmcp
```

- [ ] **Step 4: Run to verify pass**

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: tool concept and variadic registry"
```

---

### Task 9: MCP server loop

**Files:**
- Create: `include/sshmcp/mcp.hpp`, `regress/unit/test_mcp.cpp`
- Modify: `CMakeLists.txt` (add test source)

**Interfaces:**
- Consumes: `parse_request`, `make_result`, `make_error`, error
  codes, `tool_set_t`, `tool` concept, `context_t`, `VERSION`
  (generated `sshmcp/version.hpp`).
- Produces: `PROTOCOL_VERSION = "2024-11-05"`;
  `server_t<ToolTypes...>` with `explicit server_t(context_t)`,
  `handle_line(std::string_view) -> std::optional<std::string>`
  (nullopt for notifications), `run(std::istream&, std::ostream&)
  -> int` (getline loop, strips trailing `\r`, skips blank lines,
  flushes after each reply).

- [ ] **Step 1: Write failing tests**

`regress/unit/test_mcp.cpp`:
```cpp
#include "harness.hpp"

#include <sshmcp/mcp.hpp>
#include <sshmcp/tools.hpp>
#include <sshmcp/version.hpp>

#include <nlohmann/json.hpp>

#include <expected>
#include <sstream>
#include <string>

namespace {

namespace t = sshmcp::test;

using server_t = sshmcp::server_t<sshmcp::exec_tool_t,
                                  sshmcp::read_file_tool_t,
                                  sshmcp::write_file_tool_t>;

auto test_server() -> server_t {
    return server_t{sshmcp::context_t{
        .config = {},
        .spawn = [](sshmcp::spawn_request_t const&)
            -> std::expected<sshmcp::spawn_result_t,
                             sshmcp::error_t> {
            return sshmcp::spawn_result_t{
                .stdout_text = "out\n", .exit_code = 0};
        }}};
}

[[maybe_unused]] auto const t1 = t::add_test("initialize", [] {
    auto server = test_server();
    auto const reply = server.handle_line(
        R"({"jsonrpc":"2.0","id":0,"method":"initialize",)"
        R"("params":{"protocolVersion":"2025-06-18"}})");
    t::expect(reply.has_value(), "replies");
    auto const body = nlohmann::json::parse(*reply);
    t::expect(body["result"]["serverInfo"]["name"] ==
                  "sshmcp",
              "server name");
    t::expect(body["result"]["serverInfo"]["version"] ==
                  sshmcp::VERSION,
              "version from header");
    t::expect(body["result"]["protocolVersion"] ==
                  "2025-06-18",
              "echoes client protocol");
    t::expect(body["result"]["capabilities"]
                  .contains("tools"),
              "tools capability");
});

[[maybe_unused]] auto const t2 = t::add_test("lifecycle", [] {
    auto server = test_server();
    t::expect(!server
                   .handle_line(
                       R"({"jsonrpc":"2.0","method":)"
                       R"("notifications/initialized"})")
                   .has_value(),
              "notification silent");
    auto const pong = server.handle_line(
        R"({"jsonrpc":"2.0","id":9,"method":"ping"})");
    t::expect(nlohmann::json::parse(*pong)["result"]
                  .is_object(),
              "ping empty object");
});

[[maybe_unused]] auto const t3 = t::add_test("tools flow", [] {
    auto server = test_server();
    auto const list = server.handle_line(
        R"({"jsonrpc":"2.0","id":1,"method":"tools/list"})");
    auto const tools =
        nlohmann::json::parse(*list)["result"]["tools"];
    t::expect(tools.size() == 3, "three tools listed");

    auto const call = server.handle_line(
        R"({"jsonrpc":"2.0","id":2,"method":"tools/call",)"
        R"("params":{"name":"exec",)"
        R"("arguments":{"command":"ls"}}})");
    auto const body = nlohmann::json::parse(*call);
    t::expect(body["result"]["isError"] == false,
              "call ok");
    t::expect(body["result"]["content"][0]["type"] ==
                  "text",
              "text content");

    auto const unknown = server.handle_line(
        R"({"jsonrpc":"2.0","id":3,"method":"tools/call",)"
        R"("params":{"name":"nope","arguments":{}}})");
    t::expect(nlohmann::json::parse(
                  *unknown)["error"]["code"] ==
                  sshmcp::INVALID_PARAMS,
              "unknown tool errors");
});

[[maybe_unused]] auto const t4 = t::add_test("errors", [] {
    auto server = test_server();
    auto const bad = server.handle_line("garbage");
    t::expect(nlohmann::json::parse(*bad)["error"]["code"] ==
                  sshmcp::PARSE_ERROR,
              "parse error");
    auto const missing = server.handle_line(
        R"({"jsonrpc":"2.0","id":4,"method":"wat"})");
    t::expect(nlohmann::json::parse(
                  *missing)["error"]["code"] ==
                  sshmcp::METHOD_NOT_FOUND,
              "method not found");
});

[[maybe_unused]] auto const t5 = t::add_test("run loop", [] {
    auto server = test_server();
    auto input = std::istringstream{
        "{\"jsonrpc\":\"2.0\",\"id\":0,"
        "\"method\":\"ping\"}\r\n"
        "\n"
        "{\"jsonrpc\":\"2.0\",\"id\":1,"
        "\"method\":\"ping\"}\n"};
    auto output = std::ostringstream{};
    auto const code = server.run(input, output);
    t::expect(code == 0, "clean exit");
    auto const text = output.str();
    t::expect(std::count(text.begin(), text.end(), '\n') ==
                  2,
              "two reply lines");
});

}  // namespace
```

Also add `#include <algorithm>` to the test includes for
`std::count`.

- [ ] **Step 2: Run to verify failure**

Expected: compile FAIL — `sshmcp/mcp.hpp` not found.

- [ ] **Step 3: Implement**

`include/sshmcp/mcp.hpp`:
```cpp
#pragma once

#include <sshmcp/jsonrpc.hpp>
#include <sshmcp/registry.hpp>
#include <sshmcp/types.hpp>
#include <sshmcp/version.hpp>

#include <nlohmann/json.hpp>

#include <istream>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <utility>

namespace sshmcp {

inline constexpr auto PROTOCOL_VERSION =
    std::string_view{"2024-11-05"};

template<tool... ToolTypes>
class server_t {
public:
    explicit server_t(context_t context)
        : context{std::move(context)} {}

    auto handle_line(std::string_view line)
        -> std::optional<std::string> {
        auto request = parse_request(line);
        if (!request) {
            return make_error(nlohmann::json{}, PARSE_ERROR,
                              request.error().message);
        }
        if (request->is_notification) {
            return std::nullopt;
        }
        if (request->method == "initialize") {
            return handle_initialize(*request);
        }
        if (request->method == "ping") {
            return make_result(request->id,
                               nlohmann::json::object());
        }
        if (request->method == "tools/list") {
            return make_result(
                request->id,
                {{"tools", tool_set_t<ToolTypes...>::list()}});
        }
        if (request->method == "tools/call") {
            return handle_call(*request);
        }
        return make_error(
            request->id, METHOD_NOT_FOUND,
            "method not found: " + request->method);
    }

    auto run(std::istream& in, std::ostream& out) -> int {
        auto line = std::string{};
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                continue;
            }
            if (auto const reply = handle_line(line)) {
                out << *reply << '\n' << std::flush;
            }
        }
        return 0;
    }

private:
    auto handle_initialize(request_t const& request)
        -> std::string {
        auto protocol = std::string{PROTOCOL_VERSION};
        if (request.params.contains("protocolVersion") &&
            request.params["protocolVersion"].is_string()) {
            protocol = request.params["protocolVersion"]
                           .get<std::string>();
        }
        return make_result(
            request.id,
            {{"protocolVersion", protocol},
             {"capabilities",
              {{"tools", nlohmann::json::object()}}},
             {"serverInfo",
              {{"name", "sshmcp"},
               {"version", VERSION}}}});
    }

    auto handle_call(request_t const& request)
        -> std::string {
        if (!request.params.contains("name") ||
            !request.params["name"].is_string()) {
            return make_error(request.id, INVALID_PARAMS,
                              "tools/call: 'name' required");
        }
        auto const name =
            request.params["name"].get<std::string>();
        auto arguments = request.params.value(
            "arguments", nlohmann::json::object());
        if (!arguments.is_object()) {
            arguments = nlohmann::json::object();
        }
        auto const result = tool_set_t<ToolTypes...>::call(
            context, name, arguments);
        if (!result) {
            return make_error(request.id, INVALID_PARAMS,
                              "unknown tool: " + name);
        }
        return make_result(
            request.id,
            {{"content",
              nlohmann::json::array(
                  {{{"type", "text"},
                    {"text", result->text}}})},
             {"isError", result->is_error}});
    }

    context_t context;
};

}  // namespace sshmcp
```

- [ ] **Step 4: Run to verify pass**

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "feat: MCP stdio server loop"
```

---

### Task 10: CRTP subprocess platform layer

**Files:**
- Create: `include/sshmcp/subprocess_base.hpp`,
  `include/sshmcp/subprocess.hpp`,
  `include/sshmcp/platform/windows/impl.hpp`,
  `include/sshmcp/platform/posix_common.hpp`,
  `include/sshmcp/platform/linux/impl.hpp`,
  `include/sshmcp/platform/openbsd/impl.hpp`,
  `regress/unit/platform/windows/test_impl.cpp`,
  `regress/unit/platform/linux/test_impl.cpp`,
  `regress/unit/platform/openbsd/test_impl.cpp`
- Modify: `CMakeLists.txt` — add
  `regress/unit/platform/${SSHMCP_PLATFORM}/test_impl.cpp` to
  `sshmcp_unit` sources (path selection via variable, no
  condition).

**Interfaces:**
- Consumes: `spawn_request_t`, `spawn_result_t`, `error_t`,
  `log_level_t`.
- Produces: `subprocess_base_t<Derived>` with
  `explicit subprocess_base_t(log_level_t)`, `init_stdio() ->
  void`, `run(spawn_request_t const&) ->
  std::expected<spawn_result_t, error_t>` (logs argv at DEBUG,
  duration + exit at INFO, delegates to `Derived::spawn_impl` /
  `Derived::init_stdio_impl`). Each platform defines
  `platform_impl_t` deriving from it. `subprocess.hpp` includes
  base then `<impl.hpp>` (resolved via preset include path).
  Windows impl also exposes pure helpers `utf8_to_wide`,
  `quote_win_arg`, `build_command_line` for unit tests.

- [ ] **Step 1: Write failing platform test (windows)**

`regress/unit/platform/windows/test_impl.cpp`:
```cpp
#include "harness.hpp"

#include <sshmcp/subprocess.hpp>

#include <string>

namespace {

namespace t = sshmcp::test;

[[maybe_unused]] auto const t1 = t::add_test("win quoting", [] {
    t::expect(sshmcp::quote_win_arg(L"plain") == L"plain",
              "no quotes needed");
    t::expect(sshmcp::quote_win_arg(L"a b") == L"\"a b\"",
              "spaces quoted");
    t::expect(sshmcp::quote_win_arg(L"a\"b") ==
                  L"\"a\\\"b\"",
              "quote escaped");
    t::expect(sshmcp::quote_win_arg(L"end\\") ==
                  L"\"end\\\\\"",
              "trailing backslash doubled");
    t::expect(sshmcp::quote_win_arg(L"") == L"\"\"",
              "empty quoted");
});

[[maybe_unused]] auto const t2 = t::add_test("spawn echo", [] {
    auto impl =
        sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run(
        {.argv = {"cmd", "/c", "echo hi"},
         .stdin_data = {},
         .timeout_ms = 30'000});
    t::expect(result.has_value(), "spawn ok");
    t::expect(result->exit_code == 0, "exit 0");
    t::expect(result->stdout_text.contains("hi"), "stdout");
    t::expect(!result->is_timed_out, "no timeout");
});

[[maybe_unused]] auto const t3 = t::add_test("spawn stdin", [] {
    auto impl =
        sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run(
        {.argv = {"findstr", "x"},
         .stdin_data = "axb\nqqq\n",
         .timeout_ms = 30'000});
    t::expect(result.has_value(), "spawn ok");
    t::expect(result->stdout_text.contains("axb"),
              "stdin consumed");
});

[[maybe_unused]] auto const t4 = t::add_test("spawn timeout", [] {
    auto impl =
        sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run(
        {.argv = {"cmd", "/c", "ping -n 30 127.0.0.1 >nul"},
         .stdin_data = {},
         .timeout_ms = 500});
    t::expect(result.has_value(), "spawn ok");
    t::expect(result->is_timed_out, "timed out");
});

[[maybe_unused]] auto const t5 = t::add_test("spawn missing", [] {
    auto impl =
        sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run(
        {.argv = {"definitely-not-a-real-exe-xyz"},
         .stdin_data = {},
         .timeout_ms = 5'000});
    t::expect(!result.has_value(), "spawn fails cleanly");
});

}  // namespace
```

- [ ] **Step 2: Wire test source + verify failure**

In `CMakeLists.txt` add to `sshmcp_unit` sources:
```cmake
    regress/unit/platform/${SSHMCP_PLATFORM}/test_impl.cpp
```
Run build. Expected: FAIL — `sshmcp/subprocess.hpp` not found.

- [ ] **Step 3: Implement base + dispatcher**

`include/sshmcp/subprocess_base.hpp`:
```cpp
#pragma once

#include <sshmcp/types.hpp>

#include <chrono>
#include <expected>
#include <print>
#include <string>

namespace sshmcp {

template<typename Derived>
class subprocess_base_t {
public:
    explicit subprocess_base_t(log_level_t log_level)
        : log_level{log_level} {}

    auto init_stdio() -> void {
        static_cast<Derived*>(this)->init_stdio_impl();
    }

    auto run(spawn_request_t const& request)
        -> std::expected<spawn_result_t, error_t> {
        if (log_level == log_level_t::DEBUG) {
            auto line = std::string{};
            for (auto const& arg : request.argv) {
                line += arg;
                line += ' ';
            }
            std::println(stderr, "sshmcp: spawn: {}", line);
        }
        auto const start =
            std::chrono::steady_clock::now();
        auto result =
            static_cast<Derived*>(this)->spawn_impl(request);
        auto const elapsed_ms =
            std::chrono::duration_cast<
                std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start)
                .count();
        if (log_level != log_level_t::OFF) {
            if (result) {
                std::println(
                    stderr, "sshmcp: exit {} in {} ms{}",
                    result->exit_code, elapsed_ms,
                    result->is_timed_out ? " (timeout)"
                                         : "");
            } else {
                std::println(stderr,
                             "sshmcp: spawn error: {}",
                             result.error().message);
            }
        }
        return result;
    }

private:
    log_level_t log_level;
};

}  // namespace sshmcp
```

`include/sshmcp/subprocess.hpp`:
```cpp
#pragma once

#include <sshmcp/subprocess_base.hpp>

#include <impl.hpp>
```

- [ ] **Step 4: Implement windows impl**

`include/sshmcp/platform/windows/impl.hpp`
(`WIN32_LEAN_AND_MEAN`, `NOMINMAX`, `NOGDI` come from preset
flags — zero defines here):
```cpp
#pragma once

#include <sshmcp/subprocess_base.hpp>
#include <sshmcp/types.hpp>

#include <windows.h>

#include <fcntl.h>
#include <io.h>

#include <cstdio>
#include <expected>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace sshmcp {

inline auto utf8_to_wide(std::string_view text)
    -> std::wstring {
    if (text.empty()) {
        return {};
    }
    auto const size = MultiByteToWideChar(
        CP_UTF8, 0, text.data(),
        static_cast<int>(text.size()), nullptr, 0);
    auto wide =
        std::wstring(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(),
                        static_cast<int>(text.size()),
                        wide.data(), size);
    return wide;
}

inline auto quote_win_arg(std::wstring_view arg)
    -> std::wstring {
    if (!arg.empty() &&
        arg.find_first_of(L" \t\n\v\"") ==
            std::wstring_view::npos) {
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

inline auto build_command_line(
    std::vector<std::string> const& argv) -> std::wstring {
    auto line = std::wstring{};
    for (auto const& arg : argv) {
        if (!line.empty()) {
            line.push_back(L' ');
        }
        line += quote_win_arg(utf8_to_wide(arg));
    }
    return line;
}

class platform_impl_t
    : public subprocess_base_t<platform_impl_t> {
public:
    using subprocess_base_t<
        platform_impl_t>::subprocess_base_t;

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
            !CreatePipe(&out_read, &out_write, &security,
                        0) ||
            !CreatePipe(&err_read, &err_write, &security,
                        0)) {
            return std::unexpected{
                error_t{"CreatePipe failed"}};
        }
        SetHandleInformation(in_write,
                             HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(out_read,
                             HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(err_read,
                             HANDLE_FLAG_INHERIT, 0);
        auto startup = STARTUPINFOW{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = in_read;
        startup.hStdOutput = out_write;
        startup.hStdError = err_write;
        auto process = PROCESS_INFORMATION{};
        auto line = build_command_line(request.argv);
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
            return std::unexpected{error_t{
                "CreateProcess failed: " +
                request.argv.front()}};
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
                        in_write,
                        request.stdin_data.data() + offset,
                        static_cast<DWORD>(
                            request.stdin_data.size() -
                            offset),
                        &written, nullptr)) {
                    break;
                }
                offset += written;
            }
            CloseHandle(in_write);
        }};
        auto stdout_text = std::string{};
        auto stderr_text = std::string{};
        auto const read_all =
            [](HANDLE handle) -> std::string {
            auto text = std::string{};
            char buffer[4096];
            auto count = DWORD{};
            while (ReadFile(handle, buffer, sizeof(buffer),
                            &count, nullptr) &&
                   count > 0) {
                text.append(buffer, count);
            }
            return text;
        };
        auto out_reader = std::jthread{
            [&] { stdout_text = read_all(out_read); }};
        auto err_reader = std::jthread{
            [&] { stderr_text = read_all(err_read); }};
        auto const wait = WaitForSingleObject(
            process.hProcess,
            static_cast<DWORD>(request.timeout_ms));
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
        return spawn_result_t{
            .stdout_text = std::move(stdout_text),
            .stderr_text = std::move(stderr_text),
            .exit_code = static_cast<int>(exit_code),
            .is_timed_out = is_timed_out};
    }
};

}  // namespace sshmcp
```

- [ ] **Step 5: Implement posix common + linux + openbsd**

`include/sshmcp/platform/posix_common.hpp`:
```cpp
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
#include <vector>

namespace sshmcp {

inline auto posix_spawn_capture(
    spawn_request_t const& request)
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
            argv_c.push_back(
                const_cast<char*>(arg.c_str()));
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
        if (in_open &&
            (fds[0].revents & (POLLOUT | POLLERR))) {
            auto const n = write(
                in_pipe[1],
                request.stdin_data.data() + stdin_offset,
                request.stdin_data.size() - stdin_offset);
            if (n <= 0 ||
                stdin_offset +
                        static_cast<std::size_t>(n) >=
                    request.stdin_data.size()) {
                close(in_pipe[1]);
                in_open = false;
            }
            if (n > 0) {
                stdin_offset +=
                    static_cast<std::size_t>(n);
            }
        }
        if (out_open &&
            (fds[1].revents & (POLLIN | POLLHUP))) {
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
        if (err_open &&
            (fds[2].revents & (POLLIN | POLLHUP))) {
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
```

`include/sshmcp/platform/linux/impl.hpp` and
`include/sshmcp/platform/openbsd/impl.hpp` (identical content for
MVP; OpenBSD grows pledge/unveil in v2):
```cpp
#pragma once

#include <sshmcp/platform/posix_common.hpp>
#include <sshmcp/subprocess_base.hpp>
#include <sshmcp/types.hpp>

#include <signal.h>

#include <expected>

namespace sshmcp {

class platform_impl_t
    : public subprocess_base_t<platform_impl_t> {
public:
    using subprocess_base_t<
        platform_impl_t>::subprocess_base_t;

    auto init_stdio_impl() -> void {
        signal(SIGPIPE, SIG_IGN);
    }

    auto spawn_impl(spawn_request_t const& request)
        -> std::expected<spawn_result_t, error_t> {
        return posix_spawn_capture(request);
    }
};

}  // namespace sshmcp
```

`regress/unit/platform/linux/test_impl.cpp` and
`regress/unit/platform/openbsd/test_impl.cpp` (identical):
```cpp
#include "harness.hpp"

#include <sshmcp/subprocess.hpp>

#include <string>

namespace {

namespace t = sshmcp::test;

[[maybe_unused]] auto const t1 = t::add_test("spawn echo", [] {
    auto impl =
        sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run(
        {.argv = {"/bin/sh", "-c", "echo hi"},
         .stdin_data = {},
         .timeout_ms = 30'000});
    t::expect(result.has_value(), "spawn ok");
    t::expect(result->exit_code == 0, "exit 0");
    t::expect(result->stdout_text == "hi\n", "stdout");
});

[[maybe_unused]] auto const t2 = t::add_test("spawn stdin", [] {
    auto impl =
        sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run(
        {.argv = {"/bin/sh", "-c", "cat"},
         .stdin_data = "roundtrip",
         .timeout_ms = 30'000});
    t::expect(result.has_value(), "spawn ok");
    t::expect(result->stdout_text == "roundtrip",
              "stdin piped");
});

[[maybe_unused]] auto const t3 = t::add_test("spawn exit", [] {
    auto impl =
        sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run(
        {.argv = {"/bin/sh", "-c", "exit 3"},
         .stdin_data = {},
         .timeout_ms = 30'000});
    t::expect(result->exit_code == 3, "exit code");
});

[[maybe_unused]] auto const t4 = t::add_test("spawn timeout", [] {
    auto impl =
        sshmcp::platform_impl_t{sshmcp::log_level_t::OFF};
    auto const result = impl.run(
        {.argv = {"/bin/sh", "-c", "sleep 30"},
         .stdin_data = {},
         .timeout_ms = 500});
    t::expect(result->is_timed_out, "timed out");
});

}  // namespace
```

- [ ] **Step 6: Run to verify pass (windows locally)**

Run: build + `ctest --preset windows-msvc-debug`
Expected: PASS including the four windows platform cases (the
timeout case takes ~0.5 s). Linux/openbsd files are
compile-verified in CI (Task 14).

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "feat: CRTP subprocess layer for windows/linux/openbsd"
```

---

### Task 11: App wiring

**Files:**
- Modify: `app/main.cpp` (replace stub)

**Interfaces:**
- Consumes: `load_config`, `platform_impl_t`, `server_t`, the
  three tools.

- [ ] **Step 1: Replace stub**

`app/main.cpp`:
```cpp
#include <sshmcp/config.hpp>
#include <sshmcp/mcp.hpp>
#include <sshmcp/subprocess.hpp>
#include <sshmcp/tools.hpp>

#include <cstdlib>
#include <iostream>
#include <print>
#include <string>
#include <utility>
#include <vector>

auto main(int argc, char** argv) -> int {
    auto args = std::vector<std::string>{};
    for (auto i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }
    auto const config = sshmcp::load_config(
        [](char const* name) -> char const* {
            return std::getenv(name);
        },
        args);
    if (!config) {
        std::println(stderr, "sshmcp: {}",
                     config.error().message);
        return 2;
    }
    auto impl =
        sshmcp::platform_impl_t{config->log_level};
    impl.init_stdio();
    auto server = sshmcp::server_t<
        sshmcp::exec_tool_t, sshmcp::read_file_tool_t,
        sshmcp::write_file_tool_t>{sshmcp::context_t{
        .config = *config,
        .spawn =
            [&impl](sshmcp::spawn_request_t const& request) {
                return impl.run(request);
            }}};
    return server.run(std::cin, std::cout);
}
```

- [ ] **Step 2: Build + manual smoke**

Run: `cmake --build --preset windows-msvc-debug`, then from Git
Bash:
```bash
printf '%s\n' \
  '{"jsonrpc":"2.0","id":0,"method":"initialize","params":{}}' \
  | SSHMCP_TARGET=dev@example SSHMCP_LOG=off \
    ./build/windows-msvc-debug/sshmcp.exe
```
Expected: one JSON line with `"name":"sshmcp"` and the current
version; exit code 0. Also run with no `SSHMCP_TARGET` and no
args: expect exit 2 and a clear stderr message.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "feat: wire config, platform impl and server in main"
```

---

### Task 12: E2E regress harness (fake ssh)

**Files:**
- Create: `regress/e2e/fake_ssh.py`, `regress/e2e/driver.py`,
  `regress/e2e/cases/basic/transcript.jsonl`,
  `regress/e2e/cases/basic/replies.jsonl`,
  `regress/e2e/cases/basic/golden.jsonl` (blessed),
  `regress/e2e/cases/basic/expected_calls.jsonl`,
  `regress/e2e/cases/errors/*` (same four files),
  `regress/e2e/cases/timeout/*` (same four files)
- Modify: `CMakeLists.txt` (three `add_test` rows)

**Interfaces:**
- Consumes: built `sshmcp_app` binary; `SSHMCP_SSH_EXE`
  quote-aware split (Task 5).
- Produces: ctest jobs `e2e_basic`, `e2e_errors`,
  `e2e_timeout`. Driver usage:
  `python driver.py <binary> <case_dir> [--bless]`.

- [ ] **Step 1: fake_ssh.py**

```python
"""Records argv+stdin, replays canned replies in order."""
import json
import os
import sys
import time


def main() -> int:
    log_path = os.environ["FAKE_SSH_LOG"]
    replies_path = os.environ["FAKE_SSH_REPLIES"]
    stdin_data = sys.stdin.buffer.read().decode("utf-8")
    with open(log_path, "a", encoding="utf-8") as log:
        log.write(json.dumps(
            {"argv": sys.argv[1:], "stdin": stdin_data}) + "\n")
    with open(replies_path, encoding="utf-8") as f:
        replies = [json.loads(x) for x in f if x.strip()]
    index_path = replies_path + ".index"
    index = 0
    if os.path.exists(index_path):
        with open(index_path, encoding="utf-8") as f:
            index = int(f.read().strip() or 0)
    with open(index_path, "w", encoding="utf-8") as f:
        f.write(str(index + 1))
    if index < len(replies):
        reply = replies[index]
    else:
        reply = {"stdout": "", "stderr": "fake_ssh: no reply",
                 "exit": 1}
    time.sleep(reply.get("sleep_ms", 0) / 1000.0)
    sys.stdout.write(reply.get("stdout", ""))
    sys.stderr.write(reply.get("stderr", ""))
    return int(reply.get("exit", 0))


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: driver.py**

```python
"""Feeds a JSON-RPC transcript to sshmcp, diffs vs golden."""
import json
import os
import subprocess
import sys
import tempfile
from pathlib import Path


def load_jsonl(path: Path) -> list:
    if not path.exists():
        return []
    return [json.loads(x)
            for x in path.read_text(encoding="utf-8").splitlines()
            if x.strip()]


def main() -> int:
    binary = Path(sys.argv[1])
    case_dir = Path(sys.argv[2])
    bless = "--bless" in sys.argv[3:]
    fake_ssh = Path(__file__).parent / "fake_ssh.py"
    with tempfile.TemporaryDirectory() as tmp:
        log_path = Path(tmp) / "calls.jsonl"
        replies_path = Path(tmp) / "replies.jsonl"
        replies_path.write_text(
            (case_dir / "replies.jsonl")
            .read_text(encoding="utf-8"),
            encoding="utf-8")
        env = dict(os.environ)
        env.update({
            "SSHMCP_TARGET": "dev@example",
            "SSHMCP_SSH_EXE":
                f'"{sys.executable}" "{fake_ssh}"',
            "SSHMCP_LOG": "off",
            "FAKE_SSH_LOG": str(log_path),
            "FAKE_SSH_REPLIES": str(replies_path),
        })
        transcript = (case_dir / "transcript.jsonl") \
            .read_text(encoding="utf-8")
        proc = subprocess.run(
            [str(binary)], input=transcript.encode("utf-8"),
            capture_output=True, env=env, timeout=120)
        got_lines = [x for x in
                     proc.stdout.decode("utf-8").splitlines()
                     if x.strip()]
        got = [json.loads(x) for x in got_lines]
        calls = load_jsonl(log_path)
        if bless:
            (case_dir / "golden.jsonl").write_text(
                "\n".join(got_lines) + "\n",
                encoding="utf-8")
            (case_dir / "expected_calls.jsonl").write_text(
                "".join(json.dumps(c) + "\n" for c in calls),
                encoding="utf-8")
            print(f"blessed {case_dir}")
            return 0
        ok = True
        want = load_jsonl(case_dir / "golden.jsonl")
        if got != want:
            ok = False
            print("response mismatch")
            print("got: ", json.dumps(got, indent=2))
            print("want:", json.dumps(want, indent=2))
        want_calls = load_jsonl(
            case_dir / "expected_calls.jsonl")
        if calls != want_calls:
            ok = False
            print("ssh call mismatch")
            print("got: ", json.dumps(calls, indent=2))
            print("want:", json.dumps(want_calls, indent=2))
        return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 3: Case inputs**

`regress/e2e/cases/basic/transcript.jsonl`:
```
{"jsonrpc":"2.0","id":0,"method":"initialize","params":{"protocolVersion":"2024-11-05","capabilities":{},"clientInfo":{"name":"regress","version":"0"}}}
{"jsonrpc":"2.0","method":"notifications/initialized"}
{"jsonrpc":"2.0","id":1,"method":"tools/list"}
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"exec","arguments":{"command":"echo hello"}}}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"write_file","arguments":{"path":"/tmp/a b.txt","content":"hi\n"}}}
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"read_file","arguments":{"path":"/tmp/a b.txt"}}}
{"jsonrpc":"2.0","id":5,"method":"ping"}
```

`regress/e2e/cases/basic/replies.jsonl` (one per tools/call, in
order):
```
{"stdout":"hello\n","stderr":"","exit":0}
{"stdout":"","stderr":"","exit":0}
{"stdout":"hi\n","stderr":"","exit":0}
```

`regress/e2e/cases/errors/transcript.jsonl`:
```
{"jsonrpc":"2.0","id":0,"method":"initialize","params":{}}
not json at all
{"jsonrpc":"2.0","id":1,"method":"no/such/method"}
{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"nope","arguments":{}}}
{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"exec","arguments":{}}}
{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"exec","arguments":{"command":"false"}}}
{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"read_file","arguments":{"path":"/nope"}}}
```

`regress/e2e/cases/errors/replies.jsonl`:
```
{"stdout":"","stderr":"","exit":1}
{"stdout":"","stderr":"cat: /nope: No such file or directory\n","exit":1}
```

`regress/e2e/cases/timeout/transcript.jsonl`:
```
{"jsonrpc":"2.0","id":0,"method":"initialize","params":{}}
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"exec","arguments":{"command":"sleep 30","timeout_ms":500}}}
```

`regress/e2e/cases/timeout/replies.jsonl`:
```
{"stdout":"late\n","stderr":"","exit":0,"sleep_ms":10000}
```

- [ ] **Step 4: ctest wiring**

Append to `CMakeLists.txt`:
```cmake
add_test(NAME e2e_basic COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/driver.py
    $<TARGET_FILE:sshmcp_app>
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/cases/basic)
add_test(NAME e2e_errors COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/driver.py
    $<TARGET_FILE:sshmcp_app>
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/cases/errors)
add_test(NAME e2e_timeout COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/driver.py
    $<TARGET_FILE:sshmcp_app>
    ${CMAKE_CURRENT_SOURCE_DIR}/regress/e2e/cases/timeout)
```

- [ ] **Step 5: Bless goldens (deliberate inspection step)**

For each case, run the driver with `--bless`, then MANUALLY READ
the produced `golden.jsonl` and `expected_calls.jsonl` and verify
every field against the spec (§4 tool behavior, §3 argv shape:
`-T -o BatchMode=yes dev@example <command>`; timeout case must
show `"is_timed_out": true` and `"exit_code": -1`; write case
stdin must be exactly `hi\n`). Fix code if anything is wrong,
re-bless. Commit goldens only after this inspection.

```bash
python regress/e2e/driver.py \
  build/windows-msvc-debug/sshmcp.exe \
  regress/e2e/cases/basic --bless
# repeat for errors, timeout; then read the outputs
```

- [ ] **Step 6: Run to verify pass**

Run: `ctest --preset windows-msvc-debug`
Expected: `e2e_basic`, `e2e_errors`, `e2e_timeout` all PASS
(timeout case takes ~0.5 s).

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "test: e2e harness with fake ssh shim and goldens"
```

---

### Task 13: Linter suite

**Files:**
- Create: `share/linter/header_only.py`,
  `share/linter/banned_tokens.py`,
  `share/linter/include_hygiene.py`,
  `share/linter/check_format.py`, `share/linter/style.py`
- Modify: `CMakeLists.txt` (five `add_test` rows +
  `SKIP_RETURN_CODE`)

**Interfaces:**
- Produces: ctest jobs `lint_header_only`, `lint_banned`,
  `lint_include`, `lint_format`, `lint_style`. All scripts take
  the repo root as `sys.argv[1]`, print violations one per line,
  exit 1 on violation, 0 clean, 77 = skip (missing tool). Escape
  comments: `// lint-ok` (banned_tokens), `// style-ok`
  (style.py). Scanned dirs: `app/`, `include/sshmcp/`,
  `regress/unit/`; vendored `include/nlohmann/` never scanned.

- [ ] **Step 1: header_only.py**

```python
"""No .cpp outside app/ and regress/."""
import sys
from pathlib import Path

ALLOWED = ("app/", "regress/")


def main() -> int:
    root = Path(sys.argv[1])
    bad = []
    for path in root.rglob("*.cpp"):
        rel = path.relative_to(root).as_posix()
        if rel.startswith("build/"):
            continue
        if not rel.startswith(ALLOWED):
            bad.append(rel)
    for rel in bad:
        print(f"header-only violation: {rel}")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 2: banned_tokens.py**

```python
"""Bans macros, exceptions, unsafe calls, using-directives."""
import re
import sys
from pathlib import Path

RULES = [
    (re.compile(r"^\s*#\s*(?!pragma\s+once\b|include\b)\w+"),
     "preprocessor directive (only #pragma once, #include)"),
    (re.compile(r"\bsystem\s*\("), "system()"),
    (re.compile(r"\b_?popen\b"), "popen"),
    (re.compile(r"std::regex"), "std::regex"),
    (re.compile(r"\bnew\s+[A-Za-z_(]"), "raw new"),
    (re.compile(r"\bdelete\b\s*(\[\s*\])?\s*[A-Za-z_(]"),
     "raw delete"),
    (re.compile(r"\busing\s+namespace\b"), "using namespace"),
    (re.compile(r"\busing\s+[\w:]+\s*;"),
     "using-declaration (alias form 'using x = y;' is ok)"),
    (re.compile(r"\bthrow\b"), "throw"),
    (re.compile(r"\btry\s*\{"), "try block"),
    (re.compile(r"\bcatch\s*\("), "catch"),
]
ENUM_COLLISION = re.compile(
    r"enum\s+class[^;{]*\{[^}]*"
    r"\b(ERROR|IGNORE|DELETE|SMALL|TRUE|FALSE|INFINITE)\b",
    re.DOTALL)
SCAN = ("app", "include/sshmcp", "regress/unit")


def strip_comment(line: str) -> str:
    return line.split("//", 1)[0]


def main() -> int:
    root = Path(sys.argv[1])
    bad = []
    for base in SCAN:
        for path in sorted((root / base).rglob("*")):
            if path.suffix not in (".hpp", ".cpp"):
                continue
            text = path.read_text(encoding="utf-8")
            rel = path.relative_to(root).as_posix()
            for number, raw in enumerate(
                    text.splitlines(), 1):
                if "lint-ok" in raw:
                    continue
                line = strip_comment(raw)
                for rule, label in RULES:
                    if rule.search(line):
                        bad.append(
                            f"{rel}:{number}: {label}")
            if ENUM_COLLISION.search(text):
                bad.append(
                    f"{rel}: macro-colliding enumerator")
    for item in bad:
        print(item)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 3: include_hygiene.py**

```python
"""#pragma once in headers; include rank order."""
import re
import sys
from pathlib import Path

INCLUDE = re.compile(r'^\s*#\s*include\s*(["<])([^">]+)[">]')
SCAN = ("app", "include/sshmcp", "regress/unit")


def rank(kind: str, target: str) -> int:
    if kind == '"':
        return 0
    if target.startswith("sshmcp/"):
        return 0
    if target.startswith("nlohmann/"):
        return 1
    return 2


def main() -> int:
    root = Path(sys.argv[1])
    bad = []
    for base in SCAN:
        for path in sorted((root / base).rglob("*")):
            if path.suffix not in (".hpp", ".cpp"):
                continue
            rel = path.relative_to(root).as_posix()
            lines = path.read_text(
                encoding="utf-8").splitlines()
            if path.suffix == ".hpp":
                first = next(
                    (l for l in lines if l.strip()), "")
                if first.strip() != "#pragma once":
                    bad.append(
                        f"{rel}: missing #pragma once first")
            last = -1
            for number, line in enumerate(lines, 1):
                found = INCLUDE.match(line)
                if not found:
                    continue
                level = rank(found.group(1),
                             found.group(2))
                if level < last:
                    bad.append(
                        f"{rel}:{number}: include out of "
                        f"order (sshmcp, nlohmann, rest)")
                last = max(last, level)
    for item in bad:
        print(item)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: check_format.py + style.py**

`share/linter/check_format.py`:
```python
"""clang-format --dry-run --Werror over our sources."""
import shutil
import subprocess
import sys
from pathlib import Path


def main() -> int:
    if shutil.which("clang-format") is None:
        print("clang-format not found; skipping")
        return 77
    root = Path(sys.argv[1])
    files = []
    for pattern in ("app/**/*.cpp", "include/sshmcp/**/*.hpp",
                    "regress/unit/**/*.cpp",
                    "regress/unit/**/*.hpp"):
        files += [str(p) for p in sorted(root.glob(pattern))]
    result = subprocess.run(
        ["clang-format", "--dry-run", "--Werror"] + files)
    return result.returncode


if __name__ == "__main__":
    sys.exit(main())
```

`share/linter/style.py`:
```python
"""Best-effort AAA + trailing-return checks. // style-ok escapes."""
import re
import sys
from pathlib import Path

NON_AUTO_LOCAL = re.compile(
    r"^\s*(?:std::[\w:<>, ]+|[a-z_]\w*_t)\s+\w+\s*=")
NON_TRAILING_FN = re.compile(
    r"^\s*(?:inline\s+|static\s+|constexpr\s+)*"
    r"(?!auto\b|return\b|using\b|namespace\b|template\b|"
    r"if\b|for\b|while\b|else\b|case\b|struct\b|class\b|"
    r"enum\b|public\b|private\b|protected\b)"
    r"[\w:]+(?:<[^;{]*>)?\s+\w+\s*\([^;]*\)\s*"
    r"(?:const\s*)?\{")
SCAN = ("app", "include/sshmcp", "regress/unit")


def main() -> int:
    root = Path(sys.argv[1])
    bad = []
    for base in SCAN:
        for path in sorted((root / base).rglob("*")):
            if path.suffix not in (".hpp", ".cpp"):
                continue
            rel = path.relative_to(root).as_posix()
            for number, raw in enumerate(
                    path.read_text(
                        encoding="utf-8").splitlines(), 1):
                if "style-ok" in raw:
                    continue
                line = raw.split("//", 1)[0]
                if NON_AUTO_LOCAL.match(line):
                    bad.append(
                        f"{rel}:{number}: use AAA "
                        f"(auto x = type{{...}})")
                if NON_TRAILING_FN.match(line):
                    bad.append(
                        f"{rel}:{number}: use trailing "
                        f"return (auto f() -> type)")
    for item in bad:
        print(item)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 5: ctest wiring**

Append to `CMakeLists.txt`:
```cmake
add_test(NAME lint_header_only COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/share/linter/header_only.py
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME lint_banned COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/share/linter/banned_tokens.py
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME lint_include COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/share/linter/include_hygiene.py
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME lint_format COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/share/linter/check_format.py
    ${CMAKE_CURRENT_SOURCE_DIR})
add_test(NAME lint_style COMMAND Python3::Interpreter
    ${CMAKE_CURRENT_SOURCE_DIR}/share/linter/style.py
    ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(lint_format lint_style PROPERTIES
    SKIP_RETURN_CODE 77)
```

- [ ] **Step 6: Run linters, fix all violations**

Run: `ctest --preset windows-msvc-debug -R lint`
Expected: violations likely (formatting drift, style misses).
Fix code — run clang-format in write mode
(`clang-format -i` over the same globs) for `lint_format`;
adjust code or add justified `// style-ok` / `// lint-ok` for
heuristic false positives. Re-run until all five pass (77 skip
acceptable only where the tool is genuinely absent — locally
clang-format exists).

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "chore: linter suite enforcing project policies"
```

---

### Task 14: CI

**Files:**
- Create: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: ci presets (Task 1), ctest suites, linters.
- Produces: green-main gate; auto-tag `vX.Y.Z` on version bump.

- [ ] **Step 1: Workflow**

`.github/workflows/ci.yml`:
```yaml
name: ci
on:
  push:
    branches: [main]
  pull_request:

jobs:
  windows-msvc:
    runs-on: windows-2022
    steps:
      - uses: actions/checkout@v4
      - uses: ilammy/msvc-dev-cmd@v1
      - run: choco install ninja -y
      - run: cmake --preset ci-windows-msvc
      - run: cmake --build --preset ci-windows-msvc
      - run: ctest --preset ci-windows-msvc

  linux-gcc:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y
          g++-14 ninja-build clang-format
      - run: cmake --preset ci-linux-gcc
      - run: cmake --build --preset ci-linux-gcc
      - run: ctest --preset ci-linux-gcc

  linux-clang:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y
          clang-18 ninja-build clang-format
      - run: cmake --preset ci-linux-clang
      - run: cmake --build --preset ci-linux-clang
      - run: ctest --preset ci-linux-clang

  linux-clang-asan:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y
          clang-18 ninja-build clang-format
      - run: cmake --preset ci-linux-clang-asan
      - run: cmake --build --preset ci-linux-clang-asan
      - run: ctest --preset ci-linux-clang-asan

  clang-tidy:
    runs-on: ubuntu-24.04
    steps:
      - uses: actions/checkout@v4
      - run: sudo apt-get update && sudo apt-get install -y
          clang-tidy-18 clang-18 ninja-build
      - run: cmake --preset ci-linux-clang
      - run: >
          clang-tidy-18 app/main.cpp
          regress/unit/main.cpp
          --
          -std=c++23
          -Iinclude
          -Iinclude/sshmcp/platform/linux
          -Ibuild/ci-linux-clang/generated

  openbsd:
    runs-on: ubuntu-latest
    continue-on-error: true
    steps:
      - uses: actions/checkout@v4
      - uses: vmactions/openbsd-vm@v1
        with:
          prepare: pkg_add cmake ninja python
          run: |
            cmake --preset openbsd-clang-release
            cmake --build --preset openbsd-clang-release
            ctest --preset openbsd-clang-release

  autotag:
    if: github.ref == 'refs/heads/main'
    needs: [windows-msvc, linux-gcc, linux-clang,
            linux-clang-asan, clang-tidy]
    runs-on: ubuntu-24.04
    permissions:
      contents: write
    steps:
      - uses: actions/checkout@v4
        with:
          fetch-depth: 0
      - name: tag version if new
        run: |
          version=$(sed -n \
            's/^project(sshmcp VERSION \([0-9.]*\).*/\1/p' \
            CMakeLists.txt)
          if [ -z "$version" ]; then exit 1; fi
          if git rev-parse "v$version" >/dev/null 2>&1; then
            echo "v$version exists, rolling on"
          else
            git tag "v$version"
            git push origin "v$version"
          fi
```

- [ ] **Step 2: Validate + commit**

Local check: `python -c "import yaml,sys;
yaml.safe_load(open('.github/workflows/ci.yml'))"` (or eyeball if
PyYAML absent). CI proof comes on first push once a remote
exists; do not block on it locally.

```bash
git add -A
git commit -m "ci: preset matrix, clang-tidy, openbsd vm, autotag"
```

---

### Task 15: README + release readiness

**Files:**
- Modify: `README.md`

- [ ] **Step 1: Write README**

Sections (full prose, 80 col): what it is (3 tools, MCP stdio,
OpenSSH CLI transport — auth/hosts come from your ssh setup);
requirements (OpenSSH client, C++23 toolchain to build, Python 3
for regress); build (`cmake --preset windows-msvc-release &&
cmake --build --preset windows-msvc-release`); Claude Code config
example:
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
env var table (§3 of spec, copy the four vars); security notes
(BatchMode, known_hosts is the trust root, first contact by hand,
commands logged only at debug); regress + linter howto
(`ctest --preset windows-msvc-debug`); versioning (semver,
rolling, version lives in `project()`, CI auto-tags).

- [ ] **Step 2: Full local gate**

Run: `ctest --preset windows-msvc-debug` (everything: unit, e2e,
linters). Expected: all PASS. Then build release preset once:
`cmake --preset windows-msvc-release &&
cmake --build --preset windows-msvc-release` — LTO link must
succeed.

- [ ] **Step 3: Commit**

```bash
git add -A
git commit -m "docs: README with setup, config and security notes"
```

- [ ] **Step 4: Manual VPS smoke (optional, needs real VPS)**

If a real VPS is reachable: configure `.mcp.json` as in README,
restart Claude Code, run `exec whoami`, `write_file` +
`read_file` roundtrip. This is the spec's manual integration
gate; not automatable here.

---

## Self-review notes (already applied)

- Spec §9 `header_only.py` allows `.cpp` under `regress/` — plan
  and spec agree.
- `SSHMCP_SSH_EXE` split is quote-aware (local Python lives under
  `C:\Program Files\`).
- Timeout exit code normalized to `-1` in exec so e2e goldens are
  platform-stable.
- All test files avoid `using` declarations (namespace alias
  `namespace t = sshmcp::test;` only).
- Type names consistent across tasks: `spawn_request_t`,
  `spawn_result_t`, `tool_result_t`, `platform_impl_t`,
  `server_t`, `tool_set_t`, `get_env_fn_t`.
