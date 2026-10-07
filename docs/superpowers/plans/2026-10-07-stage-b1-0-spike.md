# Stage B1.0 Spike Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Prove (or refute) that a second Reality Lab device, created by us inside the game, renders the game's battle scene identically at 1× and without errors at 2× / 3×, and measure its cost.

**Architecture:** A diagnostic `[debug] b1_spike = 0..3` key turns the spike on. When the game blits its 3D view (`WinGBitBlt` / `WinGStretchBlt` into the `Map3DClass` window), `wing32/rl_shadow` renders the game's scene through the game's camera with our own device and viewport (created by calling `GAMEF.DLL`'s statically linked Reality Lab functions by address, only when the module's SHA-256 matches the GOG build), compares the 1× result with the game's frame, logs timing, and writes one BMP of a k× frame for inspection. Nothing is shown in the presenter yet.

**Deviation from spec §4:** the address table is `core::gamef_layout` (pure data, testable on Linux CI) instead of `wing32/gamef_layout`; hashing the module is `platform::sha256_file`.

**Tech Stack:** C++20, MSVC x86, CMake presets, Catch2, Win32 (BCrypt for SHA-256), the game's Reality Lab (statically linked in `GAMEF.DLL`).

**Spec:** `docs/superpowers/specs/2026-10-07-stage-b-battle-view-design.md` (§2, §5.1, §7, §9 B1.0).

## Global Constraints

- Layering: `wing32 → platform → core`; `core` has no Windows headers and builds on Linux CI.
- Red/green TDD for every behaviour that can be tested outside the game.
- Never commit game binaries, logs, captures, BMP dumps or decompiled code. Addresses and offsets in `core::gamef_layout` are facts about the build, not decompiled code.
- Stage B is active only for `GAMEF.DLL` SHA-256 `e8afe836e9c99b54a76643b2adb4edb52f56a991d53ab8af60f746129b1132d6`.
- Every Reality Lab call from our code runs on the game thread and inside SEH; on an exception the spike turns itself off for the session and the game's own blit still runs.
- Build/test: `. ./tools/vsenv.ps1; cmake --build --preset x86-debug; ctest --preset x86-debug` (release: `x86-release`). Install: `./tools/install.ps1 -GameDir 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat' -SourceDll build/x86-release/bin/wing32.dll` (pre-approved). Changing `horned-rat.ini` needs the user's OK each time; restore it afterwards.
- Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; check the PR is open before pushing; Auto-fix on for the PR.

## Facts used (from `GAMEF.DLL`, GOG build, RVAs from image base 0x10000000)

All functions are `__cdecl`; handles are 32-bit integers.

| Name | RVA | Signature (as used) |
|---|---|---|
| create_device | 0x57040 | `u32 (void* desc, int 0, int 0, int 0, palette_cb, void* cb_arg)` → device handle, 0 on failure |
| set_device_mode | 0x56F00 | `u32 (u32 device, int 0)` |
| set_device_dither | 0x56EB0 | `u32 (u32 device, int 0)` |
| set_device_shades | 0x56E40 | `u32 (u32 device, int 8)` |
| create_viewport | 0x57BF0 | `u32 (u32 device, u32 camera_frame, int x, int y, int w, int h)` → viewport, 0 on failure |
| configure_viewport | 0x56800 | `int (u32 vp, int x, int y, int w, int h)` → 0 ok |
| set_viewport_plane | 0x56790 | `int (u32 vp, float a, float b, float c, float d)`; stores a→+0xF8, b→+0xFC, c→+0x104, d→+0x100 |
| set_viewport_back | 0x56640 | `int (u32 vp, float back)`; stores +0xC4; fails if back ≤ front (+0xF4, default 1.0) |
| set_viewport_flag_d8 | 0x57BB0 | `int (u32 vp, int)`; stores +0xD8 |
| set_viewport_flag_44 | 0x57B70 | `int (u32 vp, int)`; stores +0x44 |
| clear_viewport | 0x58650 | `int (u32 vp)` |
| render_viewport | 0x583C0 | `int (u32 vp, u32 scene_frame)` |
| destroy_object | 0x56520 | `int (u32 handle)` (reference counted) |
| lookup_object | 0x56A20 | `int (int* out_object, u32 handle, const void* type)` → 0 ok |
| viewport type tag | 0xFB0E8 | address passed as `type` to look up a viewport |

Palette callback (the game's is at RVA 0x45DA0): `void (void* unused, void* cb_arg, int index, int r, int g, int b)`, colour components in the low byte.

Battle window data block `W = GetWindowLongA(GetParent(map3d_hwnd), 4)`:

| Offset in W | Meaning |
|---|---|
| 0x32860 | scene frame handle |
| 0x32868 | camera frame handle |
| 0x32870 | device handle |
| 0x32874 | viewport handle |
| 0x32878 | device description, 15 dwords: [0] width (4-aligned), [1] height, [6] stride, [7] bits pointer, others copied as is |
| 0x328FC | the game's 8-bit frame bits pointer (top-down, stride = description [0]) |
| 0x32968 / 0x3296C | active 3D view width / height (417×296 or 624×296) |
| 0x329A0 | detail divisor (1 = full, 2 = half) |

Teardown order in the game's `WM_DESTROY` of `Map3DClass`: viewport, frames, device. Our objects must be destroyed before the game's handler runs.

---

### Task 1: `core::gamef_layout` — the hash-gated address table

**Files:**
- Create: `src/core/gamef_layout.h`, `src/core/gamef_layout.cpp`
- Modify: `src/core/CMakeLists.txt` (add `gamef_layout.cpp`)
- Test: `tests/core/gamef_layout_test.cpp`; modify `tests/core/CMakeLists.txt` (add the file)

**Interfaces:**
- Produces: `struct hr::core::GamefLayout` with the `std::uint32_t` members below; `std::optional<GamefLayout> hr::core::gamef_layout(std::string_view sha256_hex)` (hex case-insensitive).

- [ ] **Step 1: Write the failing test**

`tests/core/gamef_layout_test.cpp`:
```cpp
#include "core/gamef_layout.h"

#include <catch2/catch_test_macros.hpp>

using hr::core::gamef_layout;

namespace {
constexpr const char* kGog = "e8afe836e9c99b54a76643b2adb4edb52f56a991d53ab8af60f746129b1132d6";
}

TEST_CASE("the GOG build of GAMEF.DLL has a layout", "[gamef_layout]") {
    const auto layout = gamef_layout(kGog);
    REQUIRE(layout);
    CHECK(layout->create_device == 0x57040);
    CHECK(layout->create_viewport == 0x57BF0);
    CHECK(layout->render_viewport == 0x583C0);
    CHECK(layout->viewport_type == 0xFB0E8);
    CHECK(layout->w_viewport == 0x32874);
    CHECK(layout->w_device_desc == 0x32878);
    CHECK(layout->vp_back == 0xC4);
}

TEST_CASE("the hash is matched case-insensitively", "[gamef_layout]") {
    CHECK(gamef_layout("E8AFE836E9C99B54A76643B2ADB4EDB52F56A991D53AB8AF60F746129B1132D6"));
}

TEST_CASE("any other build has no layout", "[gamef_layout]") {
    CHECK_FALSE(gamef_layout("0000000000000000000000000000000000000000000000000000000000000000"));
    CHECK_FALSE(gamef_layout(""));
}
```
Add `gamef_layout_test.cpp` to the `core_tests` sources in `tests/core/CMakeLists.txt`.

- [ ] **Step 2: Run test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --build --preset x86-debug`
Expected: compile error, `core/gamef_layout.h` not found.

- [ ] **Step 3: Write minimal implementation**

`src/core/gamef_layout.h`:
```cpp
#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace hr::core {

// Where Stage B finds things in GAMEF.DLL (spec 2026-10-07 §4, §7). Function and data addresses
// are RVAs; `w_*` are offsets in the battle window's data block (GetWindowLongA(battle, 4)),
// `vp_*` offsets in a Reality Lab viewport object. Only the GOG build is known.
struct GamefLayout {
    // Reality Lab functions (__cdecl).
    std::uint32_t create_device;
    std::uint32_t set_device_mode;
    std::uint32_t set_device_dither;
    std::uint32_t set_device_shades;
    std::uint32_t create_viewport;
    std::uint32_t configure_viewport;
    std::uint32_t set_viewport_plane;
    std::uint32_t set_viewport_back;
    std::uint32_t set_viewport_flag_d8;
    std::uint32_t set_viewport_flag_44;
    std::uint32_t clear_viewport;
    std::uint32_t render_viewport;
    std::uint32_t destroy_object;
    std::uint32_t lookup_object;
    std::uint32_t viewport_type; // type tag address passed to lookup_object
    // Battle window data block.
    std::uint32_t w_scene;
    std::uint32_t w_camera;
    std::uint32_t w_device;
    std::uint32_t w_viewport;
    std::uint32_t w_device_desc; // 15 dwords
    std::uint32_t w_bits;
    std::uint32_t w_active_width;
    std::uint32_t w_active_height;
    std::uint32_t w_divisor;
    // Viewport object.
    std::uint32_t vp_back;
    std::uint32_t vp_front;
    std::uint32_t vp_flag_d8;
    std::uint32_t vp_flag_44;
    std::uint32_t vp_plane_a; // set_viewport_plane argument order: a, b, c, d
    std::uint32_t vp_plane_b;
    std::uint32_t vp_plane_c;
    std::uint32_t vp_plane_d;
};

// The layout for the GAMEF.DLL whose SHA-256 is `sha256_hex` (any case), or nullopt.
std::optional<GamefLayout> gamef_layout(std::string_view sha256_hex);

} // namespace hr::core
```

`src/core/gamef_layout.cpp`:
```cpp
#include "core/gamef_layout.h"

#include "core/text.h"

namespace hr::core {
namespace {

constexpr std::string_view kGogSha256 =
    "e8afe836e9c99b54a76643b2adb4edb52f56a991d53ab8af60f746129b1132d6";

constexpr GamefLayout kGog{
    .create_device = 0x57040,
    .set_device_mode = 0x56F00,
    .set_device_dither = 0x56EB0,
    .set_device_shades = 0x56E40,
    .create_viewport = 0x57BF0,
    .configure_viewport = 0x56800,
    .set_viewport_plane = 0x56790,
    .set_viewport_back = 0x56640,
    .set_viewport_flag_d8 = 0x57BB0,
    .set_viewport_flag_44 = 0x57B70,
    .clear_viewport = 0x58650,
    .render_viewport = 0x583C0,
    .destroy_object = 0x56520,
    .lookup_object = 0x56A20,
    .viewport_type = 0xFB0E8,
    .w_scene = 0x32860,
    .w_camera = 0x32868,
    .w_device = 0x32870,
    .w_viewport = 0x32874,
    .w_device_desc = 0x32878,
    .w_bits = 0x328FC,
    .w_active_width = 0x32968,
    .w_active_height = 0x3296C,
    .w_divisor = 0x329A0,
    .vp_back = 0xC4,
    .vp_front = 0xF4,
    .vp_flag_d8 = 0xD8,
    .vp_flag_44 = 0x44,
    .vp_plane_a = 0xF8,
    .vp_plane_b = 0xFC,
    .vp_plane_c = 0x104,
    .vp_plane_d = 0x100,
};

} // namespace

std::optional<GamefLayout> gamef_layout(std::string_view sha256_hex) {
    if (to_lower(sha256_hex) == kGogSha256) {
        return kGog;
    }
    return std::nullopt;
}

} // namespace hr::core
```
Check `src/core/text.h` for the exact `to_lower` signature (it is used by `config.cpp` as `to_lower(std::string_view)` returning a `std::string`). Add `gamef_layout.cpp` to `hr_core` in `src/core/CMakeLists.txt`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset x86-debug; ctest --preset x86-debug -L gamef_layout`
Expected: 3 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/core/gamef_layout.h src/core/gamef_layout.cpp src/core/CMakeLists.txt tests/core/gamef_layout_test.cpp tests/core/CMakeLists.txt
git commit -m "core: GAMEF.DLL layout table for Stage B, gated by SHA-256"
```

---

### Task 2: `platform::sha256_file`

**Files:**
- Create: `src/platform/sha256.h`, `src/platform/sha256.cpp`
- Modify: `src/platform/CMakeLists.txt` (add `sha256.cpp`, link `bcrypt`)
- Test: `tests/platform/sha256_test.cpp`; modify `tests/platform/CMakeLists.txt`

**Interfaces:**
- Produces: `std::optional<std::string> hr::platform::sha256_file(const std::filesystem::path& path)` — lowercase hex, nullopt if the file cannot be read.

- [ ] **Step 1: Write the failing test**

`tests/platform/sha256_test.cpp`:
```cpp
#include "platform/sha256.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>

using hr::platform::sha256_file;

TEST_CASE("sha256_file hashes a file's bytes", "[sha256]") {
    const auto path = std::filesystem::temp_directory_path() / "hr_sha256_test.bin";
    {
        std::ofstream out(path, std::ios::binary);
        out << "abc";
    }
    CHECK(sha256_file(path) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    std::filesystem::remove(path);
}

TEST_CASE("sha256_file of a missing file is nullopt", "[sha256]") {
    CHECK_FALSE(sha256_file(std::filesystem::temp_directory_path() / "hr_no_such_file.bin"));
}
```
Add `sha256_test.cpp` to `platform_tests`.

- [ ] **Step 2: Run test to verify it fails**

Run: `cmake --build --preset x86-debug`
Expected: compile error, `platform/sha256.h` not found.

- [ ] **Step 3: Write minimal implementation**

`src/platform/sha256.h`:
```cpp
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace hr::platform {

// SHA-256 of a file's contents as lowercase hex; nullopt if it cannot be read.
std::optional<std::string> sha256_file(const std::filesystem::path& path);

} // namespace hr::platform
```

`src/platform/sha256.cpp`:
```cpp
#include "platform/sha256.h"

#include <windows.h>

#include <bcrypt.h>

#include <array>
#include <fstream>
#include <vector>

namespace hr::platform {

std::optional<std::string> sha256_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0) != 0) {
        return std::nullopt;
    }
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::optional<std::string> result;
    if (BCryptCreateHash(alg, &hash, nullptr, 0, nullptr, 0, 0) == 0) {
        std::vector<char> buffer(1 << 16);
        bool ok = true;
        while (ok && in) {
            in.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
            const auto n = static_cast<ULONG>(in.gcount());
            if (n > 0) {
                ok = BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), n, 0) == 0;
            }
        }
        std::array<UCHAR, 32> digest{};
        if (ok && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) == 0) {
            static constexpr char kHex[] = "0123456789abcdef";
            std::string hex;
            for (const UCHAR byte : digest) {
                hex += kHex[byte >> 4];
                hex += kHex[byte & 0xF];
            }
            result = hex;
        }
        BCryptDestroyHash(hash);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return result;
}

} // namespace hr::platform
```
In `src/platform/CMakeLists.txt` add `sha256.cpp` to `hr_platform` and `bcrypt` to its `PUBLIC` link libraries.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset x86-debug; ctest --preset x86-debug -L sha256`
Expected: 2 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/platform/sha256.h src/platform/sha256.cpp src/platform/CMakeLists.txt tests/platform/sha256_test.cpp tests/platform/CMakeLists.txt
git commit -m "platform: SHA-256 of a file (BCrypt)"
```

---

### Task 3: `[debug] b1_spike` config key

**Files:**
- Modify: `src/core/config.h`, `src/core/config.cpp`
- Test: `tests/core/config_test.cpp`

**Interfaces:**
- Produces: `int hr::core::Config::b1_spike` (0 = off, 1..3 = render factor), key `debug.b1_spike`. Not written to the default ini (a diagnostic, like a private switch).

- [ ] **Step 1: Write the failing tests**

Append to `tests/core/config_test.cpp`:
```cpp
TEST_CASE("b1_spike is off by default", "[config]") {
    CHECK(load_config("").config.b1_spike == 0);
}

TEST_CASE("b1_spike accepts 0 to 3", "[config]") {
    const auto r = load_config("[debug]\nb1_spike = 3\n");
    CHECK(r.warnings.empty());
    CHECK(r.config.b1_spike == 3);
}

TEST_CASE("an out-of-range b1_spike warns and stays off", "[config]") {
    const auto r = load_config("[debug]\nb1_spike = 5\n");
    CHECK(r.config.b1_spike == 0);
    CHECK(r.warnings.size() == 1);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset x86-debug`
Expected: compile error, `Config` has no member `b1_spike`.

- [ ] **Step 3: Write minimal implementation**

In `src/core/config.h`, after `message_census`:
```cpp
    int b1_spike = 0;            // diagnostics: Stage B1.0 second render at this factor (0 = off)
```
In `src/core/config.cpp`, add a helper after `parse_size`:
```cpp
std::optional<int> parse_int_in(std::string_view v, int low, int high) {
    int value = 0;
    if (!parse_int(v, value) || value < low || value > high) {
        return std::nullopt;
    }
    return value;
}
```
and a field after `debug.message_census`:
```cpp
    {"debug.b1_spike",
     [](Config& c, std::string_view v) { return assign(c.b1_spike, parse_int_in(v, 0, 3)); }},
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset x86-debug; ctest --preset x86-debug -L config`
Expected: all config tests pass (the default-ini round trip is unaffected because the key is not in it).

- [ ] **Step 5: Commit**

```bash
git add src/core/config.h src/core/config.cpp tests/core/config_test.cpp
git commit -m "core: [debug] b1_spike key for the Stage B1.0 spike"
```

---

### Task 4: `core::count_differing_pixels` — comparing two 8-bit frames by colour

**Files:**
- Create: `src/core/indexed_frame.h`, `src/core/indexed_frame.cpp`
- Modify: `src/core/CMakeLists.txt`
- Test: `tests/core/indexed_frame_test.cpp`; modify `tests/core/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  struct hr::core::IndexedFrame { const std::uint8_t* pixels; int stride; const std::array<std::uint32_t, 256>* palette; };
  int hr::core::count_differing_pixels(const IndexedFrame& a, const IndexedFrame& b, int width, int height);
  ```
  Palette entries are 0x00RRGGBB; two pixels are equal when their palette colours are equal (indices may differ).

- [ ] **Step 1: Write the failing tests**

`tests/core/indexed_frame_test.cpp`:
```cpp
#include "core/indexed_frame.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

using hr::core::count_differing_pixels;
using hr::core::IndexedFrame;

namespace {
std::array<std::uint32_t, 256> palette_with(std::uint8_t index, std::uint32_t colour) {
    std::array<std::uint32_t, 256> p{};
    p[index] = colour;
    return p;
}
} // namespace

TEST_CASE("identical frames have no differing pixels", "[indexed_frame]") {
    const std::vector<std::uint8_t> px{1, 2, 3, 4};
    const auto pal = palette_with(1, 0x112233);
    CHECK(count_differing_pixels({px.data(), 2, &pal}, {px.data(), 2, &pal}, 2, 2) == 0);
}

TEST_CASE("different indices with the same colour are equal", "[indexed_frame]") {
    const std::vector<std::uint8_t> a{7};
    const std::vector<std::uint8_t> b{9};
    const auto pa = palette_with(7, 0xABCDEF);
    const auto pb = palette_with(9, 0xABCDEF);
    CHECK(count_differing_pixels({a.data(), 1, &pa}, {b.data(), 1, &pb}, 1, 1) == 0);
}

TEST_CASE("a pixel with another colour is counted", "[indexed_frame]") {
    const std::vector<std::uint8_t> a{1, 1};
    const std::vector<std::uint8_t> b{1, 2};
    const auto pal = palette_with(2, 0xFF0000);
    CHECK(count_differing_pixels({a.data(), 2, &pal}, {b.data(), 2, &pal}, 2, 1) == 1);
}

TEST_CASE("only width x height is compared, each frame with its own stride", "[indexed_frame]") {
    // a: 2x2 inside stride 4 (padding 9s); b: the same 2x2 inside stride 2.
    const std::vector<std::uint8_t> a{1, 2, 9, 9, 3, 4, 9, 9};
    const std::vector<std::uint8_t> b{1, 2, 3, 4};
    std::array<std::uint32_t, 256> pal{};
    for (int i = 0; i < 256; ++i) {
        pal[i] = static_cast<std::uint32_t>(i);
    }
    CHECK(count_differing_pixels({a.data(), 4, &pal}, {b.data(), 2, &pal}, 2, 2) == 0);
}
```
Add the file to `core_tests`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset x86-debug`
Expected: compile error, `core/indexed_frame.h` not found.

- [ ] **Step 3: Write minimal implementation**

`src/core/indexed_frame.h`:
```cpp
#pragma once

#include <array>
#include <cstdint>

namespace hr::core {

// An 8-bit palettised picture (top-down rows of `stride` bytes); palette entries 0x00RRGGBB.
struct IndexedFrame {
    const std::uint8_t* pixels;
    int stride;
    const std::array<std::uint32_t, 256>* palette;
};

// How many of the top-left width x height pixels show a different colour in `a` and `b`
// (Stage B1.0: our second render against the game's own).
int count_differing_pixels(const IndexedFrame& a, const IndexedFrame& b, int width, int height);

} // namespace hr::core
```

`src/core/indexed_frame.cpp`:
```cpp
#include "core/indexed_frame.h"

namespace hr::core {

int count_differing_pixels(const IndexedFrame& a, const IndexedFrame& b, int width, int height) {
    int differing = 0;
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* ra = a.pixels + static_cast<std::ptrdiff_t>(y) * a.stride;
        const std::uint8_t* rb = b.pixels + static_cast<std::ptrdiff_t>(y) * b.stride;
        for (int x = 0; x < width; ++x) {
            if ((*a.palette)[ra[x]] != (*b.palette)[rb[x]]) {
                ++differing;
            }
        }
    }
    return differing;
}

} // namespace hr::core
```
Add `indexed_frame.cpp` to `hr_core`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset x86-debug; ctest --preset x86-debug -L indexed_frame`
Expected: 4 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/core/indexed_frame.h src/core/indexed_frame.cpp src/core/CMakeLists.txt tests/core/indexed_frame_test.cpp tests/core/CMakeLists.txt
git commit -m "core: count differing pixels of two 8-bit frames by colour"
```

---

### Task 5: `wing32/rl_shadow` — the spike render, wired into the blits

Not unit-testable (it calls into the running game); verified by the build, the existing tests, and Task 6. Keep it small and log everything.

**Files:**
- Create: `src/wing32/rl_shadow.h`, `src/wing32/rl_shadow.cpp`
- Modify: `src/wing32/CMakeLists.txt` (add `rl_shadow.cpp`), `src/wing32/exports.cpp` (call before the original blits), `src/wing32/hooks.cpp` (`hook_CreateWindowExA` reports created windows)

**Interfaces:**
- Consumes: `core::gamef_layout`, `platform::sha256_file`, `core::Config::b1_spike`, `core::count_differing_pixels`, `platform::Subclass`, `platform::log`, `platform::guarded`, `wing32::settings()`, `wing32::module_dir()`.
- Produces:
  ```cpp
  namespace hr::wing32::rl_shadow {
  void on_window_created(HWND window, const std::string& class_name); // game thread
  void before_blit(HDC dest, HDC src);                                 // game thread
  }
  ```

- [ ] **Step 1: Write `src/wing32/rl_shadow.h`**

```cpp
#pragma once

#include <windows.h>

#include <string>

// Stage B1.0 spike (spec 2026-10-07 §9): with [debug] b1_spike = k, renders the battle's 3D scene
// a second time through our own Reality Lab device at k× when the game blits its 3D view, compares
// k = 1 with the game's frame, logs the cost and writes one BMP of a k× frame. Game thread only.
namespace hr::wing32::rl_shadow {

// After every successful CreateWindowExA in a game module: remembers the Map3DClass window and
// subclasses it so our device is destroyed before the game's own WM_DESTROY handler runs.
void on_window_created(HWND window, const std::string& class_name);

// From WinGBitBlt / WinGStretchBlt before the original: renders the shadow frame when `dest` is
// the Map3DClass window's DC.
void before_blit(HDC dest, HDC src);

} // namespace hr::wing32::rl_shadow
```

- [ ] **Step 2: Write `src/wing32/rl_shadow.cpp`**

```cpp
#include "wing32/rl_shadow.h"

#include "core/gamef_layout.h"
#include "core/indexed_frame.h"
#include "platform/log.h"
#include "platform/sha256.h"
#include "platform/window_subclass.h"
#include "wing32/runtime.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace hr::wing32::rl_shadow {
namespace {

using u32 = std::uint32_t;
using PaletteCallback = void(__cdecl*)(void*, void*, int, int, int, int);
using CreateDevice = u32(__cdecl*)(void*, int, int, int, PaletteCallback, void*);
using HandleInt = u32(__cdecl*)(u32, int);
using CreateViewport = u32(__cdecl*)(u32, u32, int, int, int, int);
using ConfigureViewport = int(__cdecl*)(u32, int, int, int, int);
using SetPlane = int(__cdecl*)(u32, float, float, float, float);
using SetFloat = int(__cdecl*)(u32, float);
using Handle = int(__cdecl*)(u32);
using Render = int(__cdecl*)(u32, u32);
using Lookup = int(__cdecl*)(int*, u32, const void*);

struct Shadow {
    std::array<std::uint32_t, 15> desc{};
    std::vector<std::uint8_t> bits;
    std::array<std::uint32_t, 256> palette{}; // 0x00RRGGBB, filled by the palette callback
    int width = 0;  // 1× size of the active 3D view
    int height = 0;
    int k = 0;
    int stride = 0;
    u32 device = 0;
    u32 viewport = 0;
};

enum class State { Unresolved, Off, On };

State g_state = State::Unresolved;
std::optional<core::GamefLayout> g_layout;
std::uintptr_t g_base = 0;
HWND g_map3d = nullptr;
std::unique_ptr<platform::Subclass> g_subclass;
std::unique_ptr<Shadow> g_shadow;
int g_frames = 0;
double g_ms_total = 0;
double g_ms_max = 0;
bool g_dumped = false;
bool g_logged_half = false;

template <typename F> F fn(u32 rva) { return reinterpret_cast<F>(g_base + rva); }

template <typename T> T read(std::uintptr_t at) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof value);
    return value;
}

void __cdecl on_palette(void*, void* arg, int index, int r, int g, int b) {
    auto* shadow = static_cast<Shadow*>(arg);
    if (index >= 0 && index < 256) {
        shadow->palette[index] = (static_cast<std::uint32_t>(r & 0xFF) << 16) |
                                 (static_cast<std::uint32_t>(g & 0xFF) << 8) |
                                 static_cast<std::uint32_t>(b & 0xFF);
    }
}

void off(const std::string& why) {
    g_state = State::Off;
    platform::log("b1 spike: off, " + why);
}

void resolve() {
    const HMODULE gamef = GetModuleHandleA("GAMEF.DLL");
    char path[MAX_PATH] = {};
    if (gamef == nullptr || GetModuleFileNameA(gamef, path, MAX_PATH) == 0) {
        off("GAMEF.DLL not loaded");
        return;
    }
    const auto hash = platform::sha256_file(path);
    g_layout = hash ? core::gamef_layout(*hash) : std::nullopt;
    platform::log("b1 spike: GAMEF.DLL sha256 " + hash.value_or("?") +
                  (g_layout ? " (known build)" : " (unknown build)"));
    if (!g_layout) {
        off("unknown GAMEF.DLL build");
        return;
    }
    g_base = reinterpret_cast<std::uintptr_t>(gamef);
    g_state = State::On;
}

// SEH boundary: no objects with destructors in this function.
template <typename F> bool seh(F&& body) {
    __try {
        body();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void destroy_shadow() {
    if (!g_shadow) {
        return;
    }
    const auto& l = *g_layout;
    Shadow* s = g_shadow.get();
    const bool ok = seh([&] {
        if (s->viewport != 0) {
            fn<Handle>(l.destroy_object)(s->viewport);
        }
        if (s->device != 0) {
            fn<Handle>(l.destroy_object)(s->device);
        }
    });
    platform::log(std::string("b1 spike: device destroyed") + (ok ? "" : " (exception)"));
    g_shadow.reset();
}

bool create_shadow(std::uintptr_t w, int width, int height, int k) {
    const auto& l = *g_layout;
    auto shadow = std::make_unique<Shadow>();
    shadow->width = width;
    shadow->height = height;
    shadow->k = k;
    shadow->stride = ((width * k + 3) / 4) * 4;
    shadow->bits.assign(static_cast<std::size_t>(shadow->stride) * height * k, 0);
    std::memcpy(shadow->desc.data(), reinterpret_cast<const void*>(w + l.w_device_desc),
                sizeof shadow->desc);
    shadow->desc[0] = static_cast<std::uint32_t>(shadow->stride);
    shadow->desc[1] = static_cast<std::uint32_t>(height * k);
    shadow->desc[6] = static_cast<std::uint32_t>(shadow->stride);
    shadow->desc[7] = static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(shadow->bits.data()));
    const u32 camera = read<u32>(w + l.w_camera);
    Shadow* s = shadow.get();
    const bool ok = seh([&] {
        s->device = fn<CreateDevice>(l.create_device)(s->desc.data(), 0, 0, 0, &on_palette, s);
        if (s->device == 0) {
            return;
        }
        fn<HandleInt>(l.set_device_mode)(s->device, 0);
        fn<HandleInt>(l.set_device_dither)(s->device, 0);
        fn<HandleInt>(l.set_device_shades)(s->device, 8);
        s->viewport = fn<CreateViewport>(l.create_viewport)(s->device, camera, 0, 0,
                                                            s->stride, height * k);
        if (s->viewport != 0) {
            fn<ConfigureViewport>(l.configure_viewport)(s->viewport, 0, 0, width * k, height * k);
        }
    });
    g_shadow = std::move(shadow);
    platform::log("b1 spike: device " + std::to_string(s->stride) + "x" +
                  std::to_string(height * k) + " (k=" + std::to_string(k) + ") device=" +
                  std::to_string(s->device) + " viewport=" + std::to_string(s->viewport) +
                  (ok ? "" : " (exception)"));
    if (!ok) {
        off("exception while creating the device");
        return false;
    }
    if (s->device == 0 || s->viewport == 0) {
        destroy_shadow();
        off("device or viewport creation failed");
        return false;
    }
    return true;
}

// Copies the game viewport's projection settings to ours; logs a front-plane mismatch once.
bool copy_viewport(std::uintptr_t w) {
    const auto& l = *g_layout;
    const u32 game_vp = read<u32>(w + l.w_viewport);
    int object = 0;
    if (fn<Lookup>(l.lookup_object)(&object, game_vp, reinterpret_cast<const void*>(g_base + l.viewport_type)) != 0 ||
        object == 0) {
        return false;
    }
    const auto o = static_cast<std::uintptr_t>(object);
    const u32 vp = g_shadow->viewport;
    fn<SetPlane>(l.set_viewport_plane)(vp, read<float>(o + l.vp_plane_a), read<float>(o + l.vp_plane_b),
                                       read<float>(o + l.vp_plane_c), read<float>(o + l.vp_plane_d));
    fn<SetFloat>(l.set_viewport_back)(vp, read<float>(o + l.vp_back));
    fn<HandleInt>(l.set_viewport_flag_d8)(vp, read<int>(o + l.vp_flag_d8));
    fn<HandleInt>(l.set_viewport_flag_44)(vp, read<int>(o + l.vp_flag_44));
    static bool logged_front = false;
    if (!logged_front) {
        logged_front = true;
        platform::log("b1 spike: game viewport front " + std::to_string(read<float>(o + l.vp_front)) +
                      " back " + std::to_string(read<float>(o + l.vp_back)));
    }
    return true;
}

void write_bmp(const Shadow& s) {
    const int w = s.width * s.k;
    const int h = s.height * s.k;
    const auto path = module_dir() / ("horned-rat-b1-k" + std::to_string(s.k) + ".bmp");
    BITMAPINFOHEADER info{};
    info.biSize = sizeof info;
    info.biWidth = s.stride;
    info.biHeight = -h; // top-down
    info.biPlanes = 1;
    info.biBitCount = 8;
    info.biClrUsed = 256;
    const DWORD palette_bytes = 256 * 4;
    const DWORD pixel_bytes = static_cast<DWORD>(s.stride) * h;
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof file + sizeof info + palette_bytes;
    file.bfSize = file.bfOffBits + pixel_bytes;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof file);
    out.write(reinterpret_cast<const char*>(&info), sizeof info);
    for (const std::uint32_t c : s.palette) {
        const RGBQUAD q{static_cast<BYTE>(c & 0xFF), static_cast<BYTE>((c >> 8) & 0xFF),
                        static_cast<BYTE>((c >> 16) & 0xFF), 0};
        out.write(reinterpret_cast<const char*>(&q), sizeof q);
    }
    out.write(reinterpret_cast<const char*>(s.bits.data()), pixel_bytes);
    platform::log("b1 spike: wrote " + path.string() + " (" + std::to_string(w) + "x" +
                  std::to_string(h) + ")");
}

void compare_with_game(std::uintptr_t w, HDC src) {
    const auto& l = *g_layout;
    std::array<RGBQUAD, 256> quads{};
    GetDIBColorTable(src, 0, 256, quads.data());
    std::array<std::uint32_t, 256> game_palette{};
    for (int i = 0; i < 256; ++i) {
        game_palette[i] = (static_cast<std::uint32_t>(quads[i].rgbRed) << 16) |
                          (static_cast<std::uint32_t>(quads[i].rgbGreen) << 8) | quads[i].rgbBlue;
    }
    const auto* game_bits = reinterpret_cast<const std::uint8_t*>(read<std::uintptr_t>(w + l.w_bits));
    const int game_stride = static_cast<int>(read<std::uint32_t>(w + l.w_device_desc));
    const Shadow& s = *g_shadow;
    const int differing = core::count_differing_pixels({game_bits, game_stride, &game_palette},
                                                       {s.bits.data(), s.stride, &s.palette},
                                                       s.width, s.height);
    if (g_frames <= 3 || g_frames % 50 == 0) {
        platform::log("b1 spike: frame " + std::to_string(g_frames) + ", " +
                      std::to_string(differing) + " of " + std::to_string(s.width * s.height) +
                      " pixels differ");
    }
}

void render(HDC src) {
    const auto& l = *g_layout;
    const auto w = static_cast<std::uintptr_t>(GetWindowLongA(GetParent(g_map3d), 4));
    if (w == 0) {
        return;
    }
    if (read<int>(w + l.w_divisor) != 1) {
        if (!g_logged_half) {
            g_logged_half = true;
            platform::log("b1 spike: half detail, not rendered");
        }
        return;
    }
    const int width = read<int>(w + l.w_active_width);
    const int height = read<int>(w + l.w_active_height);
    const int k = settings().config.b1_spike;
    if (!g_shadow || g_shadow->width != width || g_shadow->height != height || g_shadow->k != k) {
        destroy_shadow();
        if (!create_shadow(w, width, height, k)) {
            return;
        }
    }
    const u32 scene = read<u32>(w + l.w_scene);
    LARGE_INTEGER t0, t1, freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    bool copied = false;
    Shadow* s = g_shadow.get();
    const bool ok = seh([&] {
        copied = copy_viewport(w);
        if (copied) {
            fn<Handle>(l.clear_viewport)(s->viewport);
            fn<Render>(l.render_viewport)(s->viewport, scene);
        }
    });
    QueryPerformanceCounter(&t1);
    if (!ok) {
        off("exception while rendering");
        return;
    }
    if (!copied) {
        return;
    }
    ++g_frames;
    const double ms = 1000.0 * static_cast<double>(t1.QuadPart - t0.QuadPart) /
                      static_cast<double>(freq.QuadPart);
    g_ms_total += ms;
    g_ms_max = ms > g_ms_max ? ms : g_ms_max;
    if (g_frames % 50 == 0) {
        platform::log("b1 spike: " + std::to_string(g_frames) + " frames, render avg " +
                      std::to_string(g_ms_total / g_frames) + " ms, max " +
                      std::to_string(g_ms_max) + " ms (k=" + std::to_string(k) + ")");
    }
    if (k == 1) {
        compare_with_game(w, src);
    }
    if (!g_dumped && g_frames == 20) {
        g_dumped = true;
        write_bmp(*s);
    }
}

} // namespace

void on_window_created(HWND window, const std::string& class_name) {
    if (settings().config.b1_spike == 0 || class_name != "Map3DClass") {
        return;
    }
    if (g_state == State::Unresolved) {
        resolve();
    }
    if (g_state != State::On) {
        return;
    }
    g_map3d = window;
    g_subclass = std::make_unique<platform::Subclass>(
        window, [](HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, WNDPROC original) {
            if (msg == WM_DESTROY) {
                destroy_shadow();
                g_map3d = nullptr;
            }
            return CallWindowProcA(original, hwnd, msg, wparam, lparam);
        });
    platform::log("b1 spike: watching Map3DClass window");
}

void before_blit(HDC dest, HDC src) {
    if (g_state != State::On || g_map3d == nullptr || WindowFromDC(dest) != g_map3d) {
        return;
    }
    render(src);
}

} // namespace hr::wing32::rl_shadow
```
Notes for the implementer:
- `__try` cannot be used in a function that needs object unwinding; `seh()` takes a lambda whose captured objects are references only. If MSVC still reports C2712, move the body of each `seh` call into a separate `static` function taking a plain struct pointer.
- `g_subclass` is never reset after `WM_DESTROY` while the window procedure is running; reset it on the next `on_window_created` instead (assigning a new `unique_ptr` does that). The `Subclass` destructor restores the procedure of a window that no longer exists, which `SetWindowLongPtr` ignores.

Add `rl_shadow.cpp` to `wing32` in `src/wing32/CMakeLists.txt`.

- [ ] **Step 3: Wire it in**

In `src/wing32/exports.cpp`, include `wing32/rl_shadow.h` and `platform/guard.h`, then in both blits before calling the original:
```cpp
    hr::platform::guarded("rl_shadow", [&] { hr::wing32::rl_shadow::before_blit(dest, src); }, [] {});
```
In `src/wing32/hooks.cpp`, include `wing32/rl_shadow.h` and in `hook_CreateWindowExA` after `a2::on_window_created(result, parent, style);`:
```cpp
                rl_shadow::on_window_created(result, cls);
```

- [ ] **Step 4: Build and run all tests**

Run: `. ./tools/vsenv.ps1; cmake --build --preset x86-debug; ctest --preset x86-debug; cmake --build --preset x86-release`
Expected: builds without warnings; all tests pass (the `SOTHR_GAME_DIR` test stays skipped).

- [ ] **Step 5: Commit, push, open a draft PR**

```bash
git add src/wing32/rl_shadow.h src/wing32/rl_shadow.cpp src/wing32/CMakeLists.txt src/wing32/exports.cpp src/wing32/hooks.cpp
git commit -m "wing32: B1.0 spike, a second Reality Lab render of the battle scene"
git push -u origin stage-b1
gh pr create --draft --title "Stage B1.0: spike for a second Reality Lab render" --body "<summary of spec + plan; ends with the Claude Code line>"
```
Then bind the PR and turn Auto-fix on.

---

### Task 6: Run the spike in the game and record the result

**Files:**
- Create: `docs/re-notes/b1-0-exit.md`
- Modify: `docs/superpowers/specs/2026-10-07-stage-b-battle-view-design.md` (§9 B1.0 result line)

- [ ] **Step 1: Install and ask for the ini change**

Install the release build (pre-approved). Ask the user to approve adding `b1_spike = 1` under `[debug]` in `horned-rat.ini`.

- [ ] **Step 2: Run 1 (k = 1)**

The user starts a battle and plays ~1 minute, then quits. Read `horned-rat.log`: expect `b1 spike: GAMEF.DLL sha256 … (known build)`, `watching Map3DClass window`, `device 420x296 (k=1)` (417 rounded up to 420), `frame N, D of 123432 pixels differ`, timing lines, and no `off,` line or access violation. Also ask whether the game looked and played exactly as before.

**Pass:** D = 0 in all compared frames (after the first two, which may precede the first palette callback). If D > 0, convert the BMP (Step 4) and compare it with a screenshot; investigate with the systematic-debugging skill before going on.

- [ ] **Step 3: Runs 2 and 3 (k = 2, k = 3)**

With the user's OK set `b1_spike = 2`, then `3`. Expect device `836x592` / `1252x888`, timing lines, no `off,` line, and the game unchanged.

- [ ] **Step 4: Look at the frames**

Convert each `WARFB\horned-rat-b1-k*.bmp` to PNG in the scratchpad (never in the repo) and view it:
```powershell
Add-Type -AssemblyName System.Drawing
$bmp = [System.Drawing.Bitmap]::FromFile('<WARFB>\horned-rat-b1-k3.bmp'); $bmp.Save('<scratchpad>\b1-k3.png', [System.Drawing.Imaging.ImageFormat]::Png); $bmp.Dispose()
```
Check: the same view as the game, more detail at k = 3, correct colours, nothing cut off.

- [ ] **Step 5: Restore the ini, write the exit notes**

Remove the `b1_spike` line again. `docs/re-notes/b1-0-exit.md`: setup, the three runs (log lines, pixel differences, render times avg/max per k), what the frames showed, problems found, and the recommendation for B1.1 (go / no-go and why). File open items as GitHub issues (`gh issue list` first). Add one line to the spec's §9 B1.0 with the result.

- [ ] **Step 6: Commit, push, mark ready**

```bash
git add docs/re-notes/b1-0-exit.md docs/superpowers/specs/2026-10-07-stage-b-battle-view-design.md
git commit -m "docs: B1.0 spike results"
```
Check the PR is open, push, `gh pr ready`. Then ask the user whether B1.1 goes ahead.
