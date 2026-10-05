# Stage A0 + A1: Skeleton and Instrumentation — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A CI-tested proxy `wing32.dll` that forwards all WinG calls to the original (A0), and that logs the game's own debug output plus every graphics/window/mouse API call relevant to Stage A (A1), producing the findings that A2 depends on.

**Architecture:** Three layers with a one-way dependency `wing32 → platform → core`. `core` is pure C++20 (no `<windows.h>`, also built and tested on Linux). `platform` is a thin Win32 layer (PE parsing, IAT hooks, logging), tested on Windows CI against fixture DLLs built in this repo. `wing32` is the proxy DLL itself and contains only glue.

**Tech Stack:** C++20, CMake ≥ 3.25 + Ninja + presets, MSVC (x86) on Windows, GCC/Clang on Linux, Catch2 v3.8.1 (FetchContent), Pester 5 for the PowerShell installer, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-05-stage-a-display-wrapper-design.md`
This plan covers milestones **A0** and **A1** (spec §9). A2–A4 get their own plan after A1's findings (Task 16), because A2's design depends on them.

## Global Constraints

- Never commit or upload game binaries, assets, logs from the game, or decompiled code. Findings are written in our own words.
- Red/green TDD: every behaviour gets a test that is run and **seen to fail** before it is implemented.
- `src/core` must not include `<windows.h>` or any Windows header (enforced by the Linux CI job).
- Windows binaries are **32-bit x86**, built with MSVC, **static CRT** (`/MT`), warnings `/W4 /WX /permissive- /utf-8`.
- Non-MSVC warnings: `-Wall -Wextra -Wpedantic -Werror`.
- Formatting: clang-format **19.1.7** with the repo's `.clang-format`; CI fails on unformatted code.
- Every commit message ends with the trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- Hooks are only ever installed when the host executable is `WHSHR.EXE` (spec §3).
- Supported build hashes (spec §8): `WHSHR.EXE` `df462ba4dc29d0832c44f14207b47459f281e6dd4a9e3f3ad972fd43a5921326`, `wing32.dll` `dfc3a949141dad973ad2eb4b66e3c8dc8dc7851fe8f360cd42697aab439d70c0`.
- Local game install: `D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat` (only used by opt-in tests and manual steps, via `SOTHR_GAME_DIR`).

## How to build and test locally (used by every task)

All commands run in **PowerShell 7** from the repo root. The x86 MSVC environment does not persist between shells, so every command line starts by dot-sourcing `tools/vsenv.ps1` (created in Task 1):

```powershell
. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L <label>
```

`-L <label>` selects tests by their Catch2 tag (tags are exported as CTest labels; the value is a regex, so anchor it: `-L "^ini$"`). Omit it to run everything.

**Format** (PowerShell does not expand `*.cpp` globs for native tools, so always use this form; it covers new, not-yet-added files too):

```powershell
clang-format -i @(git ls-files --cached --others --exclude-standard '*.cpp' '*.h')
```

**Wait for CI on the commit just pushed** (`gh run watch` without an id would prompt interactively):

```powershell
$sha = git rev-parse HEAD; do { Start-Sleep 5; $id = gh run list --commit $sha --json databaseId --jq '.[0].databaseId' } until ($id); gh run watch $id --exit-status
```

Below, "Format" and "Wait for CI" refer to these two commands.

## File Structure

```
CMakeLists.txt                 root: options, warnings target, Catch2, subdirectories
CMakePresets.json              x86-debug, x86-release, ci, linux-core
.clang-format
.github/workflows/ci.yml       windows-x86, linux-core (gcc+clang), format
tools/vsenv.ps1                enter x86 MSVC dev shell
tools/install.ps1              install / uninstall the proxy into the game
src/core/
  text.h                       trim / to_lower helpers (header-only)
  ini.{h,cpp}                  INI text → sections/keys/values + warnings
  geometry.h                   Size
  config.{h,cpp}               typed Config from INI; default file text
  log_format.{h,cpp}           one formatted log line
  sample_limiter.h             "log first N calls, then every Mth"
  game_modules.{h,cpp}         which modules are game modules; should hooks be installed
src/platform/
  wing_api.h                   WinG function types, export names, Functions struct
  pe.{h,cpp}                   export names; import slot lookup
  wing_forward.{h,cpp}         load the original WinG and resolve its exports
  log.{h,cpp}                  thread-safe file log + global log
  guard.h                      exception guard for hook bodies
  iat_hook.{h,cpp}             patch one import of one module
  modules.{h,cpp}              list loaded modules; module name from address
  dc_info.{h,cpp}              human-readable description of an HDC
src/wing32/
  wing32.def                   the ten undecorated WinG exports
  exports.cpp                  the ten exported functions
  runtime.{h,cpp}              module dir, settings, initialisation, lazy original
  hooks.{h,cpp}                A1 logging hooks + installer
  dllmain.cpp
tests/core/*_test.cpp          Catch2, all platforms
tests/platform/*_test.cpp      Catch2, Windows only
tests/platform/test_paths.h    bin_dir(), temp_file(), read_file()
tests/platform/fake_wing/      fake wing32_orig.dll that records calls
tests/platform/iat_fixture/    tiny DLL whose import the tests hook
tests/gamedata/gamedata_test.cpp   opt-in (SOTHR_GAME_DIR)
tests/tools/install.Tests.ps1      Pester tests for install.ps1
tests/tools/gamedata.Tests.ps1     opt-in hash check of the real game
docs/smoke-test.md
docs/re-notes/a1-findings.md
```

---

### Task 1: Build skeleton and the INI parser

**Files:**
- Create: `CMakeLists.txt`, `CMakePresets.json`, `.clang-format`, `tools/vsenv.ps1`
- Create: `src/core/CMakeLists.txt`, `src/core/text.h`, `src/core/ini.h`, `src/core/ini.cpp`
- Create: `tests/CMakeLists.txt`, `tests/core/CMakeLists.txt`
- Test: `tests/core/ini_test.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces:
  - CMake targets `hr_warnings` (INTERFACE, warning flags), `hr_core` (STATIC, include root `src/`), test target `core_tests`.
  - `namespace hr::core`: `std::string_view trim(std::string_view)`, `std::string to_lower(std::string_view)` (in `core/text.h`).
  - `struct IniWarning { int line; std::string message; };`
  - `struct IniDocument { std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>> sections; std::vector<IniWarning> warnings; const std::string* find(std::string_view section, std::string_view key) const; };`
  - `IniDocument parse_ini(std::string_view text);`

- [ ] **Step 1: Create the build files**

`CMakeLists.txt`:

```cmake
cmake_minimum_required(VERSION 3.25)
project(horned_rat VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HR_CORE_ONLY "Build only the platform-independent core library and its tests" OFF)
option(HR_SANITIZE "Enable ASan + UBSan (GCC/Clang only)" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Static CRT: the proxy DLL must not depend on a VC++ runtime being installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(HR_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(hr_warnings INTERFACE)
if(MSVC)
  target_compile_options(hr_warnings INTERFACE /W4 /WX /permissive- /utf-8)
else()
  target_compile_options(hr_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

include(FetchContent)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.8.1
  SYSTEM)
FetchContent_MakeAvailable(Catch2)
include(CTest)
include(Catch)

add_subdirectory(src/core)
add_subdirectory(tests)
```

`CMakePresets.json`:

```json
{
  "version": 6,
  "cmakeMinimumRequired": { "major": 3, "minor": 25, "patch": 0 },
  "configurePresets": [
    {
      "name": "windows-base",
      "hidden": true,
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": { "CMAKE_CXX_COMPILER": "cl" },
      "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Windows" }
    },
    { "name": "x86-debug", "inherits": "windows-base", "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug" } },
    { "name": "x86-release", "inherits": "windows-base", "cacheVariables": { "CMAKE_BUILD_TYPE": "RelWithDebInfo" } },
    { "name": "ci", "inherits": "windows-base", "cacheVariables": { "CMAKE_BUILD_TYPE": "RelWithDebInfo" } },
    {
      "name": "linux-core",
      "generator": "Ninja",
      "binaryDir": "${sourceDir}/build/${presetName}",
      "cacheVariables": { "CMAKE_BUILD_TYPE": "Debug", "HR_CORE_ONLY": "ON", "HR_SANITIZE": "ON" },
      "condition": { "type": "equals", "lhs": "${hostSystemName}", "rhs": "Linux" }
    }
  ],
  "buildPresets": [
    { "name": "x86-debug", "configurePreset": "x86-debug" },
    { "name": "x86-release", "configurePreset": "x86-release" },
    { "name": "ci", "configurePreset": "ci" },
    { "name": "linux-core", "configurePreset": "linux-core" }
  ],
  "testPresets": [
    { "name": "x86-debug", "configurePreset": "x86-debug", "output": { "outputOnFailure": true } },
    { "name": "x86-release", "configurePreset": "x86-release", "output": { "outputOnFailure": true } },
    { "name": "ci", "configurePreset": "ci", "output": { "outputOnFailure": true } },
    { "name": "linux-core", "configurePreset": "linux-core", "output": { "outputOnFailure": true } }
  ]
}
```

`.clang-format`:

```yaml
BasedOnStyle: LLVM
IndentWidth: 4
ColumnLimit: 100
```

`tools/vsenv.ps1`:

```powershell
# Dot-source to enter an x86 MSVC developer environment in the current shell:
#   . ./tools/vsenv.ps1
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$vs = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'No Visual Studio installation with the C++ x86/x64 tools was found.' }
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64' | Out-Null
```

`src/core/CMakeLists.txt`:

```cmake
add_library(hr_core STATIC ini.cpp)
target_include_directories(hr_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(hr_core PRIVATE hr_warnings)
```

`tests/CMakeLists.txt`:

```cmake
add_subdirectory(core)
```

`tests/core/CMakeLists.txt`:

```cmake
add_executable(core_tests ini_test.cpp)
target_link_libraries(core_tests PRIVATE hr_core hr_warnings Catch2::Catch2WithMain)
catch_discover_tests(core_tests ADD_TAGS_AS_LABELS)
```

- [ ] **Step 2: Write the failing test**

`tests/core/ini_test.cpp`:

```cpp
#include "core/ini.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using Catch::Matchers::ContainsSubstring;
using hr::core::parse_ini;

TEST_CASE("empty text gives an empty document", "[ini]") {
    const auto doc = parse_ini("");
    CHECK(doc.sections.empty());
    CHECK(doc.warnings.empty());
}

TEST_CASE("keys are read per section, names lower-cased and values trimmed", "[ini]") {
    const auto doc = parse_ini("[Display]\n  Mode =  windowed  \nscale=sharp\n");
    REQUIRE(doc.find("display", "mode") != nullptr);
    CHECK(*doc.find("display", "mode") == "windowed");
    CHECK(*doc.find("display", "scale") == "sharp");
    CHECK(doc.warnings.empty());
}

TEST_CASE("find returns nullptr for missing sections and keys", "[ini]") {
    const auto doc = parse_ini("[a]\nk = v\n");
    CHECK(doc.find("b", "k") == nullptr);
    CHECK(doc.find("a", "x") == nullptr);
}

TEST_CASE("comments are ignored, including inline ones", "[ini]") {
    const auto doc = parse_ini("; full line\n# also a comment\n[a]\nk = v ; trailing\n");
    CHECK(*doc.find("a", "k") == "v");
    CHECK(doc.warnings.empty());
}

TEST_CASE("CRLF line endings are accepted", "[ini]") {
    const auto doc = parse_ini("[a]\r\nk = v\r\n");
    CHECK(*doc.find("a", "k") == "v");
    CHECK(doc.warnings.empty());
}

TEST_CASE("keys before any section go into the unnamed section", "[ini]") {
    const auto doc = parse_ini("k = v\n");
    CHECK(*doc.find("", "k") == "v");
}

TEST_CASE("the last duplicate key wins", "[ini]") {
    const auto doc = parse_ini("[a]\nk = 1\nk = 2\n");
    CHECK(*doc.find("a", "k") == "2");
}

TEST_CASE("malformed lines give warnings with line numbers and parsing continues", "[ini]") {
    const auto doc = parse_ini("[a]\nnonsense\n[broken\n= novalue\nk = v\n");
    REQUIRE(doc.warnings.size() == 3);
    CHECK(doc.warnings[0].line == 2);
    CHECK_THAT(doc.warnings[0].message, ContainsSubstring("key = value"));
    CHECK(doc.warnings[1].line == 3);
    CHECK_THAT(doc.warnings[1].message, ContainsSubstring("section"));
    CHECK(doc.warnings[2].line == 4);
    CHECK_THAT(doc.warnings[2].message, ContainsSubstring("key name"));
    CHECK(*doc.find("a", "k") == "v");
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: build FAILS with `fatal error C1083: Cannot open include file: 'core/ini.h'` (the configure step may also fail first because `ini.cpp` does not exist — either is the expected red).

- [ ] **Step 4: Write the minimal implementation**

`src/core/text.h`:

```cpp
#pragma once

#include <algorithm>
#include <string>
#include <string_view>

namespace hr::core {

inline std::string_view trim(std::string_view s) {
    const auto is_space = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && is_space(s.front())) {
        s.remove_prefix(1);
    }
    while (!s.empty() && is_space(s.back())) {
        s.remove_suffix(1);
    }
    return s;
}

inline std::string to_lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
    });
    return out;
}

} // namespace hr::core
```

`src/core/ini.h`:

```cpp
#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace hr::core {

struct IniWarning {
    int line; // 1-based
    std::string message;
};

// Section and key names are lower-cased, values are trimmed, comments start at ';' or '#'
// (anywhere on a line), and a later duplicate key replaces an earlier one.
struct IniDocument {
    std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>> sections;
    std::vector<IniWarning> warnings;

    const std::string* find(std::string_view section, std::string_view key) const;
};

IniDocument parse_ini(std::string_view text);

} // namespace hr::core
```

`src/core/ini.cpp`:

```cpp
#include "core/ini.h"

#include "core/text.h"

namespace hr::core {
namespace {

std::string_view strip_comment(std::string_view line) {
    const auto pos = line.find_first_of(";#");
    return pos == std::string_view::npos ? line : line.substr(0, pos);
}

} // namespace

const std::string* IniDocument::find(std::string_view section, std::string_view key) const {
    const auto s = sections.find(section);
    if (s == sections.end()) {
        return nullptr;
    }
    const auto k = s->second.find(key);
    return k == s->second.end() ? nullptr : &k->second;
}

IniDocument parse_ini(std::string_view text) {
    IniDocument doc;
    std::string current;
    int line_number = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        const auto raw = text.substr(start, end == std::string_view::npos ? text.npos : end - start);
        ++line_number;
        const auto line = trim(strip_comment(raw));
        if (!line.empty()) {
            if (line.front() == '[') {
                if (line.back() != ']') {
                    doc.warnings.push_back({line_number, "unterminated section header"});
                } else {
                    current = to_lower(trim(line.substr(1, line.size() - 2)));
                    doc.sections[current];
                }
            } else if (const auto eq = line.find('='); eq != std::string_view::npos) {
                auto key = to_lower(trim(line.substr(0, eq)));
                if (key.empty()) {
                    doc.warnings.push_back({line_number, "missing key name before '='"});
                } else {
                    doc.sections[current][key] = std::string(trim(line.substr(eq + 1)));
                }
            } else {
                doc.warnings.push_back({line_number, "expected 'key = value'"});
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return doc;
}

} // namespace hr::core
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^ini$"`
Expected: `100% tests passed, 0 tests failed out of 8`.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt CMakePresets.json .clang-format tools/vsenv.ps1 src/core tests
git commit -m "build: CMake skeleton with Catch2; add INI parser" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Typed configuration

**Files:**
- Create: `src/core/geometry.h`, `src/core/config.h`, `src/core/config.cpp`
- Modify: `src/core/CMakeLists.txt` (add `config.cpp`), `tests/core/CMakeLists.txt` (add `config_test.cpp`)
- Test: `tests/core/config_test.cpp`

**Interfaces:**
- Consumes: `hr::core::parse_ini`, `hr::core::to_lower` (Task 1).
- Produces:
  - `struct Size { int width; int height; };` with defaulted `==` (in `core/geometry.h`).
  - `enum class DisplayMode { Windowed, Borderless }; enum class ScaleMode { Integer, Sharp, NearestFit }; enum class AspectMode { Keep4x3, Stretch };`
  - `struct Config { bool enabled = true; DisplayMode mode = Windowed; Size window_size{1280, 960}; ScaleMode scale = Sharp; AspectMode aspect = Keep4x3; bool vsync = true; bool log = true; bool game_debug = true; };` with defaulted `==`.
  - `struct ConfigLoadResult { Config config; std::vector<std::string> warnings; };`
  - `ConfigLoadResult load_config(std::string_view ini_text);`
  - `std::string default_config_ini();`

- [ ] **Step 1: Write the failing test**

`tests/core/config_test.cpp`:

```cpp
#include "core/config.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

using namespace hr::core;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

TEST_CASE("an empty file gives the defaults without warnings", "[config]") {
    const auto r = load_config("");
    CHECK(r.config == Config{});
    CHECK(r.warnings.empty());
}

TEST_CASE("every key is read", "[config]") {
    const auto r = load_config("[general]\nenabled = false\n"
                               "[display]\nmode = borderless\nwindow_size = 1920x1440\n"
                               "scale = integer\naspect = stretch\nvsync = no\n"
                               "[debug]\nlog = off\ngame_debug = 0\n");
    CHECK(r.warnings.empty());
    CHECK_FALSE(r.config.enabled);
    CHECK(r.config.mode == DisplayMode::Borderless);
    CHECK(r.config.window_size == Size{1920, 1440});
    CHECK(r.config.scale == ScaleMode::Integer);
    CHECK(r.config.aspect == AspectMode::Stretch);
    CHECK_FALSE(r.config.vsync);
    CHECK_FALSE(r.config.log);
    CHECK_FALSE(r.config.game_debug);
}

TEST_CASE("values are case-insensitive", "[config]") {
    const auto r = load_config("[display]\nscale = Nearest-Fit\nmode = WINDOWED\n[general]\nenabled = Yes\n");
    CHECK(r.warnings.empty());
    CHECK(r.config.scale == ScaleMode::NearestFit);
    CHECK(r.config.mode == DisplayMode::Windowed);
    CHECK(r.config.enabled);
}

TEST_CASE("invalid values keep the default and warn", "[config]") {
    const auto r = load_config("[display]\nscale = blurry\nwindow_size = 100x50\n[general]\nenabled = maybe\n");
    CHECK(r.config == Config{});
    REQUIRE(r.warnings.size() == 3);
    CHECK_THAT(r.warnings[0], ContainsSubstring("display.scale") && ContainsSubstring("blurry"));
    CHECK_THAT(r.warnings[1], ContainsSubstring("display.window_size") && ContainsSubstring("100x50"));
    CHECK_THAT(r.warnings[2], ContainsSubstring("general.enabled") && ContainsSubstring("maybe"));
}

TEST_CASE("unknown keys and sections warn", "[config]") {
    const auto r = load_config("top = 1\n[display]\nzoom = 2\n[extra]\nx = 3\n");
    REQUIRE(r.warnings.size() == 3);
    CHECK_THAT(r.warnings[0], ContainsSubstring("unknown key 'top'"));
    CHECK_THAT(r.warnings[1], ContainsSubstring("unknown key 'display.zoom'"));
    CHECK_THAT(r.warnings[2], ContainsSubstring("unknown key 'extra.x'"));
}

TEST_CASE("INI syntax warnings are passed on with their line number", "[config]") {
    const auto r = load_config("[display]\nnonsense\n");
    REQUIRE(r.warnings.size() == 1);
    CHECK_THAT(r.warnings[0], StartsWith("line 2: "));
}

TEST_CASE("window_size accepts an upper-case X and rejects malformed sizes", "[config]") {
    CHECK(load_config("[display]\nwindow_size = 800X600\n").config.window_size == Size{800, 600});
    for (const char* bad : {"800", "x600", "800x", "800x600x2", "-800x600", "abcxdef", "800 x 600",
                            "99999x600"}) {
        INFO(bad);
        CHECK(load_config(std::string("[display]\nwindow_size = ") + bad + "\n").warnings.size() == 1);
    }
}

TEST_CASE("the default file parses back to the defaults", "[config]") {
    const auto r = load_config(default_config_ini());
    CHECK(r.warnings.empty());
    CHECK(r.config == Config{});
}
```

Add it to `tests/core/CMakeLists.txt`:

```cmake
add_executable(core_tests ini_test.cpp config_test.cpp)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'core/config.h'`.

- [ ] **Step 3: Write the minimal implementation**

`src/core/geometry.h`:

```cpp
#pragma once

namespace hr::core {

struct Size {
    int width = 0;
    int height = 0;

    friend bool operator==(const Size&, const Size&) = default;
};

} // namespace hr::core
```

`src/core/config.h`:

```cpp
#pragma once

#include "core/geometry.h"

#include <string>
#include <string_view>
#include <vector>

namespace hr::core {

enum class DisplayMode { Windowed, Borderless };
enum class ScaleMode { Integer, Sharp, NearestFit };
enum class AspectMode { Keep4x3, Stretch };

// Mirrors horned-rat.ini (spec §6.1).
struct Config {
    bool enabled = true;
    DisplayMode mode = DisplayMode::Windowed;
    Size window_size{1280, 960};
    ScaleMode scale = ScaleMode::Sharp;
    AspectMode aspect = AspectMode::Keep4x3;
    bool vsync = true;
    bool log = true;
    bool game_debug = true;

    friend bool operator==(const Config&, const Config&) = default;
};

struct ConfigLoadResult {
    Config config;
    std::vector<std::string> warnings; // never fatal: bad entries fall back to defaults
};

ConfigLoadResult load_config(std::string_view ini_text);

// The commented file written on first run.
std::string default_config_ini();

} // namespace hr::core
```

`src/core/config.cpp`:

```cpp
#include "core/config.h"

#include "core/ini.h"
#include "core/text.h"

#include <charconv>
#include <initializer_list>
#include <optional>
#include <utility>

namespace hr::core {
namespace {

std::optional<bool> parse_bool(std::string_view v) {
    const auto s = to_lower(v);
    if (s == "true" || s == "yes" || s == "on" || s == "1") {
        return true;
    }
    if (s == "false" || s == "no" || s == "off" || s == "0") {
        return false;
    }
    return std::nullopt;
}

bool parse_int(std::string_view s, int& out) {
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && end == s.data() + s.size();
}

std::optional<Size> parse_size(std::string_view v) {
    const auto x = v.find_first_of("xX");
    if (x == std::string_view::npos) {
        return std::nullopt;
    }
    Size size;
    if (!parse_int(v.substr(0, x), size.width) || !parse_int(v.substr(x + 1), size.height)) {
        return std::nullopt;
    }
    if (size.width < 320 || size.height < 240 || size.width > 16384 || size.height > 16384) {
        return std::nullopt;
    }
    return size;
}

template <typename E>
std::optional<E> parse_choice(std::string_view v,
                              std::initializer_list<std::pair<std::string_view, E>> choices) {
    const auto s = to_lower(v);
    for (const auto& [name, value] : choices) {
        if (s == name) {
            return value;
        }
    }
    return std::nullopt;
}

template <typename T> bool assign(T& target, const std::optional<T>& value) {
    if (!value) {
        return false;
    }
    target = *value;
    return true;
}

struct Field {
    std::string_view name; // "section.key"
    bool (*apply)(Config&, std::string_view);
};

constexpr Field kFields[] = {
    {"general.enabled", [](Config& c, std::string_view v) { return assign(c.enabled, parse_bool(v)); }},
    {"display.mode",
     [](Config& c, std::string_view v) {
         return assign(c.mode, parse_choice<DisplayMode>(v, {{"windowed", DisplayMode::Windowed},
                                                             {"borderless", DisplayMode::Borderless}}));
     }},
    {"display.window_size",
     [](Config& c, std::string_view v) { return assign(c.window_size, parse_size(v)); }},
    {"display.scale",
     [](Config& c, std::string_view v) {
         return assign(c.scale, parse_choice<ScaleMode>(v, {{"integer", ScaleMode::Integer},
                                                            {"sharp", ScaleMode::Sharp},
                                                            {"nearest-fit", ScaleMode::NearestFit}}));
     }},
    {"display.aspect",
     [](Config& c, std::string_view v) {
         return assign(c.aspect, parse_choice<AspectMode>(v, {{"4:3", AspectMode::Keep4x3},
                                                              {"stretch", AspectMode::Stretch}}));
     }},
    {"display.vsync", [](Config& c, std::string_view v) { return assign(c.vsync, parse_bool(v)); }},
    {"debug.log", [](Config& c, std::string_view v) { return assign(c.log, parse_bool(v)); }},
    {"debug.game_debug",
     [](Config& c, std::string_view v) { return assign(c.game_debug, parse_bool(v)); }},
};

const Field* find_field(std::string_view name) {
    for (const auto& field : kFields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

} // namespace

ConfigLoadResult load_config(std::string_view ini_text) {
    ConfigLoadResult result;
    const auto doc = parse_ini(ini_text);
    for (const auto& w : doc.warnings) {
        result.warnings.push_back("line " + std::to_string(w.line) + ": " + w.message);
    }
    for (const auto& [section, keys] : doc.sections) {
        for (const auto& [key, value] : keys) {
            const auto name = section.empty() ? key : section + "." + key;
            const auto* field = find_field(name);
            if (field == nullptr) {
                result.warnings.push_back("unknown key '" + name + "'");
            } else if (!field->apply(result.config, value)) {
                result.warnings.push_back(name + ": invalid value '" + value +
                                          "', using the default");
            }
        }
    }
    return result;
}

std::string default_config_ini() {
    return R"ini([general]
enabled      = true        ; false = pure passthrough to the original WinG
[display]
mode         = windowed    ; windowed | borderless
window_size  = 1280x960    ; initial client size in windowed mode
scale        = sharp       ; integer | sharp | nearest-fit
aspect       = 4:3         ; 4:3 | stretch
vsync        = true
[debug]
log          = true        ; horned-rat.log
game_debug   = true        ; capture the game's own OutputDebugStringA output
)ini";
}

} // namespace hr::core
```

Update `src/core/CMakeLists.txt`:

```cmake
add_library(hr_core STATIC ini.cpp config.cpp)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^config$"`
Expected: `100% tests passed, 0 tests failed out of 8`.

- [ ] **Step 5: Commit**

```bash
git add src/core tests/core
git commit -m "core: typed configuration loaded from horned-rat.ini" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Continuous integration

**Files:**
- Create: `.github/workflows/ci.yml`

**Interfaces:**
- Consumes: presets `ci` and `linux-core` (Task 1).
- Produces: three CI jobs, `windows-x86`, `linux-core` (gcc and clang), `format`. Later tasks add steps to `windows-x86`.

The deliverable of this task is CI itself, so the red/green cycle is: introduce a deliberate formatting violation, see the `format` job fail, fix it, see everything green.

- [ ] **Step 1: Install the pinned clang-format locally and format the tree**

Run: `python -m pip install --user clang-format==19.1.7; clang-format --version`
Expected: `clang-format version 19.1.7`. (If `clang-format` is not found, use `python -m clang_format` instead in the commands below.)

Run Format.

- [ ] **Step 2: Write the workflow**

`.github/workflows/ci.yml`:

```yaml
name: CI

on:
  push:
    branches: [main]
  pull_request:

jobs:
  windows-x86:
    runs-on: windows-latest
    steps:
      - uses: actions/checkout@v4
      - uses: ilammy/msvc-dev-cmd@v1
        with:
          arch: amd64_x86
      - uses: actions/cache@v4
        with:
          path: build/ci/_deps
          key: deps-windows-${{ hashFiles('CMakeLists.txt') }}
      - name: Configure
        run: cmake --preset ci
      - name: Build
        run: cmake --build --preset ci
      - name: Test
        run: ctest --preset ci

  linux-core:
    runs-on: ubuntu-latest
    strategy:
      matrix:
        compiler: [g++, clang++]
    steps:
      - uses: actions/checkout@v4
      - name: Install Ninja
        run: sudo apt-get update && sudo apt-get install -y ninja-build
      - uses: actions/cache@v4
        with:
          path: build/linux-core/_deps
          key: deps-linux-${{ matrix.compiler }}-${{ hashFiles('CMakeLists.txt') }}
      - name: Configure
        run: cmake --preset linux-core -DCMAKE_CXX_COMPILER=${{ matrix.compiler }}
      - name: Build
        run: cmake --build --preset linux-core
      - name: Test
        run: ctest --preset linux-core

  format:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
      - name: Install clang-format
        run: pip install clang-format==19.1.7
      - name: Check formatting
        run: clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.h')
```

- [ ] **Step 3: Red — push with a deliberate formatting violation**

Edit `src/core/geometry.h`: change `int width = 0;` to `int    width = 0;` (extra spaces). Then:

```bash
git add .github/workflows/ci.yml src tests
git commit -m "ci: add Windows x86, Linux core and format jobs" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
# Wait for CI (see top of plan)
```

Expected: the run FAILS; `format` reports `geometry.h ... code should be clang-formatted [-Wclang-format-violations]`. `windows-x86` and both `linux-core` jobs must be green (if they are not, fix them before continuing — typical causes: a Windows header pulled into `core`, a compiler warning promoted to an error).

- [ ] **Step 4: Green — fix the violation**

Run: `
```bash
git add src/core/geometry.h
git commit -m "style: fix formatting" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
# Wait for CI (see top of plan)
```

Expected: all four jobs green.

---

### Task 4: Platform library and PE export listing

**Files:**
- Modify: `CMakeLists.txt` (add platform subdirectory and the x86 check), `tests/CMakeLists.txt`
- Create: `src/platform/CMakeLists.txt`, `src/platform/wing_api.h`, `src/platform/pe.h`, `src/platform/pe.cpp`
- Create: `tests/platform/CMakeLists.txt`, `tests/platform/test_paths.h`
- Create: `tests/platform/fake_wing/fake_wing.h`, `tests/platform/fake_wing/fake_wing.cpp`, `tests/platform/fake_wing/fake_wing.def`
- Test: `tests/platform/pe_test.cpp`

**Interfaces:**
- Consumes: `hr_core`, `hr_warnings`.
- Produces:
  - CMake target `hr_platform` (STATIC, defines `WIN32_LEAN_AND_MEAN`, `NOMINMAX`), `fake_wing` (builds `bin/wing32_orig.dll`), `platform_tests`.
  - `hr::platform::wing`: function pointer types `BitBlt_t, CreateBitmap_t, CreateDC_t, CreateHalftoneBrush_t, CreateHalftonePalette_t, GetDIBColorTable_t, GetDIBPointer_t, RecommendDIBFormat_t, SetDIBColorTable_t, StretchBlt_t`; `inline constexpr std::array<std::string_view, 10> export_names`; `struct Functions { bit_blt, create_bitmap, create_dc, create_halftone_brush, create_halftone_palette, get_dib_color_table, get_dib_pointer, recommend_dib_format, set_dib_color_table, stretch_blt; }`.
  - `std::vector<std::string> hr::platform::pe::export_names(HMODULE module);`
  - Test helpers `hr::test::bin_dir()`, `hr::test::temp_file(std::string_view name)`, `hr::test::read_file(const std::filesystem::path&)`.
  - Fake: `struct FakeWingCall { char function[32]; int argc; std::uintptr_t args[10]; };`, export `FakeWing_LastCall` of type `FakeWing_LastCall_t = const FakeWingCall*(__stdcall*)()`. Return values: `WinGCreateDC` → `0x1001`, `WinGCreateBitmap` → `0x1002` and `*bits = 0x1003`, `WinGGetDIBPointer` → `0x1004`, `WinGCreateHalftonePalette` → `0x1005`, `WinGCreateHalftoneBrush` → `0x1006`, the color-table functions → their `count` argument, `WinGRecommendDIBFormat`/`WinGBitBlt`/`WinGStretchBlt` → `TRUE`.

- [ ] **Step 1: Wire up the platform build, the fake WinG and the test helpers**

Replace the last two lines of `CMakeLists.txt` (`add_subdirectory(src/core)` / `add_subdirectory(tests)`) with:

```cmake
add_subdirectory(src/core)
if(NOT HR_CORE_ONLY)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The Windows parts must be built for 32-bit x86: enter the dev shell with tools/vsenv.ps1 and use an x86-* or ci preset.")
  endif()
  add_subdirectory(src/platform)
endif()
add_subdirectory(tests)
```

`tests/CMakeLists.txt`:

```cmake
add_subdirectory(core)
if(NOT HR_CORE_ONLY)
  add_subdirectory(platform)
endif()
```

`src/platform/CMakeLists.txt`:

```cmake
add_library(hr_platform STATIC pe.cpp)
target_link_libraries(hr_platform PUBLIC hr_core PRIVATE hr_warnings)
target_compile_definitions(hr_platform PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX)
```

`src/platform/wing_api.h`:

```cpp
#pragma once

#include <windows.h>

#include <array>
#include <string_view>

// The WinG 1.0 API as exported by wing32.dll. Signatures follow the WinG SDK (wing.h).
namespace hr::platform::wing {

using BitBlt_t = BOOL(WINAPI*)(HDC dest, int x, int y, int width, int height, HDC src, int src_x,
                               int src_y);
using CreateBitmap_t = HBITMAP(WINAPI*)(HDC dc, const BITMAPINFO* header, void** bits);
using CreateDC_t = HDC(WINAPI*)();
using CreateHalftoneBrush_t = HBRUSH(WINAPI*)(HDC dc, COLORREF color, int dither_type);
using CreateHalftonePalette_t = HPALETTE(WINAPI*)();
using GetDIBColorTable_t = UINT(WINAPI*)(HDC dc, UINT start, UINT count, RGBQUAD* colors);
using GetDIBPointer_t = void*(WINAPI*)(HBITMAP bitmap, BITMAPINFO* header);
using RecommendDIBFormat_t = BOOL(WINAPI*)(BITMAPINFO* format);
using SetDIBColorTable_t = UINT(WINAPI*)(HDC dc, UINT start, UINT count, const RGBQUAD* colors);
using StretchBlt_t = BOOL(WINAPI*)(HDC dest, int x, int y, int width, int height, HDC src,
                                   int src_x, int src_y, int src_width, int src_height);

// The ten exports of wing32.dll, sorted.
inline constexpr std::array<std::string_view, 10> export_names{
    "WinGBitBlt",           "WinGCreateBitmap",       "WinGCreateDC",
    "WinGCreateHalftoneBrush", "WinGCreateHalftonePalette", "WinGGetDIBColorTable",
    "WinGGetDIBPointer",    "WinGRecommendDIBFormat", "WinGSetDIBColorTable",
    "WinGStretchBlt"};

struct Functions {
    BitBlt_t bit_blt = nullptr;
    CreateBitmap_t create_bitmap = nullptr;
    CreateDC_t create_dc = nullptr;
    CreateHalftoneBrush_t create_halftone_brush = nullptr;
    CreateHalftonePalette_t create_halftone_palette = nullptr;
    GetDIBColorTable_t get_dib_color_table = nullptr;
    GetDIBPointer_t get_dib_pointer = nullptr;
    RecommendDIBFormat_t recommend_dib_format = nullptr;
    SetDIBColorTable_t set_dib_color_table = nullptr;
    StretchBlt_t stretch_blt = nullptr;
};

} // namespace hr::platform::wing
```

`tests/platform/test_paths.h`:

```cpp
#pragma once

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace hr::test {

// Directory of the running test executable (= build/<preset>/bin, where all DLLs land too).
inline std::filesystem::path bin_dir() {
    wchar_t buffer[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(std::wstring(buffer, n)).parent_path();
}

// A fresh path in the temp directory; any existing file is removed.
inline std::filesystem::path temp_file(std::string_view name) {
    auto path = std::filesystem::temp_directory_path() / ("horned_rat_" + std::string(name));
    std::filesystem::remove(path);
    return path;
}

inline std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace hr::test
```

`tests/platform/fake_wing/fake_wing.h`:

```cpp
#pragma once

#include <cstdint>

// The last call received by the fake wing32_orig.dll.
struct FakeWingCall {
    char function[32];
    int argc;
    std::uintptr_t args[10];
};

// Exported by the fake as "FakeWing_LastCall".
using FakeWing_LastCall_t = const FakeWingCall*(__stdcall*)();
```

`tests/platform/fake_wing/fake_wing.cpp`:

```cpp
// A stand-in for the original wing32.dll: records each call and returns fixed values,
// so CI can test forwarding without any Mindscape/Microsoft binary.
#include "fake_wing.h"

#include <windows.h>

#include <cstring>
#include <initializer_list>
#include <type_traits>

namespace {

FakeWingCall g_last{};

void record(const char* function, std::initializer_list<std::uintptr_t> args) {
    g_last = {};
    strncpy_s(g_last.function, function, _TRUNCATE);
    for (const auto a : args) {
        g_last.args[g_last.argc++] = a;
    }
}

template <typename T> std::uintptr_t u(T v) {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<std::uintptr_t>(v);
    } else {
        return static_cast<std::uintptr_t>(v);
    }
}

template <typename H> H handle(std::uintptr_t value) { return reinterpret_cast<H>(value); }

} // namespace

extern "C" {

const FakeWingCall* WINAPI FakeWing_LastCall() { return &g_last; }

HDC WINAPI WinGCreateDC() {
    record("WinGCreateDC", {});
    return handle<HDC>(0x1001);
}

BOOL WINAPI WinGRecommendDIBFormat(BITMAPINFO* format) {
    record("WinGRecommendDIBFormat", {u(format)});
    return TRUE;
}

HBITMAP WINAPI WinGCreateBitmap(HDC dc, const BITMAPINFO* header, void** bits) {
    record("WinGCreateBitmap", {u(dc), u(header), u(bits)});
    if (bits != nullptr) {
        *bits = handle<void*>(0x1003);
    }
    return handle<HBITMAP>(0x1002);
}

void* WINAPI WinGGetDIBPointer(HBITMAP bitmap, BITMAPINFO* header) {
    record("WinGGetDIBPointer", {u(bitmap), u(header)});
    return handle<void*>(0x1004);
}

UINT WINAPI WinGGetDIBColorTable(HDC dc, UINT start, UINT count, RGBQUAD* colors) {
    record("WinGGetDIBColorTable", {u(dc), u(start), u(count), u(colors)});
    return count;
}

UINT WINAPI WinGSetDIBColorTable(HDC dc, UINT start, UINT count, const RGBQUAD* colors) {
    record("WinGSetDIBColorTable", {u(dc), u(start), u(count), u(colors)});
    return count;
}

HPALETTE WINAPI WinGCreateHalftonePalette() {
    record("WinGCreateHalftonePalette", {});
    return handle<HPALETTE>(0x1005);
}

HBRUSH WINAPI WinGCreateHalftoneBrush(HDC dc, COLORREF color, int dither_type) {
    record("WinGCreateHalftoneBrush", {u(dc), u(color), u(dither_type)});
    return handle<HBRUSH>(0x1006);
}

BOOL WINAPI WinGBitBlt(HDC dest, int x, int y, int width, int height, HDC src, int src_x,
                       int src_y) {
    record("WinGBitBlt", {u(dest), u(x), u(y), u(width), u(height), u(src), u(src_x), u(src_y)});
    return TRUE;
}

BOOL WINAPI WinGStretchBlt(HDC dest, int x, int y, int width, int height, HDC src, int src_x,
                           int src_y, int src_width, int src_height) {
    record("WinGStretchBlt", {u(dest), u(x), u(y), u(width), u(height), u(src), u(src_x), u(src_y),
                              u(src_width), u(src_height)});
    return TRUE;
}

} // extern "C"
```

`tests/platform/fake_wing/fake_wing.def`:

```
LIBRARY wing32_orig
EXPORTS
    FakeWing_LastCall
    WinGBitBlt
    WinGCreateBitmap
    WinGCreateDC
    WinGCreateHalftoneBrush
    WinGCreateHalftonePalette
    WinGGetDIBColorTable
    WinGGetDIBPointer
    WinGRecommendDIBFormat
    WinGSetDIBColorTable
    WinGStretchBlt
```

`tests/platform/CMakeLists.txt`:

```cmake
add_library(fake_wing SHARED fake_wing/fake_wing.cpp fake_wing/fake_wing.def)
set_target_properties(fake_wing PROPERTIES OUTPUT_NAME wing32_orig)
target_compile_definitions(fake_wing PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(fake_wing PRIVATE hr_warnings)

add_executable(platform_tests pe_test.cpp)
target_include_directories(platform_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(platform_tests PRIVATE hr_platform hr_warnings Catch2::Catch2WithMain)
add_dependencies(platform_tests fake_wing)
catch_discover_tests(platform_tests ADD_TAGS_AS_LABELS)
```

- [ ] **Step 2: Write the failing test**

`tests/platform/pe_test.cpp`:

```cpp
#include "platform/pe.h"
#include "platform/wing_api.h"
#include "test_paths.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

TEST_CASE("export_names lists every named export of a module", "[pe]") {
    const auto path = hr::test::bin_dir() / "wing32_orig.dll";
    // DONT_RESOLVE_DLL_REFERENCES maps the image without running its DllMain.
    const HMODULE module = LoadLibraryExW(path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    REQUIRE(module != nullptr);
    auto names = hr::platform::pe::export_names(module);
    FreeLibrary(module);

    std::vector<std::string> expected(hr::platform::wing::export_names.begin(),
                                      hr::platform::wing::export_names.end());
    expected.emplace_back("FakeWing_LastCall");
    std::ranges::sort(names);
    std::ranges::sort(expected);
    CHECK(names == expected);
}

TEST_CASE("export_names of a module without exports is empty", "[pe]") {
    CHECK(hr::platform::pe::export_names(GetModuleHandleW(nullptr)).empty());
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL — configure reports `Cannot find source file: pe.cpp`, or the build reports `Cannot open include file: 'platform/pe.h'`.

- [ ] **Step 4: Write the minimal implementation**

`src/platform/pe.h`:

```cpp
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace hr::platform::pe {

// Names in the export directory of a module mapped as an image, in name-table order.
std::vector<std::string> export_names(HMODULE module);

} // namespace hr::platform::pe
```

`src/platform/pe.cpp`:

```cpp
#include "platform/pe.h"

namespace hr::platform::pe {
namespace {

const IMAGE_NT_HEADERS* nt_headers(const BYTE* base) {
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        return nullptr;
    }
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    return nt->Signature == IMAGE_NT_SIGNATURE ? nt : nullptr;
}

} // namespace

std::vector<std::string> export_names(HMODULE module) {
    const auto base = reinterpret_cast<const BYTE*>(module);
    const auto nt = nt_headers(base);
    if (nt == nullptr) {
        return {};
    }
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (dir.VirtualAddress == 0) {
        return {};
    }
    const auto exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + dir.VirtualAddress);
    const auto names = reinterpret_cast<const DWORD*>(base + exports->AddressOfNames);
    std::vector<std::string> out;
    out.reserve(exports->NumberOfNames);
    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        out.emplace_back(reinterpret_cast<const char*>(base + names[i]));
    }
    return out;
}

} // namespace hr::platform::pe
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^pe$"`
Expected: `100% tests passed, 0 tests failed out of 2`.

- [ ] **Step 6: Format, commit, push, and check CI**

Run Format, then:

```bash
git add CMakeLists.txt src/platform tests
git commit -m "platform: PE export listing; fake WinG test fixture" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
# Wait for CI (see top of plan)
```

Expected: all CI jobs green.

---

### Task 5: Loading the original WinG

**Files:**
- Create: `src/platform/wing_forward.h`, `src/platform/wing_forward.cpp`
- Modify: `src/platform/CMakeLists.txt` (add `wing_forward.cpp`), `tests/platform/CMakeLists.txt` (add `wing_forward_test.cpp`)
- Test: `tests/platform/wing_forward_test.cpp`

**Interfaces:**
- Consumes: `hr::platform::wing::Functions` (Task 4), fake `wing32_orig.dll` (Task 4).
- Produces:
  - `struct hr::platform::WingLoadResult { std::optional<wing::Functions> functions; std::string error; };`
  - `WingLoadResult hr::platform::load_original_wing(const std::filesystem::path& path);` — error strings contain `could not load` or `does not export <Name>`.

- [ ] **Step 1: Write the failing test**

`tests/platform/wing_forward_test.cpp`:

```cpp
#include "platform/wing_forward.h"
#include "test_paths.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>

using Catch::Matchers::ContainsSubstring;
using hr::platform::load_original_wing;

TEST_CASE("load_original_wing resolves all ten functions", "[wing_forward]") {
    const auto r = load_original_wing(hr::test::bin_dir() / "wing32_orig.dll");
    REQUIRE(r.error.empty());
    REQUIRE(r.functions.has_value());
    const auto& f = *r.functions;
    CHECK(f.bit_blt != nullptr);
    CHECK(f.create_bitmap != nullptr);
    CHECK(f.create_dc != nullptr);
    CHECK(f.create_halftone_brush != nullptr);
    CHECK(f.create_halftone_palette != nullptr);
    CHECK(f.get_dib_color_table != nullptr);
    CHECK(f.get_dib_pointer != nullptr);
    CHECK(f.recommend_dib_format != nullptr);
    CHECK(f.set_dib_color_table != nullptr);
    CHECK(f.stretch_blt != nullptr);
    CHECK(f.create_dc() == reinterpret_cast<HDC>(std::uintptr_t{0x1001}));
}

TEST_CASE("load_original_wing reports a missing file", "[wing_forward]") {
    const auto r = load_original_wing(hr::test::bin_dir() / "does_not_exist.dll");
    CHECK_FALSE(r.functions.has_value());
    CHECK_THAT(r.error, ContainsSubstring("could not load"));
}

TEST_CASE("load_original_wing reports a DLL that is not WinG", "[wing_forward]") {
    wchar_t system_dir[MAX_PATH];
    const UINT n = GetSystemDirectoryW(system_dir, MAX_PATH);
    const auto path = std::filesystem::path(std::wstring(system_dir, n)) / "version.dll";
    const auto r = load_original_wing(path);
    CHECK_FALSE(r.functions.has_value());
    CHECK_THAT(r.error, ContainsSubstring("does not export WinGBitBlt"));
}
```

Update `tests/platform/CMakeLists.txt`:

```cmake
add_executable(platform_tests pe_test.cpp wing_forward_test.cpp)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'platform/wing_forward.h'`.

- [ ] **Step 3: Write the minimal implementation**

`src/platform/wing_forward.h`:

```cpp
#pragma once

#include "platform/wing_api.h"

#include <filesystem>
#include <optional>
#include <string>

namespace hr::platform {

struct WingLoadResult {
    std::optional<wing::Functions> functions;
    std::string error; // empty on success
};

// Loads the original WinG DLL at `path` and resolves all ten exports. On success the module is
// intentionally never freed: it must live as long as the process.
WingLoadResult load_original_wing(const std::filesystem::path& path);

} // namespace hr::platform
```

`src/platform/wing_forward.cpp`:

```cpp
#include "platform/wing_forward.h"

#include <type_traits>

namespace hr::platform {

WingLoadResult load_original_wing(const std::filesystem::path& path) {
    const HMODULE module = LoadLibraryW(path.c_str());
    if (module == nullptr) {
        const DWORD error = GetLastError();
        return {std::nullopt,
                "could not load " + path.string() + " (error " + std::to_string(error) + ")"};
    }

    wing::Functions f;
    std::string missing;
    const auto resolve = [&](auto& target, const char* name) {
        target = reinterpret_cast<std::remove_reference_t<decltype(target)>>(
            GetProcAddress(module, name));
        if (target == nullptr && missing.empty()) {
            missing = name;
        }
    };
    resolve(f.bit_blt, "WinGBitBlt");
    resolve(f.create_bitmap, "WinGCreateBitmap");
    resolve(f.create_dc, "WinGCreateDC");
    resolve(f.create_halftone_brush, "WinGCreateHalftoneBrush");
    resolve(f.create_halftone_palette, "WinGCreateHalftonePalette");
    resolve(f.get_dib_color_table, "WinGGetDIBColorTable");
    resolve(f.get_dib_pointer, "WinGGetDIBPointer");
    resolve(f.recommend_dib_format, "WinGRecommendDIBFormat");
    resolve(f.set_dib_color_table, "WinGSetDIBColorTable");
    resolve(f.stretch_blt, "WinGStretchBlt");

    if (!missing.empty()) {
        FreeLibrary(module);
        return {std::nullopt, path.string() + " does not export " + missing};
    }
    return {f, {}};
}

} // namespace hr::platform
```

Update `src/platform/CMakeLists.txt`:

```cmake
add_library(hr_platform STATIC pe.cpp wing_forward.cpp)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^wing_forward$"`
Expected: `100% tests passed, 0 tests failed out of 3`.

- [ ] **Step 5: Format and commit**

Run Format, then:

```bash
git add src/platform tests/platform
git commit -m "platform: load the original WinG and resolve its exports" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: The proxy `wing32.dll` (pure forwarding)

**Files:**
- Modify: `CMakeLists.txt` (add `src/wing32`), `tests/platform/CMakeLists.txt`, `.github/workflows/ci.yml` (package + upload artifact)
- Create: `src/wing32/CMakeLists.txt`, `src/wing32/wing32.def`, `src/wing32/runtime.h`, `src/wing32/runtime.cpp`, `src/wing32/exports.cpp`, `src/wing32/dllmain.cpp`
- Test: `tests/platform/proxy_test.cpp`

**Interfaces:**
- Consumes: `load_original_wing` (Task 5), `pe::export_names` (Task 4), `FakeWingCall` (Task 4).
- Produces:
  - CMake target `wing32` → `bin/wing32.dll`, exporting exactly the ten WinG names.
  - `hr::wing32::module_dir()` → `std::filesystem::path` of the directory holding our DLL.
  - `hr::wing32::original()` → `const platform::wing::Functions&`, loading `wing32_orig.dll` next to our DLL on first use; on failure shows one `MessageBox` and exits (spec §6.2).

- [ ] **Step 1: Write the failing test**

`tests/platform/proxy_test.cpp`:

```cpp
#include "fake_wing/fake_wing.h"
#include "platform/pe.h"
#include "platform/wing_api.h"
#include "test_paths.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

using namespace hr::platform::wing;

namespace {

HMODULE proxy() {
    static const HMODULE module = LoadLibraryW((hr::test::bin_dir() / "wing32.dll").c_str());
    return module;
}

template <typename F> F proxy_fn(const char* name) {
    REQUIRE(proxy() != nullptr);
    const auto fn = reinterpret_cast<F>(GetProcAddress(proxy(), name));
    REQUIRE(fn != nullptr);
    return fn;
}

const FakeWingCall& last_call() {
    const auto get = reinterpret_cast<FakeWing_LastCall_t>(
        GetProcAddress(GetModuleHandleW(L"wing32_orig.dll"), "FakeWing_LastCall"));
    REQUIRE(get != nullptr);
    return *get();
}

std::vector<std::uintptr_t> args_of(const FakeWingCall& call) {
    return {call.args, call.args + call.argc};
}

template <typename T> std::uintptr_t u(T v) {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<std::uintptr_t>(v);
    } else {
        return static_cast<std::uintptr_t>(v);
    }
}

template <typename H> H handle(std::uintptr_t value) { return reinterpret_cast<H>(value); }

} // namespace

TEST_CASE("the proxy exports exactly the ten WinG functions", "[proxy]") {
    REQUIRE(proxy() != nullptr);
    auto names = hr::platform::pe::export_names(proxy());
    std::vector<std::string> expected(export_names.begin(), export_names.end());
    std::ranges::sort(names);
    CHECK(names == expected);
}

TEST_CASE("every export forwards its arguments and result to the original", "[proxy]") {
    const auto dc = handle<HDC>(11);
    const auto other_dc = handle<HDC>(22);

    SECTION("WinGCreateDC") {
        CHECK(proxy_fn<CreateDC_t>("WinGCreateDC")() == handle<HDC>(0x1001));
        CHECK(std::string(last_call().function) == "WinGCreateDC");
        CHECK(last_call().argc == 0);
    }
    SECTION("WinGRecommendDIBFormat") {
        BITMAPINFO info{};
        CHECK(proxy_fn<RecommendDIBFormat_t>("WinGRecommendDIBFormat")(&info) == TRUE);
        CHECK(std::string(last_call().function) == "WinGRecommendDIBFormat");
        CHECK(args_of(last_call()) == std::vector{u(&info)});
    }
    SECTION("WinGCreateBitmap") {
        BITMAPINFO info{};
        void* bits = nullptr;
        CHECK(proxy_fn<CreateBitmap_t>("WinGCreateBitmap")(dc, &info, &bits) ==
              handle<HBITMAP>(0x1002));
        CHECK(bits == handle<void*>(0x1003));
        CHECK(std::string(last_call().function) == "WinGCreateBitmap");
        CHECK(args_of(last_call()) == std::vector{u(dc), u(&info), u(&bits)});
    }
    SECTION("WinGGetDIBPointer") {
        BITMAPINFO info{};
        CHECK(proxy_fn<GetDIBPointer_t>("WinGGetDIBPointer")(handle<HBITMAP>(33), &info) ==
              handle<void*>(0x1004));
        CHECK(std::string(last_call().function) == "WinGGetDIBPointer");
        CHECK(args_of(last_call()) == std::vector{u(handle<HBITMAP>(33)), u(&info)});
    }
    SECTION("WinGGetDIBColorTable") {
        RGBQUAD colors[4]{};
        CHECK(proxy_fn<GetDIBColorTable_t>("WinGGetDIBColorTable")(dc, 2, 4, colors) == 4u);
        CHECK(std::string(last_call().function) == "WinGGetDIBColorTable");
        CHECK(args_of(last_call()) == std::vector{u(dc), u(2u), u(4u), u(&colors[0])});
    }
    SECTION("WinGSetDIBColorTable") {
        const RGBQUAD colors[3]{};
        CHECK(proxy_fn<SetDIBColorTable_t>("WinGSetDIBColorTable")(dc, 1, 3, colors) == 3u);
        CHECK(std::string(last_call().function) == "WinGSetDIBColorTable");
        CHECK(args_of(last_call()) == std::vector{u(dc), u(1u), u(3u), u(&colors[0])});
    }
    SECTION("WinGCreateHalftonePalette") {
        CHECK(proxy_fn<CreateHalftonePalette_t>("WinGCreateHalftonePalette")() ==
              handle<HPALETTE>(0x1005));
        CHECK(std::string(last_call().function) == "WinGCreateHalftonePalette");
        CHECK(last_call().argc == 0);
    }
    SECTION("WinGCreateHalftoneBrush") {
        CHECK(proxy_fn<CreateHalftoneBrush_t>("WinGCreateHalftoneBrush")(dc, RGB(1, 2, 3), 2) ==
              handle<HBRUSH>(0x1006));
        CHECK(std::string(last_call().function) == "WinGCreateHalftoneBrush");
        CHECK(args_of(last_call()) == std::vector{u(dc), u(RGB(1, 2, 3)), u(2)});
    }
    SECTION("WinGBitBlt") {
        CHECK(proxy_fn<BitBlt_t>("WinGBitBlt")(dc, 1, 2, 3, 4, other_dc, 5, 6) == TRUE);
        CHECK(std::string(last_call().function) == "WinGBitBlt");
        CHECK(args_of(last_call()) ==
              std::vector{u(dc), u(1), u(2), u(3), u(4), u(other_dc), u(5), u(6)});
    }
    SECTION("WinGStretchBlt") {
        CHECK(proxy_fn<StretchBlt_t>("WinGStretchBlt")(dc, 1, 2, 3, 4, other_dc, 5, 6, 7, 8) ==
              TRUE);
        CHECK(std::string(last_call().function) == "WinGStretchBlt");
        CHECK(args_of(last_call()) == std::vector{u(dc), u(1), u(2), u(3), u(4), u(other_dc),
                                                  u(5), u(6), u(7), u(8)});
    }
}
```

Update `tests/platform/CMakeLists.txt`:

```cmake
add_executable(platform_tests pe_test.cpp wing_forward_test.cpp proxy_test.cpp)
target_include_directories(platform_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(platform_tests PRIVATE hr_platform hr_warnings Catch2::Catch2WithMain)
add_dependencies(platform_tests fake_wing wing32)
catch_discover_tests(platform_tests ADD_TAGS_AS_LABELS)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug`
Expected: FAIL — `add_dependencies ... target "wing32" does not exist` (the proxy has not been created yet).

- [ ] **Step 3: Write the minimal implementation**

Add `add_subdirectory(src/wing32)` directly after `add_subdirectory(src/platform)` in the root `CMakeLists.txt`.

`src/wing32/CMakeLists.txt`:

```cmake
add_library(wing32 SHARED dllmain.cpp exports.cpp runtime.cpp wing32.def)
set_target_properties(wing32 PROPERTIES PREFIX "" OUTPUT_NAME wing32)
target_link_libraries(wing32 PRIVATE hr_platform hr_warnings)
target_compile_definitions(wing32 PRIVATE HR_VERSION="${PROJECT_VERSION}")
```

`src/wing32/wing32.def`:

```
LIBRARY wing32
EXPORTS
    WinGBitBlt
    WinGCreateBitmap
    WinGCreateDC
    WinGCreateHalftoneBrush
    WinGCreateHalftonePalette
    WinGGetDIBColorTable
    WinGGetDIBPointer
    WinGRecommendDIBFormat
    WinGSetDIBColorTable
    WinGStretchBlt
```

`src/wing32/runtime.h`:

```cpp
#pragma once

#include "platform/wing_api.h"

#include <filesystem>

namespace hr::wing32 {

// Directory containing our wing32.dll (the game's WARFB folder when installed).
std::filesystem::path module_dir();

// The original WinG, loaded from wing32_orig.dll on first use. If it cannot be loaded the user
// sees one message box and the process exits: the game cannot run without it (spec §6.2).
const platform::wing::Functions& original();

} // namespace hr::wing32
```

`src/wing32/runtime.cpp`:

```cpp
#include "wing32/runtime.h"

#include "platform/wing_forward.h"

#include <string>

namespace hr::wing32 {
namespace {

HMODULE this_module() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&this_module), &module);
    return module;
}

[[noreturn]] void fatal(const std::string& message) {
    MessageBoxA(nullptr, message.c_str(), "horned-rat", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

} // namespace

std::filesystem::path module_dir() {
    wchar_t buffer[MAX_PATH];
    const DWORD n = GetModuleFileNameW(this_module(), buffer, MAX_PATH);
    return std::filesystem::path(std::wstring(buffer, n)).parent_path();
}

const platform::wing::Functions& original() {
    static const platform::wing::Functions functions = [] {
        auto result = platform::load_original_wing(module_dir() / "wing32_orig.dll");
        if (!result.functions) {
            fatal(result.error +
                  "\n\nThe original WinG library (wing32_orig.dll) is missing or damaged. Use GOG "
                  "Galaxy \"Verify / Repair\" on the game, then run tools/install.ps1 again.");
        }
        return *result.functions;
    }();
    return functions;
}

} // namespace hr::wing32
```

`src/wing32/exports.cpp`:

```cpp
// The ten WinG exports. Undecorated names come from wing32.def.
#include "wing32/runtime.h"

using hr::wing32::original;

extern "C" {

HDC WINAPI WinGCreateDC() { return original().create_dc(); }

BOOL WINAPI WinGRecommendDIBFormat(BITMAPINFO* format) {
    return original().recommend_dib_format(format);
}

HBITMAP WINAPI WinGCreateBitmap(HDC dc, const BITMAPINFO* header, void** bits) {
    return original().create_bitmap(dc, header, bits);
}

void* WINAPI WinGGetDIBPointer(HBITMAP bitmap, BITMAPINFO* header) {
    return original().get_dib_pointer(bitmap, header);
}

UINT WINAPI WinGGetDIBColorTable(HDC dc, UINT start, UINT count, RGBQUAD* colors) {
    return original().get_dib_color_table(dc, start, count, colors);
}

UINT WINAPI WinGSetDIBColorTable(HDC dc, UINT start, UINT count, const RGBQUAD* colors) {
    return original().set_dib_color_table(dc, start, count, colors);
}

HPALETTE WINAPI WinGCreateHalftonePalette() { return original().create_halftone_palette(); }

HBRUSH WINAPI WinGCreateHalftoneBrush(HDC dc, COLORREF color, int dither_type) {
    return original().create_halftone_brush(dc, color, dither_type);
}

BOOL WINAPI WinGBitBlt(HDC dest, int x, int y, int width, int height, HDC src, int src_x,
                       int src_y) {
    return original().bit_blt(dest, x, y, width, height, src, src_x, src_y);
}

BOOL WINAPI WinGStretchBlt(HDC dest, int x, int y, int width, int height, HDC src, int src_x,
                           int src_y, int src_width, int src_height) {
    return original().stretch_blt(dest, x, y, width, height, src, src_x, src_y, src_width,
                                  src_height);
}

} // extern "C"
```

`src/wing32/dllmain.cpp`:

```cpp
#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
    }
    return TRUE;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^proxy$"`
Expected: `100% tests passed, 0 tests failed out of 2`.

- [ ] **Step 5: Publish the DLL as a CI artifact**

Append to the `windows-x86` job in `.github/workflows/ci.yml` (after `Test`):

```yaml
      - name: Package
        shell: pwsh
        run: |
          New-Item -ItemType Directory -Force dist | Out-Null
          Copy-Item build/ci/bin/wing32.dll dist/
      - uses: actions/upload-artifact@v4
        with:
          name: horned-rat-windows-x86
          path: dist/
```

(`install.ps1` arrives in Task 7; until then only the DLL is packaged.)

- [ ] **Step 6: Format, commit, push, check CI**

Run Format, then:

```bash
git add CMakeLists.txt src/wing32 tests/platform .github/workflows/ci.yml
git commit -m "wing32: proxy DLL forwarding all ten WinG exports" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
# Wait for CI (see top of plan)
```

Expected: all jobs green, artifact `horned-rat-windows-x86` contains `wing32.dll`.

---

### Task 7: Installer script

**Files:**
- Create: `tools/install.ps1`
- Test: `tests/tools/install.Tests.ps1`
- Modify: `.github/workflows/ci.yml` (Pester step)

**Interfaces:**
- Consumes: `bin/wing32.dll` from Task 6 (only for manual use; tests use a fake file).
- Produces: `tools/install.ps1 -GameDir <path> [-SourceDll <path>] [-Force]` and `tools/install.ps1 -GameDir <path> -Uninstall`. Hidden-for-tests parameter `-SupportedHashes @{ 'WHSHR.EXE' = ...; 'wing32.dll' = ... }`.

- [ ] **Step 1: Make sure Pester 5 is available**

Run: `if (-not (Get-Module -ListAvailable Pester | Where-Object Version -ge '5.5.0')) { Install-Module Pester -MinimumVersion 5.5.0 -Scope CurrentUser -Force -SkipPublisherCheck }; (Get-Module -ListAvailable Pester | Sort-Object Version -Descending | Select-Object -First 1).Version`
Expected: a version ≥ 5.5.0.

- [ ] **Step 2: Write the failing test**

`tests/tools/install.Tests.ps1`:

```powershell
BeforeAll {
    $script:Installer = Join-Path $PSScriptRoot '..\..\tools\install.ps1'

    function New-FakeGame([string] $Root) {
        $warfb = Join-Path $Root 'WARFB'
        New-Item -ItemType Directory -Path $warfb -Force | Out-Null
        Set-Content -LiteralPath (Join-Path $warfb 'WHSHR.EXE') -Value 'fake exe' -NoNewline
        Set-Content -LiteralPath (Join-Path $warfb 'wing32.dll') -Value 'original wing' -NoNewline
        Set-Content -LiteralPath (Join-Path $Root 'proxy.dll') -Value 'proxy wing' -NoNewline
        $warfb
    }

    function Get-Hash([string] $Path) {
        (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

Describe 'install.ps1' {
    BeforeEach {
        $root = Join-Path $TestDrive ([guid]::NewGuid())
        $warfb = New-FakeGame $root
        $proxy = Join-Path $root 'proxy.dll'
        $hashes = @{
            'WHSHR.EXE'  = Get-Hash (Join-Path $warfb 'WHSHR.EXE')
            'wing32.dll' = Get-Hash (Join-Path $warfb 'wing32.dll')
        }
    }

    It 'keeps the original as wing32_orig.dll and copies the proxy' {
        & $Installer -GameDir $root -SourceDll $proxy -SupportedHashes $hashes | Out-Null
        Get-Content -Raw (Join-Path $warfb 'wing32_orig.dll') | Should -Be 'original wing'
        Get-Content -Raw (Join-Path $warfb 'wing32.dll') | Should -Be 'proxy wing'
    }

    It 'accepts the WARFB folder itself as GameDir' {
        & $Installer -GameDir $warfb -SourceDll $proxy -SupportedHashes $hashes | Out-Null
        Get-Content -Raw (Join-Path $warfb 'wing32.dll') | Should -Be 'proxy wing'
    }

    It 'refuses an unsupported WHSHR.EXE and changes nothing' {
        $hashes['WHSHR.EXE'] = 'deadbeef'
        { & $Installer -GameDir $root -SourceDll $proxy -SupportedHashes $hashes } | Should -Throw '*WHSHR.EXE*'
        Get-Content -Raw (Join-Path $warfb 'wing32.dll') | Should -Be 'original wing'
        Join-Path $warfb 'wing32_orig.dll' | Should -Not -Exist
    }

    It 'refuses an unsupported wing32.dll' {
        $hashes['wing32.dll'] = 'deadbeef'
        { & $Installer -GameDir $root -SourceDll $proxy -SupportedHashes $hashes } | Should -Throw '*wing32.dll*'
    }

    It 'installs anyway with -Force' {
        $hashes['WHSHR.EXE'] = 'deadbeef'
        & $Installer -GameDir $root -SourceDll $proxy -SupportedHashes $hashes -Force | Out-Null
        Get-Content -Raw (Join-Path $warfb 'wing32.dll') | Should -Be 'proxy wing'
    }

    It 'is safe to run twice: the original is never overwritten' {
        & $Installer -GameDir $root -SourceDll $proxy -SupportedHashes $hashes | Out-Null
        & $Installer -GameDir $root -SourceDll $proxy -SupportedHashes $hashes | Out-Null
        Get-Content -Raw (Join-Path $warfb 'wing32_orig.dll') | Should -Be 'original wing'
    }

    It 'uninstalls back to the original state' {
        & $Installer -GameDir $root -SourceDll $proxy -SupportedHashes $hashes | Out-Null
        & $Installer -GameDir $root -Uninstall -SupportedHashes $hashes | Out-Null
        Get-Content -Raw (Join-Path $warfb 'wing32.dll') | Should -Be 'original wing'
        Join-Path $warfb 'wing32_orig.dll' | Should -Not -Exist
    }

    It 'refuses to uninstall when not installed and points to GOG Verify / Repair' {
        { & $Installer -GameDir $root -Uninstall -SupportedHashes $hashes } | Should -Throw '*Verify / Repair*'
    }

    It 'fails clearly when WHSHR.EXE cannot be found' {
        { & $Installer -GameDir $TestDrive -SourceDll $proxy -SupportedHashes $hashes } | Should -Throw '*WHSHR.EXE not found*'
    }
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `Invoke-Pester -Path tests/tools -Output Detailed`
Expected: 9 tests FAIL (the script does not exist; errors mention `install.ps1` not being recognised).

- [ ] **Step 4: Write the minimal implementation**

`tools/install.ps1`:

```powershell
<#
.SYNOPSIS
Installs or uninstalls the horned-rat WinG proxy in the GOG release of Shadow of the Horned Rat.

.EXAMPLE
./install.ps1 -GameDir 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat'

.EXAMPLE
./install.ps1 -GameDir '...\Warhammer - Shadow of the Horned Rat' -Uninstall
#>
[CmdletBinding(DefaultParameterSetName = 'Install')]
param(
    # The GOG game folder, or its WARFB subfolder.
    [Parameter(Mandatory)] [string] $GameDir,

    # The proxy DLL to install.
    [Parameter(ParameterSetName = 'Install')] [string] $SourceDll = (Join-Path $PSScriptRoot 'wing32.dll'),

    # Install even if the game files do not match the supported build.
    [Parameter(ParameterSetName = 'Install')] [switch] $Force,

    [Parameter(ParameterSetName = 'Uninstall', Mandatory)] [switch] $Uninstall,

    # SHA-256 of the supported build (spec §8). Overridable for tests only.
    [hashtable] $SupportedHashes = @{
        'WHSHR.EXE'  = 'df462ba4dc29d0832c44f14207b47459f281e6dd4a9e3f3ad972fd43a5921326'
        'wing32.dll' = 'dfc3a949141dad973ad2eb4b66e3c8dc8dc7851fe8f360cd42697aab439d70c0'
    }
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Get-Sha256([string] $Path) {
    (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

if (Test-Path -LiteralPath (Join-Path $GameDir 'WHSHR.EXE')) {
    $warfb = $GameDir
} elseif (Test-Path -LiteralPath (Join-Path $GameDir 'WARFB\WHSHR.EXE')) {
    $warfb = Join-Path $GameDir 'WARFB'
} else {
    throw "WHSHR.EXE not found in '$GameDir' or its WARFB subfolder."
}
$exe = Join-Path $warfb 'WHSHR.EXE'
$wing = Join-Path $warfb 'wing32.dll'
$orig = Join-Path $warfb 'wing32_orig.dll'

if ($Uninstall) {
    if (-not (Test-Path -LiteralPath $orig)) {
        throw "wing32_orig.dll not found in '$warfb': horned-rat is not installed, or the original was lost. Use GOG Galaxy 'Verify / Repair' to restore the original game files."
    }
    Remove-Item -LiteralPath $wing -Force -ErrorAction SilentlyContinue
    Rename-Item -LiteralPath $orig -NewName 'wing32.dll'
    Write-Output "Uninstalled horned-rat from '$warfb'."
    return
}

if (-not (Test-Path -LiteralPath $SourceDll)) {
    throw "Proxy DLL not found: '$SourceDll'."
}
$originalWing = if (Test-Path -LiteralPath $orig) { $orig } else { $wing }
if (-not $Force) {
    if ((Get-Sha256 $exe) -ne $SupportedHashes['WHSHR.EXE']) {
        throw 'Unsupported game version: WHSHR.EXE does not match the supported GOG build. Use -Force to install anyway.'
    }
    if ((Get-Sha256 $originalWing) -ne $SupportedHashes['wing32.dll']) {
        throw 'Unsupported game version: the original wing32.dll does not match the supported GOG build. Use -Force to install anyway.'
    }
}
if (-not (Test-Path -LiteralPath $orig)) {
    Rename-Item -LiteralPath $wing -NewName 'wing32_orig.dll'
}
Copy-Item -LiteralPath $SourceDll -Destination $wing -Force
Write-Output "Installed horned-rat into '$warfb'."
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `Invoke-Pester -Path tests/tools -Output Detailed`
Expected: `Tests Passed: 9, Failed: 0`.

- [ ] **Step 6: Run Pester in CI and package the script**

In `.github/workflows/ci.yml`, add before `Package` in `windows-x86`:

```yaml
      - name: Installer tests
        shell: pwsh
        run: Invoke-Pester -Path tests/tools -Output Detailed -CI
```

and replace the `Package` step's `run` with:

```yaml
        run: |
          New-Item -ItemType Directory -Force dist | Out-Null
          Copy-Item build/ci/bin/wing32.dll, tools/install.ps1 dist/
```

- [ ] **Step 7: Commit, push, check CI**

```bash
git add tools/install.ps1 tests/tools .github/workflows/ci.yml
git commit -m "tools: installer with hash check, safe reinstall and uninstall" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
# Wait for CI (see top of plan)
```

Expected: all jobs green; the artifact contains `wing32.dll` and `install.ps1`.

---

### Task 8: Game-data checks, smoke test, and the A0 milestone on the real game

**Files:**
- Create: `tests/gamedata/CMakeLists.txt`, `tests/gamedata/gamedata_test.cpp`, `tests/tools/gamedata.Tests.ps1`, `docs/smoke-test.md`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `pe::export_names` (Task 4), `wing::export_names` (Task 4), `bin/wing32.dll` (Task 6), `tools/install.ps1` (Task 7).
- Produces: CTest label `gamedata` (skipped unless `SOTHR_GAME_DIR` is set), `docs/smoke-test.md`.

- [ ] **Step 1: Write the opt-in tests**

`tests/gamedata/gamedata_test.cpp`:

```cpp
// Opt-in checks against the user's own game install. Skipped unless SOTHR_GAME_DIR points at the
// GOG game folder. Never runs in CI.
#include "platform/pe.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::filesystem::path game_dir_or_skip() {
    wchar_t buffer[1024];
    const DWORD n = GetEnvironmentVariableW(L"SOTHR_GAME_DIR", buffer, 1024);
    if (n == 0 || n >= 1024) {
        SKIP("SOTHR_GAME_DIR is not set");
    }
    return std::filesystem::path(std::wstring(buffer, n));
}

std::filesystem::path bin_dir() {
    wchar_t buffer[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(std::wstring(buffer, n)).parent_path();
}

std::vector<std::string> sorted_exports(const std::filesystem::path& dll) {
    const HMODULE module = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    REQUIRE(module != nullptr);
    auto names = hr::platform::pe::export_names(module);
    FreeLibrary(module);
    std::ranges::sort(names);
    return names;
}

} // namespace

TEST_CASE("our proxy exports exactly the names of the real WinG", "[gamedata]") {
    const auto warfb = game_dir_or_skip() / "WARFB";
    const auto real = std::filesystem::exists(warfb / "wing32_orig.dll") ? warfb / "wing32_orig.dll"
                                                                          : warfb / "wing32.dll";
    REQUIRE(std::filesystem::exists(real));
    CHECK(sorted_exports(bin_dir() / "wing32.dll") == sorted_exports(real));
}
```

`tests/gamedata/CMakeLists.txt`:

```cmake
add_executable(gamedata_tests gamedata_test.cpp)
target_link_libraries(gamedata_tests PRIVATE hr_platform hr_warnings Catch2::Catch2WithMain)
add_dependencies(gamedata_tests wing32)
# Catch2 exits with 4 when every test was skipped.
catch_discover_tests(gamedata_tests ADD_TAGS_AS_LABELS PROPERTIES SKIP_RETURN_CODE 4)
```

`tests/CMakeLists.txt`:

```cmake
add_subdirectory(core)
if(NOT HR_CORE_ONLY)
  add_subdirectory(platform)
  add_subdirectory(gamedata)
endif()
```

`tests/tools/gamedata.Tests.ps1`:

```powershell
# Opt-in: verifies the local game install is the build this project supports (spec §8).
Describe 'local game install' -Skip:(-not $env:SOTHR_GAME_DIR) {
    BeforeAll {
        $warfb = Join-Path $env:SOTHR_GAME_DIR 'WARFB'
        function Get-Hash([string] $Path) { (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
    }

    It 'has the supported WHSHR.EXE' {
        Get-Hash (Join-Path $warfb 'WHSHR.EXE') | Should -Be 'df462ba4dc29d0832c44f14207b47459f281e6dd4a9e3f3ad972fd43a5921326'
    }

    It 'has the supported original wing32.dll' {
        $orig = Join-Path $warfb 'wing32_orig.dll'
        $path = if (Test-Path -LiteralPath $orig) { $orig } else { Join-Path $warfb 'wing32.dll' }
        Get-Hash $path | Should -Be 'dfc3a949141dad973ad2eb4b66e3c8dc8dc7851fe8f360cd42697aab439d70c0'
    }
}
```

- [ ] **Step 2: Verify the tests are skipped without the variable (the CI situation)**

Run: `Remove-Item Env:SOTHR_GAME_DIR -ErrorAction SilentlyContinue; . ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^gamedata$"; Invoke-Pester -Path tests/tools/gamedata.Tests.ps1 -Output Detailed`
Expected: CTest reports the test as `Skipped` (not failed); Pester reports `Skipped: 2`.

- [ ] **Step 3: Red — prove the export check can fail**

Temporarily remove the `WinGStretchBlt` line from `src/wing32/wing32.def`. Run:
`$env:SOTHR_GAME_DIR = 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat'; . ./tools/vsenv.ps1; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^gamedata$"`
Expected: FAIL, the two name lists differ by `WinGStretchBlt`. (The `proxy` tests fail too.)

- [ ] **Step 4: Green — restore the line**

Restore `WinGStretchBlt` in `wing32.def`, rebuild, and run:
`$env:SOTHR_GAME_DIR = 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat'; . ./tools/vsenv.ps1; cmake --build --preset x86-debug; ctest --preset x86-debug; Invoke-Pester -Path tests/tools -Output Detailed`
Expected: all CTest tests pass (including `gamedata`); Pester `Passed: 11`.

- [ ] **Step 5: Write the smoke test checklist**

`docs/smoke-test.md`:

```markdown
# Manual smoke test

Run before every release and at every milestone exit. Record the date, build (git hash) and
result of each item in the PR or release notes. Never commit game logs or screenshots.

## Setup
1. `. ./tools/vsenv.ps1; cmake --preset x86-release; cmake --build --preset x86-release`
2. `./tools/install.ps1 -GameDir '<GOG game folder>' -SourceDll build/x86-release/bin/wing32.dll`
3. Launch the game from GOG Galaxy.

## Checklist
- [ ] Intro/video plays (or can be skipped) without a crash.
- [ ] Main menu appears; mouse clicks hit the right buttons.
- [ ] Start a new campaign; the campaign map appears and scrolls.
- [ ] Enter the first battle; deploy, move and attack with a unit.
- [ ] A palette effect (fade to/from black between screens) looks correct.
- [ ] Save the game, quit to the menu, load the save.
- [ ] Quit the game normally; no error dialog.
- [ ] `horned-rat.log` exists in `WARFB\` and has no `exception` lines.
- [ ] Uninstall: `./tools/install.ps1 -GameDir '<GOG game folder>' -Uninstall`; the game still starts.

Later milestones add items here (window resizing, scale modes, Alt+Enter, ...).
```

- [ ] **Step 6: A0 exit — run the smoke test on the real game**

This modifies the user's game folder: **ask the user for confirmation first.** If the install fails with "access denied", the GOG folder needs an elevated PowerShell — ask the user to run the install command themselves.

Run: `. ./tools/vsenv.ps1; cmake --preset x86-release; cmake --build --preset x86-release; ./tools/install.ps1 -GameDir 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat' -SourceDll build/x86-release/bin/wing32.dll`
Expected: `Installed horned-rat into '...\WARFB'.`

The user then plays through `docs/smoke-test.md` (the log item does not apply yet). Expected: the game behaves exactly as without the proxy.

- [ ] **Step 7: Commit, push, check CI**

```bash
git add tests docs/smoke-test.md
git commit -m "test: opt-in game-data checks; manual smoke test checklist" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
# Wait for CI (see top of plan)
```

Expected: CI green (the gamedata tests show as skipped). **Milestone A0 is complete.**

---

### Task 9: Log line formatting and call sampling (core)

**Files:**
- Create: `src/core/log_format.h`, `src/core/log_format.cpp`, `src/core/sample_limiter.h`
- Modify: `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`
- Test: `tests/core/log_format_test.cpp`, `tests/core/sample_limiter_test.cpp`

**Interfaces:**
- Produces:
  - `std::string hr::core::format_log_line(double seconds, std::string_view message);` → `"[%9.3f] <message>\n"`; trailing CR/LF removed, interior CR/LF replaced by spaces.
  - `struct hr::core::SampleLimiter { std::uint64_t first = 50; std::uint64_t every = 1000; constexpr bool should_log(std::uint64_t call_number) const; };` (1-based call numbers).

- [ ] **Step 1: Write the failing tests**

`tests/core/log_format_test.cpp`:

```cpp
#include "core/log_format.h"

#include <catch2/catch_test_macros.hpp>

using hr::core::format_log_line;

TEST_CASE("a log line has a fixed-width timestamp and ends with a newline", "[log_format]") {
    CHECK(format_log_line(12.3456, "hello") == "[   12.346] hello\n");
    CHECK(format_log_line(0.0, "") == "[    0.000] \n");
}

TEST_CASE("trailing line breaks are removed", "[log_format]") {
    CHECK(format_log_line(0.0, "msg\r\n") == "[    0.000] msg\n");
    CHECK(format_log_line(0.0, "msg\n\n") == "[    0.000] msg\n");
}

TEST_CASE("interior line breaks become spaces so one record is one line", "[log_format]") {
    CHECK(format_log_line(0.0, "a\nb\r\nc") == "[    0.000] a b  c\n");
}
```

`tests/core/sample_limiter_test.cpp`:

```cpp
#include "core/sample_limiter.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("the first calls are always logged, later only every Nth", "[sample_limiter]") {
    constexpr hr::core::SampleLimiter limiter{};
    CHECK(limiter.should_log(1));
    CHECK(limiter.should_log(50));
    CHECK_FALSE(limiter.should_log(51));
    CHECK_FALSE(limiter.should_log(999));
    CHECK(limiter.should_log(1000));
    CHECK_FALSE(limiter.should_log(1001));
    CHECK(limiter.should_log(2000));
}

TEST_CASE("the limits are configurable", "[sample_limiter]") {
    constexpr hr::core::SampleLimiter limiter{2, 10};
    CHECK(limiter.should_log(2));
    CHECK_FALSE(limiter.should_log(3));
    CHECK(limiter.should_log(10));
}
```

`tests/core/CMakeLists.txt`:

```cmake
add_executable(core_tests ini_test.cpp config_test.cpp log_format_test.cpp sample_limiter_test.cpp)
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'core/log_format.h'`.

- [ ] **Step 3: Write the minimal implementation**

`src/core/log_format.h`:

```cpp
#pragma once

#include <string>
#include <string_view>

namespace hr::core {

// "[    12.346] message\n": one record per line, whatever line breaks `message` contains.
std::string format_log_line(double seconds, std::string_view message);

} // namespace hr::core
```

`src/core/log_format.cpp`:

```cpp
#include "core/log_format.h"

#include <cstdio>

namespace hr::core {

std::string format_log_line(double seconds, std::string_view message) {
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
        message.remove_suffix(1);
    }
    char stamp[32];
    std::snprintf(stamp, sizeof stamp, "[%9.3f] ", seconds);
    std::string line = stamp;
    for (const char c : message) {
        line += (c == '\n' || c == '\r') ? ' ' : c;
    }
    line += '\n';
    return line;
}

} // namespace hr::core
```

`src/core/sample_limiter.h`:

```cpp
#pragma once

#include <cstdint>

namespace hr::core {

// Decides which calls of a frequently called function get logged: the first `first` calls,
// then every `every`-th call. Call numbers start at 1.
struct SampleLimiter {
    std::uint64_t first = 50;
    std::uint64_t every = 1000;

    constexpr bool should_log(std::uint64_t call_number) const {
        return call_number <= first || call_number % every == 0;
    }
};

} // namespace hr::core
```

`src/core/CMakeLists.txt`:

```cmake
add_library(hr_core STATIC ini.cpp config.cpp log_format.cpp)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^(log_format|sample_limiter)$"`
Expected: `100% tests passed, 0 tests failed out of 5`.

- [ ] **Step 5: Format and commit**

Run Format, then:

```bash
git add src/core tests/core
git commit -m "core: log line formatting and call sampling" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 10: Thread-safe file log

**Files:**
- Create: `src/platform/log.h`, `src/platform/log.cpp`
- Modify: `src/platform/CMakeLists.txt`, `tests/platform/CMakeLists.txt`
- Test: `tests/platform/log_test.cpp`

**Interfaces:**
- Consumes: `core::format_log_line` (Task 9).
- Produces:
  - `class hr::platform::Log { public: using Clock = std::function<double()>; explicit Log(const std::filesystem::path& path, Clock clock = {}); void write(std::string_view message) noexcept; };` — truncates the file, binary mode (LF line endings), flushes every line; default clock = seconds since construction.
  - `void hr::platform::set_global_log(Log* log) noexcept;`
  - `void hr::platform::log(std::string_view message) noexcept;` — no-op without a global log.
  - `void hr::platform::log_exception(const char* where, const char* what) noexcept;` — writes `exception in <where>: <what>` (or `unknown exception in <where>` when `what` is null).

- [ ] **Step 1: Write the failing test**

`tests/platform/log_test.cpp`:

```cpp
#include "platform/log.h"
#include "test_paths.h"

#include <catch2/catch_test_macros.hpp>

#include <regex>
#include <sstream>
#include <thread>
#include <vector>

using hr::platform::Log;

TEST_CASE("Log writes formatted, timestamped lines", "[log]") {
    const auto path = hr::test::temp_file("log_basic.txt");
    {
        double now = 0.5;
        Log log(path, [&] { return now; });
        log.write("first");
        now = 2.25;
        log.write("second\n");
    }
    CHECK(hr::test::read_file(path) == "[    0.500] first\n[    2.250] second\n");
}

TEST_CASE("Log truncates an existing file", "[log]") {
    const auto path = hr::test::temp_file("log_truncate.txt");
    {
        Log first(path, [] { return 0.0; });
        first.write("old");
    }
    {
        Log second(path, [] { return 0.0; });
        second.write("new");
    }
    CHECK(hr::test::read_file(path) == "[    0.000] new\n");
}

TEST_CASE("concurrent writes never interleave within a line", "[log]") {
    const auto path = hr::test::temp_file("log_threads.txt");
    {
        Log log(path, [] { return 1.0; });
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&log, t] {
                for (int i = 0; i < 200; ++i) {
                    log.write("thread " + std::to_string(t) + " line " + std::to_string(i));
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
    }
    std::istringstream lines(hr::test::read_file(path));
    const std::regex well_formed(R"(\[ *1\.000\] thread \d line \d+)");
    int count = 0;
    for (std::string line; std::getline(lines, line);) {
        INFO(line);
        CHECK(std::regex_match(line, well_formed));
        ++count;
    }
    CHECK(count == 1600);
}

TEST_CASE("the global log forwards to the installed Log and is a no-op without one", "[log]") {
    const auto path = hr::test::temp_file("log_global.txt");
    hr::platform::log("dropped: no global log installed");
    {
        Log log(path, [] { return 0.0; });
        hr::platform::set_global_log(&log);
        hr::platform::log("kept");
        hr::platform::log_exception("Hook", "boom");
        hr::platform::log_exception("Hook", nullptr);
        hr::platform::set_global_log(nullptr);
    }
    CHECK(hr::test::read_file(path) == "[    0.000] kept\n"
                                       "[    0.000] exception in Hook: boom\n"
                                       "[    0.000] unknown exception in Hook\n");
}
```

Add `log_test.cpp` to `add_executable(platform_tests ...)` in `tests/platform/CMakeLists.txt`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'platform/log.h'`.

- [ ] **Step 3: Write the minimal implementation**

`src/platform/log.h`:

```cpp
#pragma once

#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string_view>

namespace hr::platform {

class Log {
public:
    using Clock = std::function<double()>; // seconds; defaults to time since construction

    explicit Log(const std::filesystem::path& path, Clock clock = {});
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    void write(std::string_view message) noexcept;

private:
    std::mutex mutex_;
    std::ofstream file_;
    Clock clock_;
};

void set_global_log(Log* log) noexcept;
void log(std::string_view message) noexcept;
void log_exception(const char* where, const char* what) noexcept;

} // namespace hr::platform
```

`src/platform/log.cpp`:

```cpp
#include "platform/log.h"

#include "core/log_format.h"

#include <atomic>
#include <chrono>
#include <string>

namespace hr::platform {
namespace {

std::atomic<Log*> g_log{nullptr};

Log::Clock steady_since_now() {
    return [start = std::chrono::steady_clock::now()] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
}

} // namespace

Log::Log(const std::filesystem::path& path, Clock clock)
    : file_(path, std::ios::binary | std::ios::trunc),
      clock_(clock ? std::move(clock) : steady_since_now()) {}

void Log::write(std::string_view message) noexcept {
    try {
        const std::lock_guard lock(mutex_);
        file_ << core::format_log_line(clock_(), message);
        file_.flush();
    } catch (...) {
        // Logging must never take the game down.
    }
}

void set_global_log(Log* log) noexcept { g_log.store(log); }

void log(std::string_view message) noexcept {
    if (Log* const target = g_log.load()) {
        target->write(message);
    }
}

void log_exception(const char* where, const char* what) noexcept {
    try {
        if (what != nullptr) {
            log(std::string("exception in ") + where + ": " + what);
        } else {
            log(std::string("unknown exception in ") + where);
        }
    } catch (...) {
    }
}

} // namespace hr::platform
```

`src/platform/CMakeLists.txt`:

```cmake
add_library(hr_platform STATIC pe.cpp wing_forward.cpp log.cpp)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^log$"`
Expected: `100% tests passed, 0 tests failed out of 4`.

- [ ] **Step 5: Format and commit**

Run Format, then:

```bash
git add src/platform tests/platform
git commit -m "platform: thread-safe file log" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Exception guard for hook bodies

**Files:**
- Create: `src/platform/guard.h`
- Modify: `tests/platform/CMakeLists.txt`
- Test: `tests/platform/guard_test.cpp`

**Interfaces:**
- Consumes: `log_exception` (Task 10).
- Produces: `template <typename Body, typename Fallback> auto hr::platform::guarded(const char* where, Body&& body, Fallback&& fallback) noexcept -> decltype(body());` — returns `body()`, or logs and returns `fallback()` if `body` throws.

- [ ] **Step 1: Write the failing test**

`tests/platform/guard_test.cpp`:

```cpp
#include "platform/guard.h"
#include "platform/log.h"
#include "test_paths.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using hr::platform::guarded;

TEST_CASE("guarded returns the body's result when nothing throws", "[guard]") {
    CHECK(guarded("t", [] { return 1; }, [] { return 2; }) == 1);
}

TEST_CASE("guarded logs an exception and returns the fallback", "[guard]") {
    const auto path = hr::test::temp_file("guard.txt");
    {
        hr::platform::Log log(path, [] { return 0.0; });
        hr::platform::set_global_log(&log);
        const int r = guarded(
            "Hook", []() -> int { throw std::runtime_error("boom"); }, [] { return 2; });
        CHECK(r == 2);
        const int r2 = guarded(
            "Other", []() -> int { throw 42; }, [] { return 3; });
        CHECK(r2 == 3);
        hr::platform::set_global_log(nullptr);
    }
    CHECK(hr::test::read_file(path) == "[    0.000] exception in Hook: boom\n"
                                       "[    0.000] unknown exception in Other\n");
}

TEST_CASE("guarded supports void bodies", "[guard]") {
    bool fallback_ran = false;
    guarded(
        "v", [] { throw std::runtime_error("x"); }, [&] { fallback_ran = true; });
    CHECK(fallback_ran);
}
```

Add `guard_test.cpp` to `add_executable(platform_tests ...)`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'platform/guard.h'`.

- [ ] **Step 3: Write the minimal implementation**

`src/platform/guard.h`:

```cpp
#pragma once

#include "platform/log.h"

#include <exception>

namespace hr::platform {

// Runs `body`. If it throws, the exception is logged (tagged with `where`) and `fallback()` is
// returned instead. Wraps every hook body so that no C++ exception can unwind into game code
// (spec §6.2). `fallback` normally calls the original API function and must not throw.
template <typename Body, typename Fallback>
auto guarded(const char* where, Body&& body, Fallback&& fallback) noexcept -> decltype(body()) {
    try {
        return body();
    } catch (const std::exception& e) {
        log_exception(where, e.what());
    } catch (...) {
        log_exception(where, nullptr);
    }
    return fallback();
}

} // namespace hr::platform
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^guard$"`
Expected: `100% tests passed, 0 tests failed out of 3`.

- [ ] **Step 5: Format and commit**

Run Format, then:

```bash
git add src/platform tests/platform
git commit -m "platform: exception guard for hook bodies" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: Import address table hooks

**Files:**
- Modify: `src/platform/pe.h`, `src/platform/pe.cpp` (add `find_import_slot`)
- Create: `src/platform/iat_hook.h`, `src/platform/iat_hook.cpp`
- Create: `tests/platform/iat_fixture/iat_fixture.cpp`, `tests/platform/iat_fixture/iat_fixture.def`
- Modify: `src/platform/CMakeLists.txt`, `tests/platform/CMakeLists.txt`
- Test: `tests/platform/iat_hook_test.cpp`

**Interfaces:**
- Consumes: `pe.cpp`'s `nt_headers` helper (Task 4).
- Produces:
  - `void** hr::platform::pe::find_import_slot(HMODULE module, const char* dll, const char* function);` — DLL name compared case-insensitively; imports by ordinal are skipped; `nullptr` if not found or the module has no import name table.
  - `enum class hr::platform::HookResult { Patched, AlreadyPatched, NotImported, Failed };`
  - `HookResult hr::platform::hook_import(HMODULE module, const char* dll, const char* function, void* replacement);` (idempotent)
  - `const char* hr::platform::to_string(HookResult);` → `"patched"`, `"already patched"`, `"not imported"`, `"failed"`.

- [ ] **Step 1: Write the fixture and the failing test**

`tests/platform/iat_fixture/iat_fixture.cpp`:

```cpp
// A tiny DLL whose import the IAT hook tests redirect.
#include <windows.h>

extern "C" int WINAPI Fixture_Add(int a, int b) { return a + b; }
```

`tests/platform/iat_fixture/iat_fixture.def`:

```
LIBRARY iat_fixture
EXPORTS
    Fixture_Add
```

`tests/platform/iat_hook_test.cpp`:

```cpp
#include "platform/iat_hook.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <string>

// Imported from iat_fixture.dll, so every call goes through this executable's IAT.
extern "C" __declspec(dllimport) int WINAPI Fixture_Add(int a, int b);

using hr::platform::hook_import;
using hr::platform::HookResult;

namespace {

int WINAPI fake_add(int a, int b) { return a * 100 + b; }

void* real_add() {
    return reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"iat_fixture.dll"), "Fixture_Add"));
}

} // namespace

TEST_CASE("hook_import redirects a module's import and can restore it", "[iat]") {
    const HMODULE exe = GetModuleHandleW(nullptr);
    REQUIRE(real_add() != nullptr);
    REQUIRE(Fixture_Add(2, 3) == 5);

    CHECK(hook_import(exe, "iat_fixture.dll", "Fixture_Add", reinterpret_cast<void*>(&fake_add)) ==
          HookResult::Patched);
    CHECK(Fixture_Add(2, 3) == 203);

    // Idempotent, and the DLL name is matched case-insensitively.
    CHECK(hook_import(exe, "IAT_FIXTURE.DLL", "Fixture_Add", reinterpret_cast<void*>(&fake_add)) ==
          HookResult::AlreadyPatched);

    CHECK(hook_import(exe, "iat_fixture.dll", "Fixture_Add", real_add()) == HookResult::Patched);
    CHECK(Fixture_Add(2, 3) == 5);
}

TEST_CASE("hook_import reports imports that do not exist", "[iat]") {
    const HMODULE exe = GetModuleHandleW(nullptr);
    const auto replacement = reinterpret_cast<void*>(&fake_add);
    CHECK(hook_import(exe, "iat_fixture.dll", "No_Such_Function", replacement) ==
          HookResult::NotImported);
    CHECK(hook_import(exe, "no_such.dll", "Fixture_Add", replacement) == HookResult::NotImported);
}

TEST_CASE("HookResult has readable names", "[iat]") {
    CHECK(std::string(hr::platform::to_string(HookResult::Patched)) == "patched");
    CHECK(std::string(hr::platform::to_string(HookResult::AlreadyPatched)) == "already patched");
    CHECK(std::string(hr::platform::to_string(HookResult::NotImported)) == "not imported");
    CHECK(std::string(hr::platform::to_string(HookResult::Failed)) == "failed");
}
```

`tests/platform/CMakeLists.txt` — add the fixture and link it:

```cmake
add_library(iat_fixture SHARED iat_fixture/iat_fixture.cpp iat_fixture/iat_fixture.def)
target_compile_definitions(iat_fixture PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_link_libraries(iat_fixture PRIVATE hr_warnings)
```

and change the test target to:

```cmake
add_executable(platform_tests pe_test.cpp wing_forward_test.cpp proxy_test.cpp log_test.cpp
                              guard_test.cpp iat_hook_test.cpp)
target_include_directories(platform_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(platform_tests PRIVATE hr_platform hr_warnings iat_fixture Catch2::Catch2WithMain)
add_dependencies(platform_tests fake_wing wing32)
catch_discover_tests(platform_tests ADD_TAGS_AS_LABELS)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'platform/iat_hook.h'`.

- [ ] **Step 3: Write the minimal implementation**

Add to `src/platform/pe.h` (inside the namespace):

```cpp
// The IAT slot through which `module` calls `function` imported by name from `dll` (DLL name
// compared case-insensitively). nullptr if there is no such import, or if the module has no
// import name table to match names against.
void** find_import_slot(HMODULE module, const char* dll, const char* function);
```

Add to `src/platform/pe.cpp`: `#include <cstring>` at the top, and inside `namespace hr::platform::pe` after `export_names`:

```cpp
void** find_import_slot(HMODULE module, const char* dll, const char* function) {
    const auto base = reinterpret_cast<BYTE*>(module);
    const auto nt = nt_headers(base);
    if (nt == nullptr) {
        return nullptr;
    }
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (dir.VirtualAddress == 0) {
        return nullptr;
    }
    for (auto desc = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress);
         desc->Name != 0; ++desc) {
        if (_stricmp(reinterpret_cast<const char*>(base + desc->Name), dll) != 0 ||
            desc->OriginalFirstThunk == 0) {
            continue;
        }
        auto names = reinterpret_cast<const IMAGE_THUNK_DATA*>(base + desc->OriginalFirstThunk);
        auto slots = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);
        for (; names->u1.AddressOfData != 0; ++names, ++slots) {
            if (IMAGE_SNAP_BY_ORDINAL(names->u1.Ordinal)) {
                continue;
            }
            const auto by_name =
                reinterpret_cast<const IMAGE_IMPORT_BY_NAME*>(base + names->u1.AddressOfData);
            if (std::strcmp(reinterpret_cast<const char*>(by_name->Name), function) == 0) {
                return reinterpret_cast<void**>(&slots->u1.Function);
            }
        }
    }
    return nullptr;
}
```

`src/platform/iat_hook.h`:

```cpp
#pragma once

#include <windows.h>

namespace hr::platform {

enum class HookResult { Patched, AlreadyPatched, NotImported, Failed };

// Points `module`'s import of `dll`!`function` at `replacement`. Idempotent: returns
// AlreadyPatched if the slot already holds `replacement`. Restoring is hooking with the original.
HookResult hook_import(HMODULE module, const char* dll, const char* function, void* replacement);

const char* to_string(HookResult result);

} // namespace hr::platform
```

`src/platform/iat_hook.cpp`:

```cpp
#include "platform/iat_hook.h"

#include "platform/pe.h"

namespace hr::platform {

HookResult hook_import(HMODULE module, const char* dll, const char* function, void* replacement) {
    void** const slot = pe::find_import_slot(module, dll, function);
    if (slot == nullptr) {
        return HookResult::NotImported;
    }
    if (*slot == replacement) {
        return HookResult::AlreadyPatched;
    }
    DWORD old_protection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protection)) {
        return HookResult::Failed;
    }
    *slot = replacement; // pointer-sized aligned store: atomic on x86
    VirtualProtect(slot, sizeof(void*), old_protection, &old_protection);
    return HookResult::Patched;
}

const char* to_string(HookResult result) {
    switch (result) {
    case HookResult::Patched:
        return "patched";
    case HookResult::AlreadyPatched:
        return "already patched";
    case HookResult::NotImported:
        return "not imported";
    case HookResult::Failed:
        return "failed";
    }
    return "?";
}

} // namespace hr::platform
```

`src/platform/CMakeLists.txt`:

```cmake
add_library(hr_platform STATIC pe.cpp wing_forward.cpp log.cpp iat_hook.cpp)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^(iat|pe)$"`
Expected: `100% tests passed, 0 tests failed out of 5`.

- [ ] **Step 5: Format, commit, push, check CI**

Run Format, then:

```bash
git add src/platform tests/platform
git commit -m "platform: import address table hooks" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
# Wait for CI (see top of plan)
```

Expected: CI green.

---

### Task 13: Which modules to hook, and when (core)

**Files:**
- Create: `src/core/game_modules.h`, `src/core/game_modules.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`
- Test: `tests/core/game_modules_test.cpp`

**Interfaces:**
- Consumes: `core::to_lower` (Task 1).
- Produces:
  - `bool hr::core::is_game_module(std::string_view module_path, std::string_view game_dir);` — true if the module lives in `game_dir` or below, excluding `wing32.dll` and `wing32_orig.dll`. Windows path rules: `\` or `/`, case-insensitive.
  - `bool hr::core::should_install_hooks(std::string_view exe_path, bool config_enabled, std::string_view disable_env);` — true only for `WHSHR.EXE` hosts, `enabled = true`, and `HORNED_RAT_DISABLE` ≠ `"1"`.

- [ ] **Step 1: Write the failing test**

`tests/core/game_modules_test.cpp`:

```cpp
#include "core/game_modules.h"

#include <catch2/catch_test_macros.hpp>

using hr::core::is_game_module;
using hr::core::should_install_hooks;

TEST_CASE("modules inside the game folder are game modules", "[game_modules]") {
    const auto dir = R"(D:\Games\SotHR\WARFB)";
    CHECK(is_game_module(R"(D:\Games\SotHR\WARFB\WHSHR.EXE)", dir));
    CHECK(is_game_module(R"(D:\Games\SotHR\WARFB\RL\SOFTF.DLL)", dir));
    CHECK(is_game_module(R"(d:/games/sothr/warfb/gamef.dll)", dir));
    CHECK(is_game_module(R"(D:\Games\SotHR\WARFB\GAMEF.DLL)", R"(D:\Games\SotHR\WARFB\)"));
}

TEST_CASE("modules outside the game folder are not game modules", "[game_modules]") {
    const auto dir = R"(D:\Games\SotHR\WARFB)";
    CHECK_FALSE(is_game_module(R"(C:\Windows\SysWOW64\gdi32.dll)", dir));
    CHECK_FALSE(is_game_module(R"(D:\Games\SotHR\WARFB2\X.DLL)", dir));
    CHECK_FALSE(is_game_module(R"(D:\Games\SotHR\WARFB)", dir));
}

TEST_CASE("our own DLL and the original WinG are never hooked", "[game_modules]") {
    const auto dir = R"(D:\Games\SotHR\WARFB)";
    CHECK_FALSE(is_game_module(R"(D:\Games\SotHR\WARFB\wing32.dll)", dir));
    CHECK_FALSE(is_game_module(R"(D:\Games\SotHR\WARFB\WING32_ORIG.DLL)", dir));
}

TEST_CASE("hooks are installed only into the game, when enabled", "[game_modules]") {
    CHECK(should_install_hooks(R"(D:\x\WHSHR.EXE)", true, ""));
    CHECK(should_install_hooks(R"(D:\x\whshr.exe)", true, "0"));
    CHECK_FALSE(should_install_hooks(R"(D:\build\bin\platform_tests.exe)", true, ""));
    CHECK_FALSE(should_install_hooks(R"(D:\x\WHSHR.EXE)", false, ""));
    CHECK_FALSE(should_install_hooks(R"(D:\x\WHSHR.EXE)", true, "1"));
}
```

`tests/core/CMakeLists.txt`:

```cmake
add_executable(core_tests ini_test.cpp config_test.cpp log_format_test.cpp sample_limiter_test.cpp
                          game_modules_test.cpp)
```

- [ ] **Step 2: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'core/game_modules.h'`.

- [ ] **Step 3: Write the minimal implementation**

`src/core/game_modules.h`:

```cpp
#pragma once

#include <string_view>

namespace hr::core {

// Windows path semantics ('\\' or '/', case-insensitive), implemented without Windows headers.
// A game module lives in `game_dir` or a subfolder and is not wing32.dll / wing32_orig.dll.
bool is_game_module(std::string_view module_path, std::string_view game_dir);

// Hooks are installed only into WHSHR.EXE, when enabled in the config and not disabled by the
// HORNED_RAT_DISABLE=1 environment variable (spec §3, §6.2).
bool should_install_hooks(std::string_view exe_path, bool config_enabled,
                          std::string_view disable_env);

} // namespace hr::core
```

`src/core/game_modules.cpp`:

```cpp
#include "core/game_modules.h"

#include "core/text.h"

#include <string>

namespace hr::core {
namespace {

std::string normalize(std::string_view path) {
    auto s = to_lower(path);
    for (auto& c : s) {
        if (c == '/') {
            c = '\\';
        }
    }
    while (!s.empty() && s.back() == '\\') {
        s.pop_back();
    }
    return s;
}

std::string_view file_name(std::string_view path) {
    const auto slash = path.find_last_of("\\/");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

} // namespace

bool is_game_module(std::string_view module_path, std::string_view game_dir) {
    const auto module = normalize(module_path);
    const auto prefix = normalize(game_dir) + '\\';
    if (module.size() <= prefix.size() || module.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    const auto name = file_name(module);
    return name != "wing32.dll" && name != "wing32_orig.dll";
}

bool should_install_hooks(std::string_view exe_path, bool config_enabled,
                          std::string_view disable_env) {
    return config_enabled && disable_env != "1" && to_lower(file_name(exe_path)) == "whshr.exe";
}

} // namespace hr::core
```

`src/core/CMakeLists.txt`:

```cmake
add_library(hr_core STATIC ini.cpp config.cpp log_format.cpp game_modules.cpp)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^game_modules$"`
Expected: `100% tests passed, 0 tests failed out of 4`.

- [ ] **Step 5: Format and commit**

Run Format, then:

```bash
git add src/core tests/core
git commit -m "core: decide which modules to hook and when" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 14: Module listing and DC descriptions (platform)

**Files:**
- Create: `src/platform/modules.h`, `src/platform/modules.cpp`, `src/platform/dc_info.h`, `src/platform/dc_info.cpp`
- Modify: `src/platform/CMakeLists.txt`, `tests/platform/CMakeLists.txt`
- Test: `tests/platform/modules_test.cpp`, `tests/platform/dc_info_test.cpp`

**Interfaces:**
- Produces:
  - `struct hr::platform::LoadedModule { HMODULE handle; std::string path; };`
  - `std::vector<LoadedModule> hr::platform::loaded_modules();`
  - `std::string hr::platform::module_path(HMODULE module);` — `nullptr` = the main executable.
  - `std::string hr::platform::module_file_name_from_address(const void* address);` — e.g. `"GAMEF.DLL"`, or `"?"`.
  - `std::string hr::platform::describe_dc(HDC dc);` — `"null"`, `"window 0x… class '…'"`, `"memory"`, or `"type N"`.

- [ ] **Step 1: Write the failing tests**

`tests/platform/modules_test.cpp`:

```cpp
#include "platform/modules.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cstring>

using Catch::Matchers::EndsWith;

namespace {
int some_function_in_this_exe() { return 42; }
} // namespace

TEST_CASE("loaded_modules includes the executable and kernel32", "[modules]") {
    const auto modules = hr::platform::loaded_modules();
    const auto has = [&](const char* suffix) {
        return std::ranges::any_of(modules, [&](const auto& m) {
            return m.path.size() >= std::strlen(suffix) &&
                   _stricmp(m.path.c_str() + m.path.size() - std::strlen(suffix), suffix) == 0;
        });
    };
    CHECK(has("platform_tests.exe"));
    CHECK(has("kernel32.dll"));
}

TEST_CASE("module_path(nullptr) is the main executable", "[modules]") {
    CHECK_THAT(hr::platform::module_path(nullptr), EndsWith("platform_tests.exe"));
}

TEST_CASE("module_file_name_from_address names the module containing an address", "[modules]") {
    CHECK(hr::platform::module_file_name_from_address(
              reinterpret_cast<const void*>(&some_function_in_this_exe)) == "platform_tests.exe");
    CHECK(hr::platform::module_file_name_from_address(nullptr) == "?");
}
```

`tests/platform/dc_info_test.cpp`:

```cpp
#include "platform/dc_info.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <windows.h>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

TEST_CASE("describe_dc tells window, memory and null DCs apart", "[dc_info]") {
    CHECK(hr::platform::describe_dc(nullptr) == "null");

    const HDC memory = CreateCompatibleDC(nullptr);
    REQUIRE(memory != nullptr);
    CHECK(hr::platform::describe_dc(memory) == "memory");
    DeleteDC(memory);

    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "HrDcInfoTestWindow";
    RegisterClassA(&wc);
    const HWND window = CreateWindowExA(0, "HrDcInfoTestWindow", "", WS_OVERLAPPEDWINDOW, 0, 0,
                                        100, 100, nullptr, nullptr, wc.hInstance, nullptr);
    REQUIRE(window != nullptr);
    const HDC dc = GetDC(window);
    const auto description = hr::platform::describe_dc(dc);
    CHECK_THAT(description, StartsWith("window "));
    CHECK_THAT(description, ContainsSubstring("class 'HrDcInfoTestWindow'"));
    ReleaseDC(window, dc);
    DestroyWindow(window);
}
```

Add `modules_test.cpp dc_info_test.cpp` to `add_executable(platform_tests ...)`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'platform/modules.h'`.

- [ ] **Step 3: Write the minimal implementation**

`src/platform/modules.h`:

```cpp
#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace hr::platform {

struct LoadedModule {
    HMODULE handle;
    std::string path; // ANSI, as returned by GetModuleFileNameA
};

std::vector<LoadedModule> loaded_modules();

// Full path of `module`; nullptr means the main executable.
std::string module_path(HMODULE module);

// File name of the module that contains `address`, e.g. "GAMEF.DLL"; "?" if none does.
std::string module_file_name_from_address(const void* address);

} // namespace hr::platform
```

`src/platform/modules.cpp`:

```cpp
#include "platform/modules.h"

#include <psapi.h>

namespace hr::platform {

std::vector<LoadedModule> loaded_modules() {
    std::vector<HMODULE> handles(256);
    DWORD needed = 0;
    while (true) {
        const auto bytes = static_cast<DWORD>(handles.size() * sizeof(HMODULE));
        if (!K32EnumProcessModules(GetCurrentProcess(), handles.data(), bytes, &needed)) {
            return {};
        }
        if (needed <= bytes) {
            break;
        }
        handles.resize(needed / sizeof(HMODULE));
    }
    handles.resize(needed / sizeof(HMODULE));

    std::vector<LoadedModule> modules;
    modules.reserve(handles.size());
    for (const HMODULE handle : handles) {
        modules.push_back({handle, module_path(handle)});
    }
    return modules;
}

std::string module_path(HMODULE module) {
    char buffer[MAX_PATH];
    const DWORD n = GetModuleFileNameA(module, buffer, MAX_PATH);
    return std::string(buffer, n);
}

std::string module_file_name_from_address(const void* address) {
    HMODULE module = nullptr;
    if (address == nullptr ||
        !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            static_cast<LPCSTR>(address), &module)) {
        return "?";
    }
    const auto path = module_path(module);
    const auto slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

} // namespace hr::platform
```

`src/platform/dc_info.h`:

```cpp
#pragma once

#include <windows.h>

#include <string>

namespace hr::platform {

// "null", "window 0x... class 'Name'", "memory", or "type N" (GetObjectType value).
std::string describe_dc(HDC dc);

} // namespace hr::platform
```

`src/platform/dc_info.cpp`:

```cpp
#include "platform/dc_info.h"

#include <cstdio>

namespace hr::platform {

std::string describe_dc(HDC dc) {
    if (dc == nullptr) {
        return "null";
    }
    if (const HWND window = WindowFromDC(dc)) {
        char class_name[128] = {};
        GetClassNameA(window, class_name, sizeof class_name);
        char text[200];
        std::snprintf(text, sizeof text, "window 0x%p class '%s'", static_cast<void*>(window),
                      class_name);
        return text;
    }
    const DWORD type = GetObjectType(dc);
    if (type == OBJ_MEMDC) {
        return "memory";
    }
    return "type " + std::to_string(type);
}

} // namespace hr::platform
```

`src/platform/CMakeLists.txt`:

```cmake
add_library(hr_platform STATIC pe.cpp wing_forward.cpp log.cpp iat_hook.cpp modules.cpp dc_info.cpp)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^(modules|dc_info)$"`
Expected: `100% tests passed, 0 tests failed out of 4`.

- [ ] **Step 5: Format, commit, push, check CI**

Run Format, then:

```bash
git add src/platform tests/platform
git commit -m "platform: list loaded modules; describe device contexts" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
# Wait for CI (see top of plan)
```

Expected: CI green (the `dc_info` test creates a hidden window; GitHub's Windows runners support this).

---

### Task 15: Instrumentation hooks in the proxy

**Files:**
- Modify: `src/wing32/runtime.h`, `src/wing32/runtime.cpp`, `src/wing32/exports.cpp`, `src/wing32/dllmain.cpp`, `src/wing32/CMakeLists.txt`, `tests/platform/CMakeLists.txt`, `.gitignore`
- Create: `src/wing32/hooks.h`, `src/wing32/hooks.cpp`
- Test: `tests/platform/proxy_init_test.cpp`

**Interfaces:**
- Consumes: everything from Tasks 2, 9–14.
- Produces:
  - `struct hr::wing32::Settings { core::Config config; bool hooks_enabled = false; std::string game_dir; };`
  - `void hr::wing32::initialize();` (called from `DllMain`), `const Settings& hr::wing32::settings();`
  - `void hr::wing32::install_hooks();` (idempotent; no-op unless `hooks_enabled`)
  - `void hr::wing32::log_wing_bit_blt(const void* caller, HDC dest, int x, int y, int w, int h);` and `log_wing_stretch_blt(...)` with the same parameters.
  - Files written next to the DLL: `horned-rat.ini` (if missing), `horned-rat.log` (if `debug.log`).

Most of this task is glue that only does something inside `WHSHR.EXE`, so automated tests cover what can be checked outside the game: initialisation writes the default config, opens the log, and **refuses to hook a non-game host**. The hooks themselves are exercised by Task 16 on the real game.

- [ ] **Step 1: Write the failing test**

`tests/platform/proxy_init_test.cpp`:

```cpp
#include "platform/wing_api.h"
#include "test_paths.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>

using Catch::Matchers::ContainsSubstring;

TEST_CASE("loading the proxy writes the default config and a log, and does not hook a "
          "non-game host",
          "[proxy_init]") {
    const auto bin = hr::test::bin_dir();
    std::filesystem::remove(bin / "horned-rat.ini");
    REQUIRE(LoadLibraryW((bin / "wing32.dll").c_str()) != nullptr);

    CHECK(std::filesystem::exists(bin / "horned-rat.ini"));
    const auto log = hr::test::read_file(bin / "horned-rat.log");
    CHECK_THAT(log, ContainsSubstring("horned-rat 0.1.0 loaded"));
    CHECK_THAT(log, ContainsSubstring("hooks disabled"));
    CHECK_THAT(log, ContainsSubstring("platform_tests.exe"));
}
```

Add `proxy_init_test.cpp` to `add_executable(platform_tests ...)`.

`catch_discover_tests` runs every test case in its own process, so this test always gets a fresh `DllMain`. The `std::filesystem::remove` before `LoadLibraryW` guarantees the test cannot pass on a file left over from an earlier run (the log is truncated by the DLL itself on every start).

- [ ] **Step 2: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^proxy_init$"`
Expected: FAIL — `horned-rat.ini` does not exist and the log file is empty.

- [ ] **Step 3: Write the implementation**

`src/wing32/runtime.h` (full replacement):

```cpp
#pragma once

#include "core/config.h"
#include "platform/wing_api.h"

#include <filesystem>
#include <string>

namespace hr::wing32 {

struct Settings {
    core::Config config;
    bool hooks_enabled = false;
    std::string game_dir; // directory of the host executable (ANSI)
};

// Directory containing our wing32.dll (the game's WARFB folder when installed).
std::filesystem::path module_dir();

// Called once from DllMain(PROCESS_ATTACH): reads/writes horned-rat.ini, opens horned-rat.log,
// and installs hooks if this is the game. Must not call LoadLibrary (loader lock).
void initialize();

const Settings& settings();

// The original WinG, loaded from wing32_orig.dll on first use; re-applies the hooks once at that
// point (spec §3). If it cannot be loaded the user sees one message box and the process exits.
const platform::wing::Functions& original();

} // namespace hr::wing32
```

`src/wing32/runtime.cpp` (full replacement):

```cpp
#include "wing32/runtime.h"

#include "core/game_modules.h"
#include "platform/log.h"
#include "platform/modules.h"
#include "platform/wing_forward.h"
#include "wing32/hooks.h"

#include <fstream>
#include <iterator>
#include <string>

namespace hr::wing32 {
namespace {

Settings g_settings;

HMODULE this_module() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&this_module), &module);
    return module;
}

[[noreturn]] void fatal(const std::string& message) {
    platform::log("fatal: " + message);
    MessageBoxA(nullptr, message.c_str(), "horned-rat", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

std::string environment_variable(const char* name) {
    char buffer[64];
    const DWORD n = GetEnvironmentVariableA(name, buffer, sizeof buffer);
    return n > 0 && n < sizeof buffer ? std::string(buffer, n) : std::string();
}

std::string read_or_create_config(const std::filesystem::path& path) {
    if (std::ifstream in{path, std::ios::binary}) {
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
    std::ofstream(path, std::ios::binary) << core::default_config_ini();
    return {};
}

} // namespace

std::filesystem::path module_dir() {
    wchar_t buffer[MAX_PATH];
    const DWORD n = GetModuleFileNameW(this_module(), buffer, MAX_PATH);
    return std::filesystem::path(std::wstring(buffer, n)).parent_path();
}

void initialize() {
    const auto dir = module_dir();
    const auto loaded = core::load_config(read_or_create_config(dir / "horned-rat.ini"));
    g_settings.config = loaded.config;

    if (loaded.config.log) {
        static platform::Log log_file(dir / "horned-rat.log");
        platform::set_global_log(&log_file);
    }
    platform::log("horned-rat " HR_VERSION " loaded");
    for (const auto& warning : loaded.warnings) {
        platform::log("config: " + warning);
    }

    const auto exe = platform::module_path(nullptr);
    const auto slash = exe.find_last_of("\\/");
    g_settings.game_dir = slash == std::string::npos ? std::string() : exe.substr(0, slash);
    const auto disable = environment_variable("HORNED_RAT_DISABLE");
    g_settings.hooks_enabled = core::should_install_hooks(exe, loaded.config.enabled, disable);
    if (!g_settings.hooks_enabled) {
        platform::log("hooks disabled (host '" + exe + "', enabled=" +
                      (loaded.config.enabled ? "true" : "false") + ", HORNED_RAT_DISABLE='" +
                      disable + "')");
        return;
    }
    install_hooks();
}

const Settings& settings() { return g_settings; }

const platform::wing::Functions& original() {
    static const platform::wing::Functions functions = [] {
        auto result = platform::load_original_wing(module_dir() / "wing32_orig.dll");
        if (!result.functions) {
            fatal(result.error +
                  "\n\nThe original WinG library (wing32_orig.dll) is missing or damaged. Use GOG "
                  "Galaxy \"Verify / Repair\" on the game, then run tools/install.ps1 again.");
        }
        platform::log("original WinG loaded");
        install_hooks(); // catch modules whose imports were not yet bound at DllMain time
        return *result.functions;
    }();
    return functions;
}

} // namespace hr::wing32
```

`src/wing32/dllmain.cpp` (full replacement):

```cpp
#include "platform/guard.h"
#include "wing32/runtime.h"

#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        hr::platform::guarded("initialize", [] { hr::wing32::initialize(); }, [] {});
    }
    return TRUE;
}
```

`src/wing32/hooks.h`:

```cpp
#pragma once

#include <windows.h>

namespace hr::wing32 {

// Installs or refreshes the A1 logging hooks in every game module (spec §3). Idempotent; no-op
// unless settings().hooks_enabled. Safe under the loader lock.
void install_hooks();

// Called by the WinG blit exports so their calls are logged like the GDI ones.
void log_wing_bit_blt(const void* caller, HDC dest, int x, int y, int width, int height);
void log_wing_stretch_blt(const void* caller, HDC dest, int x, int y, int width, int height);

} // namespace hr::wing32
```

`src/wing32/hooks.cpp`:

```cpp
// A1 instrumentation (spec §9): log-only hooks that answer which APIs the game uses to draw,
// create windows and read the mouse, and from which module. Every hook calls the original.
#include "wing32/hooks.h"

#include "core/game_modules.h"
#include "core/sample_limiter.h"
#include "platform/dc_info.h"
#include "platform/guard.h"
#include "platform/iat_hook.h"
#include "platform/log.h"
#include "platform/modules.h"
#include "wing32/runtime.h"

#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <type_traits>

#pragma intrinsic(_ReturnAddress)

namespace hr::wing32 {
namespace {

constexpr core::SampleLimiter kLimiter{};

// Calls that target a window DC are counted separately, so rare window draws are never drowned
// out by frequent memory-DC draws.
struct DcCounters {
    std::atomic<std::uint64_t> window{0};
    std::atomic<std::uint64_t> other{0};
};

struct Counters {
    std::atomic<std::uint64_t> set_window_pos{0}, get_cursor_pos{0}, set_cursor_pos{0},
        clip_cursor{0}, screen_to_client{0}, client_to_screen{0}, get_dc{0}, get_window_dc{0},
        begin_paint{0};
    DcCounters stretch_dibits, text_out, pat_blt, bit_blt, set_dibits_to_device, wing_bit_blt,
        wing_stretch_blt;
};

Counters g_counters;

// 1-based call number if this call should be logged, else 0.
std::uint64_t sample(std::atomic<std::uint64_t>& calls) {
    const auto n = ++calls;
    return kLimiter.should_log(n) ? n : 0;
}

std::uint64_t sample_dc(DcCounters& counters, bool targets_window) {
    return sample(targets_window ? counters.window : counters.other);
}

void trace(const char* function, std::uint64_t n, const void* caller, const std::string& details) {
    const DWORD error = GetLastError();
    platform::log(std::string(function) + " #" + std::to_string(n) + " from " +
                  platform::module_file_name_from_address(caller) + ": " + details);
    SetLastError(error);
}

std::string rect(int x, int y, int w, int h) {
    return "(" + std::to_string(x) + "," + std::to_string(y) + " " + std::to_string(w) + "x" +
           std::to_string(h) + ")";
}

std::string hex(const void* p) {
    char text[24];
    std::snprintf(text, sizeof text, "0x%p", p);
    return text;
}

std::string dc_text(HDC dc) { return platform::describe_dc(dc); }

struct Originals {
    decltype(&::OutputDebugStringA) OutputDebugStringA;
    decltype(&::LoadLibraryA) LoadLibraryA;
    decltype(&::GetProcAddress) GetProcAddress;
    decltype(&::StretchDIBits) StretchDIBits;
    decltype(&::TextOutA) TextOutA;
    decltype(&::PatBlt) PatBlt;
    decltype(&::BitBlt) BitBlt;
    decltype(&::SetDIBitsToDevice) SetDIBitsToDevice;
    decltype(&::CreateWindowExA) CreateWindowExA;
    decltype(&::SetWindowPos) SetWindowPos;
    decltype(&::GetCursorPos) GetCursorPos;
    decltype(&::SetCursorPos) SetCursorPos;
    decltype(&::ClipCursor) ClipCursor;
    decltype(&::ScreenToClient) ScreenToClient;
    decltype(&::ClientToScreen) ClientToScreen;
    decltype(&::GetDC) GetDC;
    decltype(&::GetWindowDC) GetWindowDC;
    decltype(&::BeginPaint) BeginPaint;
};

const Originals& originals() {
    static const Originals o = [] {
        const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        const HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll");
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        Originals r{};
        const auto get = [](HMODULE module, const char* name, auto& target) {
            target = reinterpret_cast<std::remove_reference_t<decltype(target)>>(
                ::GetProcAddress(module, name));
        };
        get(kernel32, "OutputDebugStringA", r.OutputDebugStringA);
        get(kernel32, "LoadLibraryA", r.LoadLibraryA);
        get(kernel32, "GetProcAddress", r.GetProcAddress);
        get(gdi32, "StretchDIBits", r.StretchDIBits);
        get(gdi32, "TextOutA", r.TextOutA);
        get(gdi32, "PatBlt", r.PatBlt);
        get(gdi32, "BitBlt", r.BitBlt);
        get(gdi32, "SetDIBitsToDevice", r.SetDIBitsToDevice);
        get(user32, "CreateWindowExA", r.CreateWindowExA);
        get(user32, "SetWindowPos", r.SetWindowPos);
        get(user32, "GetCursorPos", r.GetCursorPos);
        get(user32, "SetCursorPos", r.SetCursorPos);
        get(user32, "ClipCursor", r.ClipCursor);
        get(user32, "ScreenToClient", r.ScreenToClient);
        get(user32, "ClientToScreen", r.ClientToScreen);
        get(user32, "GetDC", r.GetDC);
        get(user32, "GetWindowDC", r.GetWindowDC);
        get(user32, "BeginPaint", r.BeginPaint);
        return r;
    }();
    return o;
}

// ---- KERNEL32 ----

void WINAPI hook_OutputDebugStringA(LPCSTR text) {
    const auto call = [&] { originals().OutputDebugStringA(text); };
    platform::guarded(
        "OutputDebugStringA",
        [&] {
            if (settings().config.game_debug && text != nullptr) {
                platform::log(std::string("game: ") + text);
            }
            call();
        },
        call);
}

HMODULE WINAPI hook_LoadLibraryA(LPCSTR name) {
    const void* caller = _ReturnAddress();
    const HMODULE result = originals().LoadLibraryA(name);
    const DWORD error = GetLastError();
    platform::guarded(
        "LoadLibraryA",
        [&] {
            platform::log(std::string("LoadLibraryA(\"") + (name ? name : "(null)") + "\") from " +
                          platform::module_file_name_from_address(caller) + " -> " + hex(result));
            if (result != nullptr) {
                install_hooks();
            }
        },
        [] {});
    SetLastError(error);
    return result;
}

FARPROC WINAPI hook_GetProcAddress(HMODULE module, LPCSTR name) {
    const void* caller = _ReturnAddress();
    const FARPROC result = originals().GetProcAddress(module, name);
    const DWORD error = GetLastError();
    platform::guarded(
        "GetProcAddress",
        [&] {
            const std::string what =
                IS_INTRESOURCE(name) ? "#" + std::to_string(reinterpret_cast<std::uintptr_t>(name))
                                     : std::string(name);
            platform::log("GetProcAddress(" + platform::module_file_name_from_address(module) +
                          "!" + what + ") from " + platform::module_file_name_from_address(caller) +
                          (result ? "" : " -> not found"));
        },
        [] {});
    SetLastError(error);
    return result;
}

// ---- GDI32 ----

int WINAPI hook_StretchDIBits(HDC dc, int xd, int yd, int wd, int hd, int xs, int ys, int ws,
                              int hs, const VOID* bits, const BITMAPINFO* info, UINT usage,
                              DWORD rop) {
    const void* caller = _ReturnAddress();
    const auto call = [&] {
        return originals().StretchDIBits(dc, xd, yd, wd, hd, xs, ys, ws, hs, bits, info, usage,
                                         rop);
    };
    return platform::guarded(
        "StretchDIBits",
        [&] {
            if (const auto n = sample_dc(g_counters.stretch_dibits, WindowFromDC(dc) != nullptr)) {
                trace("StretchDIBits", n, caller,
                      "dc=" + dc_text(dc) + " dst=" + rect(xd, yd, wd, hd) +
                          " src=" + rect(xs, ys, ws, hs) + " bpp=" +
                          std::to_string(info ? info->bmiHeader.biBitCount : 0));
            }
            return call();
        },
        call);
}

BOOL WINAPI hook_TextOutA(HDC dc, int x, int y, LPCSTR text, int length) {
    const void* caller = _ReturnAddress();
    const auto call = [&] { return originals().TextOutA(dc, x, y, text, length); };
    return platform::guarded(
        "TextOutA",
        [&] {
            if (const auto n = sample_dc(g_counters.text_out, WindowFromDC(dc) != nullptr)) {
                trace("TextOutA", n, caller,
                      "dc=" + dc_text(dc) + " at (" + std::to_string(x) + "," + std::to_string(y) +
                          ") len=" + std::to_string(length));
            }
            return call();
        },
        call);
}

BOOL WINAPI hook_PatBlt(HDC dc, int x, int y, int w, int h, DWORD rop) {
    const void* caller = _ReturnAddress();
    const auto call = [&] { return originals().PatBlt(dc, x, y, w, h, rop); };
    return platform::guarded(
        "PatBlt",
        [&] {
            if (const auto n = sample_dc(g_counters.pat_blt, WindowFromDC(dc) != nullptr)) {
                trace("PatBlt", n, caller, "dc=" + dc_text(dc) + " " + rect(x, y, w, h));
            }
            return call();
        },
        call);
}

BOOL WINAPI hook_BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop) {
    const void* caller = _ReturnAddress();
    const auto call = [&] { return originals().BitBlt(dst, x, y, w, h, src, sx, sy, rop); };
    return platform::guarded(
        "BitBlt",
        [&] {
            const bool window = WindowFromDC(dst) != nullptr || WindowFromDC(src) != nullptr;
            if (const auto n = sample_dc(g_counters.bit_blt, window)) {
                trace("BitBlt", n, caller,
                      "dst=" + dc_text(dst) + " " + rect(x, y, w, h) + " src=" + dc_text(src) +
                          " at (" + std::to_string(sx) + "," + std::to_string(sy) + ")");
            }
            return call();
        },
        call);
}

int WINAPI hook_SetDIBitsToDevice(HDC dc, int xd, int yd, DWORD w, DWORD h, int xs, int ys,
                                  UINT start_scan, UINT lines, const VOID* bits,
                                  const BITMAPINFO* info, UINT usage) {
    const void* caller = _ReturnAddress();
    const auto call = [&] {
        return originals().SetDIBitsToDevice(dc, xd, yd, w, h, xs, ys, start_scan, lines, bits,
                                             info, usage);
    };
    return platform::guarded(
        "SetDIBitsToDevice",
        [&] {
            if (const auto n =
                    sample_dc(g_counters.set_dibits_to_device, WindowFromDC(dc) != nullptr)) {
                trace("SetDIBitsToDevice", n, caller,
                      "dc=" + dc_text(dc) + " dst=" +
                          rect(xd, yd, static_cast<int>(w), static_cast<int>(h)) + " bpp=" +
                          std::to_string(info ? info->bmiHeader.biBitCount : 0));
            }
            return call();
        },
        call);
}

// ---- USER32 ----

HWND WINAPI hook_CreateWindowExA(DWORD ex_style, LPCSTR class_name, LPCSTR window_name,
                                 DWORD style, int x, int y, int w, int h, HWND parent, HMENU menu,
                                 HINSTANCE instance, LPVOID param) {
    const void* caller = _ReturnAddress();
    const HWND result = originals().CreateWindowExA(ex_style, class_name, window_name, style, x, y,
                                                    w, h, parent, menu, instance, param);
    const DWORD error = GetLastError();
    platform::guarded(
        "CreateWindowExA",
        [&] {
            char styles[64];
            std::snprintf(styles, sizeof styles, "style=0x%08lx ex=0x%08lx",
                          static_cast<unsigned long>(style), static_cast<unsigned long>(ex_style));
            const std::string cls =
                IS_INTRESOURCE(class_name)
                    ? "#" + std::to_string(reinterpret_cast<std::uintptr_t>(class_name))
                    : std::string(class_name);
            platform::log("CreateWindowExA from " + platform::module_file_name_from_address(caller) +
                          ": class '" + cls + "' title '" + (window_name ? window_name : "") +
                          "' " + styles + " " + rect(x, y, w, h) + " parent=" + hex(parent) +
                          " -> " + hex(result));
        },
        [] {});
    SetLastError(error);
    return result;
}

BOOL WINAPI hook_SetWindowPos(HWND window, HWND after, int x, int y, int w, int h, UINT flags) {
    const void* caller = _ReturnAddress();
    const auto call = [&] { return originals().SetWindowPos(window, after, x, y, w, h, flags); };
    return platform::guarded(
        "SetWindowPos",
        [&] {
            if (const auto n = sample(g_counters.set_window_pos)) {
                trace("SetWindowPos", n, caller,
                      "hwnd=" + hex(window) + " " + rect(x, y, w, h) +
                          " flags=" + std::to_string(flags));
            }
            return call();
        },
        call);
}

BOOL WINAPI hook_GetCursorPos(LPPOINT point) {
    const void* caller = _ReturnAddress();
    const BOOL result = originals().GetCursorPos(point);
    platform::guarded(
        "GetCursorPos",
        [&] {
            if (const auto n = sample(g_counters.get_cursor_pos); n && point) {
                trace("GetCursorPos", n, caller,
                      "-> (" + std::to_string(point->x) + "," + std::to_string(point->y) + ")");
            }
        },
        [] {});
    return result;
}

BOOL WINAPI hook_SetCursorPos(int x, int y) {
    const void* caller = _ReturnAddress();
    const auto call = [&] { return originals().SetCursorPos(x, y); };
    return platform::guarded(
        "SetCursorPos",
        [&] {
            if (const auto n = sample(g_counters.set_cursor_pos)) {
                trace("SetCursorPos", n, caller,
                      "(" + std::to_string(x) + "," + std::to_string(y) + ")");
            }
            return call();
        },
        call);
}

BOOL WINAPI hook_ClipCursor(const RECT* r) {
    const void* caller = _ReturnAddress();
    const auto call = [&] { return originals().ClipCursor(r); };
    return platform::guarded(
        "ClipCursor",
        [&] {
            if (const auto n = sample(g_counters.clip_cursor)) {
                trace("ClipCursor", n, caller,
                      r ? rect(r->left, r->top, r->right - r->left, r->bottom - r->top)
                        : std::string("(null)"));
            }
            return call();
        },
        call);
}

BOOL WINAPI hook_ScreenToClient(HWND window, LPPOINT point) {
    const void* caller = _ReturnAddress();
    const POINT before = point ? *point : POINT{};
    const BOOL result = originals().ScreenToClient(window, point);
    platform::guarded(
        "ScreenToClient",
        [&] {
            if (const auto n = sample(g_counters.screen_to_client); n && point) {
                trace("ScreenToClient", n, caller,
                      "hwnd=" + hex(window) + " (" + std::to_string(before.x) + "," +
                          std::to_string(before.y) + ") -> (" + std::to_string(point->x) + "," +
                          std::to_string(point->y) + ")");
            }
        },
        [] {});
    return result;
}

BOOL WINAPI hook_ClientToScreen(HWND window, LPPOINT point) {
    const void* caller = _ReturnAddress();
    const POINT before = point ? *point : POINT{};
    const BOOL result = originals().ClientToScreen(window, point);
    platform::guarded(
        "ClientToScreen",
        [&] {
            if (const auto n = sample(g_counters.client_to_screen); n && point) {
                trace("ClientToScreen", n, caller,
                      "hwnd=" + hex(window) + " (" + std::to_string(before.x) + "," +
                          std::to_string(before.y) + ") -> (" + std::to_string(point->x) + "," +
                          std::to_string(point->y) + ")");
            }
        },
        [] {});
    return result;
}

HDC WINAPI hook_GetDC(HWND window) {
    const void* caller = _ReturnAddress();
    const HDC result = originals().GetDC(window);
    platform::guarded(
        "GetDC",
        [&] {
            if (const auto n = sample(g_counters.get_dc)) {
                trace("GetDC", n, caller, "hwnd=" + hex(window) + " -> " + hex(result));
            }
        },
        [] {});
    return result;
}

HDC WINAPI hook_GetWindowDC(HWND window) {
    const void* caller = _ReturnAddress();
    const HDC result = originals().GetWindowDC(window);
    platform::guarded(
        "GetWindowDC",
        [&] {
            if (const auto n = sample(g_counters.get_window_dc)) {
                trace("GetWindowDC", n, caller, "hwnd=" + hex(window) + " -> " + hex(result));
            }
        },
        [] {});
    return result;
}

HDC WINAPI hook_BeginPaint(HWND window, LPPAINTSTRUCT paint) {
    const void* caller = _ReturnAddress();
    const HDC result = originals().BeginPaint(window, paint);
    platform::guarded(
        "BeginPaint",
        [&] {
            if (const auto n = sample(g_counters.begin_paint); n && paint) {
                const RECT& r = paint->rcPaint;
                trace("BeginPaint", n, caller,
                      "hwnd=" + hex(window) + " " +
                          rect(r.left, r.top, r.right - r.left, r.bottom - r.top));
            }
        },
        [] {});
    return result;
}

struct HookSpec {
    const char* dll;
    const char* function;
    void* replacement;
};

const HookSpec kHooks[] = {
    {"KERNEL32.dll", "OutputDebugStringA", reinterpret_cast<void*>(&hook_OutputDebugStringA)},
    {"KERNEL32.dll", "LoadLibraryA", reinterpret_cast<void*>(&hook_LoadLibraryA)},
    {"KERNEL32.dll", "GetProcAddress", reinterpret_cast<void*>(&hook_GetProcAddress)},
    {"GDI32.dll", "StretchDIBits", reinterpret_cast<void*>(&hook_StretchDIBits)},
    {"GDI32.dll", "TextOutA", reinterpret_cast<void*>(&hook_TextOutA)},
    {"GDI32.dll", "PatBlt", reinterpret_cast<void*>(&hook_PatBlt)},
    {"GDI32.dll", "BitBlt", reinterpret_cast<void*>(&hook_BitBlt)},
    {"GDI32.dll", "SetDIBitsToDevice", reinterpret_cast<void*>(&hook_SetDIBitsToDevice)},
    {"USER32.dll", "CreateWindowExA", reinterpret_cast<void*>(&hook_CreateWindowExA)},
    {"USER32.dll", "SetWindowPos", reinterpret_cast<void*>(&hook_SetWindowPos)},
    {"USER32.dll", "GetCursorPos", reinterpret_cast<void*>(&hook_GetCursorPos)},
    {"USER32.dll", "SetCursorPos", reinterpret_cast<void*>(&hook_SetCursorPos)},
    {"USER32.dll", "ClipCursor", reinterpret_cast<void*>(&hook_ClipCursor)},
    {"USER32.dll", "ScreenToClient", reinterpret_cast<void*>(&hook_ScreenToClient)},
    {"USER32.dll", "ClientToScreen", reinterpret_cast<void*>(&hook_ClientToScreen)},
    {"USER32.dll", "GetDC", reinterpret_cast<void*>(&hook_GetDC)},
    {"USER32.dll", "GetWindowDC", reinterpret_cast<void*>(&hook_GetWindowDC)},
    {"USER32.dll", "BeginPaint", reinterpret_cast<void*>(&hook_BeginPaint)},
};

void log_wing_blit(const char* function, DcCounters& counters, const void* caller, HDC dest, int x,
                   int y, int width, int height) {
    platform::guarded(
        function,
        [&] {
            if (const auto n = sample_dc(counters, WindowFromDC(dest) != nullptr)) {
                trace(function, n, caller, "dst=" + dc_text(dest) + " " + rect(x, y, width, height));
            }
        },
        [] {});
}

} // namespace

void install_hooks() {
    if (!settings().hooks_enabled) {
        return;
    }
    static std::mutex mutex;
    const std::lock_guard lock(mutex);
    originals(); // resolve before any slot points at a hook
    for (const auto& module : platform::loaded_modules()) {
        if (!core::is_game_module(module.path, settings().game_dir)) {
            continue;
        }
        for (const auto& hook : kHooks) {
            const auto result =
                platform::hook_import(module.handle, hook.dll, hook.function, hook.replacement);
            if (result == platform::HookResult::Patched || result == platform::HookResult::Failed) {
                platform::log("hook " + module.path + " " + hook.dll + "!" + hook.function + ": " +
                              platform::to_string(result));
            }
        }
    }
}

void log_wing_bit_blt(const void* caller, HDC dest, int x, int y, int width, int height) {
    log_wing_blit("WinGBitBlt", g_counters.wing_bit_blt, caller, dest, x, y, width, height);
}

void log_wing_stretch_blt(const void* caller, HDC dest, int x, int y, int width, int height) {
    log_wing_blit("WinGStretchBlt", g_counters.wing_stretch_blt, caller, dest, x, y, width,
                  height);
}

} // namespace hr::wing32
```

In `src/wing32/exports.cpp`, add `#include "wing32/hooks.h"` and `#include <intrin.h>` plus `#pragma intrinsic(_ReturnAddress)` below the existing include, and replace the two blit functions with:

```cpp
BOOL WINAPI WinGBitBlt(HDC dest, int x, int y, int width, int height, HDC src, int src_x,
                       int src_y) {
    hr::wing32::log_wing_bit_blt(_ReturnAddress(), dest, x, y, width, height);
    return original().bit_blt(dest, x, y, width, height, src, src_x, src_y);
}

BOOL WINAPI WinGStretchBlt(HDC dest, int x, int y, int width, int height, HDC src, int src_x,
                           int src_y, int src_width, int src_height) {
    hr::wing32::log_wing_stretch_blt(_ReturnAddress(), dest, x, y, width, height);
    return original().stretch_blt(dest, x, y, width, height, src, src_x, src_y, src_width,
                                  src_height);
}
```

`src/wing32/CMakeLists.txt`:

```cmake
add_library(wing32 SHARED dllmain.cpp exports.cpp runtime.cpp hooks.cpp wing32.def)
```

Append to `.gitignore`:

```
# Game logs and local config written by the proxy
*.log
horned-rat.ini
```

- [ ] **Step 4: Run all tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug`
Expected: `100% tests passed` (gamedata tests skipped unless `SOTHR_GAME_DIR` is set). In particular `proxy` (forwarding still works) and `proxy_init` pass.

- [ ] **Step 5: Format, commit, push, check CI**

Run Format, then:

```bash
git add src/wing32 tests/platform .gitignore
git commit -m "wing32: config, log and A1 instrumentation hooks" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
# Wait for CI (see top of plan)
```

Expected: CI green.

---

### Task 16: A1 exit — instrumented play session and findings

**Files:**
- Create: `docs/re-notes/a1-findings.md`
- Modify: `docs/superpowers/specs/2026-10-05-stage-a-display-wrapper-design.md` (§2, §5, §10 as the findings require), `docs/smoke-test.md` (log item)

**Interfaces:**
- Consumes: the installed release build of Task 15.
- Produces: written answers to the five questions below, which the A2 plan is based on.

This task is manual and needs the user: they play, we read the log together. **Ask before installing into the game folder.** The game log is never committed.

- [ ] **Step 1: Build and install the instrumented proxy**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-release; cmake --build --preset x86-release; ./tools/install.ps1 -GameDir 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat' -SourceDll build/x86-release/bin/wing32.dll`
Expected: `Installed horned-rat into '...\WARFB'.`

- [ ] **Step 2: The user plays a fixed scenario**

Launch from GOG Galaxy and: watch the intro (or skip it) → main menu → new campaign → campaign map (scroll, open a unit roster) → first battle (deploy, move, attack, wait for a spell or missile effect if possible) → save → quit to menu → load → quit to desktop. Then run the full `docs/smoke-test.md` checklist. Expected: the game behaves exactly as vanilla.

- [ ] **Step 3: Summarise the log**

Run:

```powershell
$log = 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat\WARFB\horned-rat.log'
Select-String -Path $log -Pattern '^\[ *[\d.]+\] (hook |LoadLibraryA|CreateWindowExA|GetProcAddress\(GDI32|GetProcAddress\(USER32|exception|fatal)' | ForEach-Object Line
Select-String -Path $log -Pattern '^\[ *[\d.]+\] (\w+) #\d+ from (\S+): .*?(dc=|dst=)window' | ForEach-Object { $_.Matches[0].Groups[1].Value + ' from ' + $_.Matches[0].Groups[2].Value } | Group-Object | Sort-Object Count -Descending | Format-Table Count, Name
Select-String -Path $log -Pattern '^\[ *[\d.]+\] (GetCursorPos|SetCursorPos|ClipCursor|ScreenToClient|ClientToScreen) #\d+ from (\S+)' | ForEach-Object { $_.Matches[0].Groups[1].Value + ' from ' + $_.Matches[0].Groups[2].Value } | Group-Object | Format-Table Count, Name
```

Expected: hook lines for `WHSHR.EXE` and `GAMEF.DLL` (and any dynamically loaded modules), no `exception`/`fatal` lines, and the window-DC and mouse tables.

- [ ] **Step 4: Write the findings (own words, no game strings)**

`docs/re-notes/a1-findings.md` answers, with counts and module names from Step 3:

1. **Window-DC drawing:** which functions draw to (or read from) a window DC, from which module, and how often? Is everything except `WinGBitBlt`/`WinGStretchBlt` memory-DC only? (Spec §2 known unknown.)
2. **Main window:** is the first top-level `CreateWindowExA` the game's main window (spec §5.3)? Its class, style and size.
3. **Mouse:** which cursor APIs are used, by which modules, and in which coordinate space.
4. **Reality Lab / video presentation:** do `SOFTF.DLL` or `OMNI000R.DLL` present directly (`BitBlt`/`SetDIBitsToDevice` to a window DC)?
5. **Dynamic loading:** which modules are loaded with `LoadLibraryA`, and are any GDI/USER32 functions resolved via `GetProcAddress` (which would bypass IAT hooks)?

End the document with "Consequences for A2": each finding that changes the design, and the matching spec edit.

- [ ] **Step 5: Update the spec and smoke test**

Apply the "Consequences for A2" edits to the spec (§2 findings, §5 data flow, §10 risks). In `docs/smoke-test.md`, the `horned-rat.log` item now applies.

- [ ] **Step 6: Commit and push**

```bash
git add docs
git commit -m "docs: A1 findings; spec updated for A2" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

**Milestone A1 is complete.** Next: write the A2 plan (capture at 1:1) from the updated spec with superpowers:writing-plans.
