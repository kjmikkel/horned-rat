# Stage B1.1 Hi-res Overlay Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show the battle's 3D view in the presenter at k× resolution (a second Reality Lab render of the game's scene), masked so anything the game draws over the view stays visible, with a `render_scale` key and a slow-render fallback.

**Architecture:** The B1.0 spike becomes the production second render (`wing32/rl_shadow`): on the game thread, during the game's 3D blit, it renders the scene at the factor the presenter asks for and publishes the frame (k× pixels, the game's 1× pixels, the game's palette) through a `core::Latest` mailbox. The presenter takes the newest frame, compares the captured picture with the game's 1× frame per pixel (`core::overlay_mask`), builds a straight-alpha k× texture only when the frame or the mask changes (`core::compose_overlay`), and draws it into the 3D view's on-screen rect (`core::map_rect`) with the active scaling filter.

**Tech Stack:** C++20, MSVC x86, CMake presets, Catch2, Win32, D3D11 (WARP in tests).

**Spec:** `docs/superpowers/specs/2026-10-07-stage-b-battle-view-design.md` (§5, §6, §7, §9 B1.1) and the B1.0 results `docs/re-notes/b1-0-exit.md`.

**Deviations from the spec (decided while planning):**
- Palette conversion lives in `core` (`core::to_bgra`, `core::compose_overlay`) instead of `platform/palette_frame`: it needs no Windows headers and is testable on Linux CI.
- The mask is applied on the CPU when building the overlay texture (straight alpha, uploaded only when the frame or the mask changes) instead of in a shader; the presenter's existing alpha-blended quad draws it.
- The B1.0 spike key `[debug] b1_spike` and its diagnostics (1× comparison, object diff, BMP dumps, `core::count_differing_pixels`) are removed (Task 7); they are documented in the B1.0 exit notes and in git history.
- A failed device or viewport creation turns B1 off for the session (spec §7 said "retried next battle"); simpler, and B1.0 never saw it fail.

## Global Constraints

- Layering: `wing32 → platform → core`; `core` has no Windows headers and builds on Linux CI.
- Red/green TDD for everything testable outside the game.
- Never commit game binaries, logs, captures, dumps or decompiled code.
- Stage B runs only for `GAMEF.DLL` SHA-256 `e8afe836e9c99b54a76643b2adb4edb52f56a991d53ab8af60f746129b1132d6` (`core::gamef_layout`).
- Every Reality Lab call runs on the game thread under SEH; any exception turns B1 off for the session and the game's blit still runs.
- B1.0 facts: the ramp palette is shared (our pixels are the game's palette indices; the palette comes from `GetDIBColorTable` on the game's source DC); the viewport is configured `align4(w)·k` wide (`align4(w) = ((w + 3) / 4) * 4`), the visible picture is the first `w·k` columns.
- Config: `[display] render_scale = auto | 1 | 2 | 3 | 4` (`1` = off, vanilla view); invalid values warn and use `auto`.
- Build/test: `. ./tools/vsenv.ps1; cmake --build --preset x86-debug; ctest --preset x86-debug`; release `x86-release`; formatting `clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.h')` (clang-format 19.1.7). Install (pre-approved): `./tools/install.ps1 -GameDir 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat' -SourceDll build/x86-release/bin/wing32.dll`. Ini changes need the user's OK; restore afterwards.
- Branch `stage-b1-1` from `stage-b1` (PR #13 may still be open; if it has been merged, branch from `origin/main`). Commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`; PR body ends with `🤖 Generated with [Claude Code](https://claude.com/claude-code)`; check the PR is open before pushing; Auto-fix on.

---

### Task 1: `[display] render_scale`

**Files:**
- Modify: `src/core/config.h`, `src/core/config.cpp`
- Test: `tests/core/config_test.cpp`

**Interfaces:**
- Produces: `int hr::core::Config::render_scale` (0 = auto, 1..4 = fixed; 1 = off), key `display.render_scale`, in the default ini. (`b1_spike` stays until Task 7 removes the spike, so every commit builds.)

- [ ] **Step 1: Write the failing tests**

Append to `tests/core/config_test.cpp`:
```cpp
TEST_CASE("render_scale is auto by default", "[config]") {
    CHECK(load_config("").config.render_scale == 0);
}

TEST_CASE("render_scale reads auto and 1 to 4", "[config]") {
    CHECK(load_config("[display]\nrender_scale = 3\n").config.render_scale == 3);
    CHECK(load_config("[display]\nrender_scale = 1\n").config.render_scale == 1);
    const auto r = load_config("[display]\nrender_scale = AUTO\n");
    CHECK(r.warnings.empty());
    CHECK(r.config.render_scale == 0);
}

TEST_CASE("an invalid render_scale warns and stays auto", "[config]") {
    for (const char* bad : {"0", "5", "sharp"}) {
        const auto r = load_config(std::string("[display]\nrender_scale = ") + bad + "\n");
        CHECK(r.config.render_scale == 0);
        CHECK(r.warnings.size() == 1);
    }
}
```
Also add `render_scale = 2` to the `[display]` part of the "every key is read" test's input and `CHECK(r.config.render_scale == 2);` to its checks. Add `#include <string>` if the file does not have it.

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset x86-debug`
Expected: compile error, `Config` has no member `render_scale`.

- [ ] **Step 3: Write minimal implementation**

`src/core/config.h`, after `cursor`:
```cpp
    int render_scale = 0;        // battle 3D view render factor: 0 = auto, 1 = off, 2..4 (Stage B)
```
`src/core/config.cpp`, after `display.cursor` (`parse_int_in` already exists):
```cpp
    {"display.render_scale",
     [](Config& c, std::string_view v) {
         if (to_lower(v) == "auto") {
             c.render_scale = 0;
             return true;
         }
         return assign(c.render_scale, parse_int_in(v, 1, 4));
     }},
```
and in `default_config_ini()` after the `cursor` line:
```
render_scale = auto        ; battle 3D view detail: auto | 1 (off, original) | 2 | 3 | 4
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset x86-debug; ctest --preset x86-debug -L config`
Expected: all config tests pass, including the default-ini round trip.

- [ ] **Step 5: Commit**

```bash
git add src/core/config.h src/core/config.cpp tests/core/config_test.cpp
git commit -m "core: [display] render_scale key for the sharp battle 3D view"
```

---

### Task 2: `core::render_factor` and `core::RenderBudget`

**Files:**
- Create: `src/core/render_scale.h`, `src/core/render_scale.cpp`
- Modify: `src/core/CMakeLists.txt`
- Test: `tests/core/render_scale_test.cpp`; modify `tests/core/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  int hr::core::render_factor(int setting, Size content, Rect dest);
  class hr::core::RenderBudget { public: void add(double now_s, double ms, int factor); int cap() const; };
  ```

- [ ] **Step 1: Write the failing tests**

`tests/core/render_scale_test.cpp`:
```cpp
#include "core/render_scale.h"

#include <catch2/catch_test_macros.hpp>

using hr::core::render_factor;
using hr::core::RenderBudget;

TEST_CASE("auto rounds the on-screen scale up, the larger axis wins", "[render_scale]") {
    CHECK(render_factor(0, {640, 480}, {0, 0, 640, 480}) == 1);
    CHECK(render_factor(0, {640, 480}, {0, 0, 1280, 960}) == 2);
    CHECK(render_factor(0, {640, 480}, {240, 0, 1440, 1080}) == 3); // sharp at 2.25
    CHECK(render_factor(0, {640, 480}, {0, 60, 1920, 960}) == 3);   // integer stretch 3x2
}

TEST_CASE("auto is clamped to 1..4", "[render_scale]") {
    CHECK(render_factor(0, {640, 480}, {0, 0, 320, 240}) == 1);
    CHECK(render_factor(0, {640, 480}, {0, 0, 3840, 2880}) == 4);
}

TEST_CASE("a fixed setting is used as is", "[render_scale]") {
    CHECK(render_factor(2, {640, 480}, {0, 0, 3840, 2880}) == 2);
    CHECK(render_factor(1, {640, 480}, {0, 0, 1920, 1440}) == 1);
}

TEST_CASE("an empty picture or destination renders at 1", "[render_scale]") {
    CHECK(render_factor(0, {0, 0}, {0, 0, 1280, 960}) == 1);
    CHECK(render_factor(0, {640, 480}, {0, 0, 0, 0}) == 1);
}

TEST_CASE("fast renders keep the cap at 4", "[render_scale]") {
    RenderBudget budget;
    for (int i = 0; i <= 30; ++i) {
        budget.add(i * 0.1, 3.0, 3);
    }
    CHECK(budget.cap() == 4);
}

TEST_CASE("a 2 s window averaging over 50 ms lowers the cap below the factor", "[render_scale]") {
    RenderBudget budget;
    for (int i = 0; i <= 20; ++i) {
        budget.add(i * 0.1, 60.0, 3);
    }
    CHECK(budget.cap() == 2);
}

TEST_CASE("the cap never goes below 1", "[render_scale]") {
    RenderBudget budget;
    for (int i = 0; i <= 20; ++i) {
        budget.add(i * 0.1, 80.0, 1);
    }
    CHECK(budget.cap() == 1);
}

TEST_CASE("a slow spike inside a fast window does not lower the cap", "[render_scale]") {
    RenderBudget budget;
    budget.add(0.0, 120.0, 3);
    for (int i = 1; i <= 20; ++i) {
        budget.add(i * 0.1, 2.0, 3);
    }
    CHECK(budget.cap() == 4);
}
```
Add `render_scale_test.cpp` to `core_tests`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset x86-debug --target core_tests`
Expected: compile error, `core/render_scale.h` not found.

- [ ] **Step 3: Write minimal implementation**

`src/core/render_scale.h`:
```cpp
#pragma once

#include "core/geometry.h"

namespace hr::core {

// Stage B1 render factor (spec 2026-10-07 §5.2). `setting` 0 = auto: the picture's on-screen
// scale (dest / content per axis) rounded up, the larger axis, clamped to 1..4. 1..4 = fixed.
int render_factor(int setting, Size content, Rect dest);

// Spec §7: over each 2 s window, a mean render time above 50 ms lowers the cap to one below the
// factor in use, down to 1. The cap never rises again in a session.
class RenderBudget {
  public:
    void add(double now_s, double ms, int factor);
    int cap() const { return cap_; }

  private:
    int cap_ = 4;
    double window_start_ = -1.0;
    double total_ms_ = 0.0;
    int count_ = 0;
};

} // namespace hr::core
```

`src/core/render_scale.cpp`:
```cpp
#include "core/render_scale.h"

#include <algorithm>

namespace hr::core {

int render_factor(int setting, Size content, Rect dest) {
    if (setting >= 1) {
        return std::min(setting, 4);
    }
    if (content.width <= 0 || content.height <= 0 || dest.width <= 0 || dest.height <= 0) {
        return 1;
    }
    const int kx = (dest.width + content.width - 1) / content.width;
    const int ky = (dest.height + content.height - 1) / content.height;
    return std::clamp(std::max(kx, ky), 1, 4);
}

void RenderBudget::add(double now_s, double ms, int factor) {
    if (window_start_ < 0.0) {
        window_start_ = now_s;
    }
    total_ms_ += ms;
    ++count_;
    if (now_s - window_start_ >= 2.0) {
        if (total_ms_ / count_ > 50.0) {
            cap_ = std::max(1, std::min(cap_, factor - 1));
        }
        window_start_ = now_s;
        total_ms_ = 0.0;
        count_ = 0;
    }
}

} // namespace hr::core
```
Add `render_scale.cpp` to `hr_core`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset x86-debug --target core_tests; ctest --preset x86-debug -L render_scale`
Expected: 8 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/core/render_scale.h src/core/render_scale.cpp src/core/CMakeLists.txt tests/core/render_scale_test.cpp tests/core/CMakeLists.txt
git commit -m "core: B1 render factor and slow-render budget"
```

---

### Task 3: `core::map_rect` — a content rect on screen

**Files:**
- Modify: `src/core/viewport.h`, `src/core/viewport.cpp`
- Test: `tests/core/viewport_test.cpp`

**Interfaces:**
- Produces: `Rect hr::core::map_rect(Rect r, Size content, Rect dest);` — where a rect of the content picture lands when the picture is drawn into `dest` (edges rounded to the nearest pixel).

- [ ] **Step 1: Write the failing tests**

Append to `tests/core/viewport_test.cpp` (it already includes `core/viewport.h` and has `using namespace hr::core` or qualified names; follow the file's style):
```cpp
TEST_CASE("map_rect at 1:1 only offsets by the destination", "[viewport]") {
    CHECK(hr::core::map_rect({10, 20, 30, 40}, {640, 480}, {5, 7, 640, 480}) ==
          hr::core::Rect{15, 27, 30, 40});
}

TEST_CASE("map_rect scales by the destination", "[viewport]") {
    CHECK(hr::core::map_rect({10, 20, 30, 40}, {640, 480}, {0, 0, 1280, 960}) ==
          hr::core::Rect{20, 40, 60, 80});
}

TEST_CASE("map_rect rounds fractional edges", "[viewport]") {
    // 2.25x: left 5 -> 11.25 -> 11, right 422 -> 949.5 -> 950, top 5 -> 11, bottom 301 -> 677
    CHECK(hr::core::map_rect({5, 5, 417, 296}, {640, 480}, {240, 0, 1440, 1080}) ==
          hr::core::Rect{251, 11, 939, 666});
}

TEST_CASE("map_rect of an empty content is empty", "[viewport]") {
    CHECK(hr::core::map_rect({1, 1, 2, 2}, {0, 0}, {0, 0, 100, 100}) == hr::core::Rect{});
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset x86-debug --target core_tests`
Expected: compile error, `map_rect` is not a member of `hr::core`.

- [ ] **Step 3: Write minimal implementation**

`src/core/viewport.h`, after `to_content`:
```cpp
// Where `r` (in content coordinates) lands when the content is drawn into `dest`; each edge is
// scaled and rounded to the nearest pixel. Empty for an empty content.
Rect map_rect(Rect r, Size content, Rect dest);
```
`src/core/viewport.cpp` (add `#include <cmath>` if missing):
```cpp
Rect map_rect(Rect r, Size content, Rect dest) {
    if (content.width <= 0 || content.height <= 0) {
        return {};
    }
    const auto sx = [&](int x) {
        return dest.x + static_cast<int>(
                            std::lround(static_cast<double>(x) * dest.width / content.width));
    };
    const auto sy = [&](int y) {
        return dest.y + static_cast<int>(
                            std::lround(static_cast<double>(y) * dest.height / content.height));
    };
    const int left = sx(r.x);
    const int top = sy(r.y);
    return {left, top, sx(r.x + r.width) - left, sy(r.y + r.height) - top};
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset x86-debug --target core_tests; ctest --preset x86-debug -L viewport`
Expected: all viewport tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/core/viewport.h src/core/viewport.cpp tests/core/viewport_test.cpp
git commit -m "core: map a content rect onto the presenter destination"
```

---

### Task 4: `core` overlay pixels — `to_bgra`, `overlay_mask`, `compose_overlay`

**Files:**
- Create: `src/core/overlay.h`, `src/core/overlay.cpp`
- Modify: `src/core/CMakeLists.txt`
- Test: `tests/core/overlay_test.cpp`; modify `tests/core/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  using hr::core::Palette = std::array<std::uint32_t, 256>; // 0x00RRGGBB
  std::vector<std::uint32_t> hr::core::to_bgra(const std::uint8_t* pixels, int stride, Size size, const Palette& palette);
  std::vector<std::uint8_t> hr::core::overlay_mask(const std::uint32_t* captured, Size captured_size, Rect view, const std::vector<std::uint32_t>& game);
  std::vector<std::uint32_t> hr::core::compose_overlay(const std::uint8_t* hires, int stride, Size size, int factor, const Palette& palette, const std::vector<std::uint8_t>& mask);
  ```

- [ ] **Step 1: Write the failing tests**

`tests/core/overlay_test.cpp`:
```cpp
#include "core/overlay.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using hr::core::compose_overlay;
using hr::core::overlay_mask;
using hr::core::Palette;
using hr::core::to_bgra;

namespace {
Palette test_palette() {
    Palette p{};
    p[1] = 0x112233;
    p[2] = 0x445566;
    p[3] = 0x778899;
    return p;
}
} // namespace

TEST_CASE("to_bgra maps indices through the palette, row by row", "[overlay]") {
    const std::vector<std::uint8_t> px{1, 2, 9, 3, 1, 9}; // 2x2 in stride 3
    const auto out = to_bgra(px.data(), 3, {2, 2}, test_palette());
    CHECK(out == std::vector<std::uint32_t>{0x112233, 0x445566, 0x778899, 0x112233});
}

TEST_CASE("the mask is set where the capture shows the game's own pixel", "[overlay]") {
    // 4x3 capture; the view is the 2x2 at (1,1).
    std::vector<std::uint32_t> captured(12, 0xAA000000u);
    captured[1 * 4 + 1] = 0xFF112233u; // matches (alpha ignored)
    captured[1 * 4 + 2] = 0x00999999u; // covered by something else
    captured[2 * 4 + 1] = 0x00445566u; // matches
    captured[2 * 4 + 2] = 0x00778899u; // matches
    const std::vector<std::uint32_t> game{0x112233, 0x123456, 0x445566, 0x778899};
    CHECK(overlay_mask(captured.data(), {4, 3}, {1, 1, 2, 2}, game) ==
          std::vector<std::uint8_t>{1, 0, 1, 1});
}

TEST_CASE("view pixels outside the capture count as covered", "[overlay]") {
    const std::vector<std::uint32_t> captured(4, 0x00112233u); // 2x2
    const std::vector<std::uint32_t> game(4, 0x112233u);
    CHECK(overlay_mask(captured.data(), {2, 2}, {1, 1, 2, 2}, game) ==
          std::vector<std::uint8_t>{1, 0, 0, 0});
}

TEST_CASE("compose_overlay scales the mask by the factor and keeps colour under alpha 0",
          "[overlay]") {
    // 1x size 2x1 at factor 2: hires 4x2 in stride 5.
    const std::vector<std::uint8_t> hires{1, 2, 3, 1, 0, 2, 3, 1, 2, 0};
    const std::vector<std::uint8_t> mask{1, 0};
    const auto out = compose_overlay(hires.data(), 5, {2, 1}, 2, test_palette(), mask);
    CHECK(out == std::vector<std::uint32_t>{0xFF112233u, 0xFF445566u, 0x00778899u, 0x00112233u,
                                            0xFF445566u, 0xFF778899u, 0x00112233u, 0x00445566u});
}
```
Add `overlay_test.cpp` to `core_tests`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset x86-debug --target core_tests`
Expected: compile error, `core/overlay.h` not found.

- [ ] **Step 3: Write minimal implementation**

`src/core/overlay.h`:
```cpp
#pragma once

#include "core/geometry.h"

#include <array>
#include <cstdint>
#include <vector>

namespace hr::core {

// 256 colours as 0x00RRGGBB.
using Palette = std::array<std::uint32_t, 256>;

// 8-bit pixels (rows of `stride` bytes) as 0x00RRGGBB, rows of size.width.
std::vector<std::uint32_t> to_bgra(const std::uint8_t* pixels, int stride, Size size,
                                   const Palette& palette);

// Stage B1 (spec 2026-10-07 §5.3): per pixel of the 3D view, 1 where the captured picture shows
// the game's own 1× render (`game`, view.width x view.height, 0x00RRGGBB), else 0. `view` is
// where the 3D view lies in the capture; parts outside the capture count as covered. Alpha bytes
// of the capture are ignored.
std::vector<std::uint8_t> overlay_mask(const std::uint32_t* captured, Size captured_size,
                                       Rect view, const std::vector<std::uint32_t>& game);

// Our factor× render (rows of `stride` bytes; size is the 1× size) as straight-alpha 0xAARRGGBB
// rows of size.width·factor: opaque where the 1× mask is set, alpha 0 (colour kept, so filtering
// at mask edges does not darken) elsewhere.
std::vector<std::uint32_t> compose_overlay(const std::uint8_t* hires, int stride, Size size,
                                           int factor, const Palette& palette,
                                           const std::vector<std::uint8_t>& mask);

} // namespace hr::core
```

`src/core/overlay.cpp`:
```cpp
#include "core/overlay.h"

#include <cstddef>

namespace hr::core {

std::vector<std::uint32_t> to_bgra(const std::uint8_t* pixels, int stride, Size size,
                                   const Palette& palette) {
    std::vector<std::uint32_t> out(static_cast<std::size_t>(size.width) * size.height);
    for (int y = 0; y < size.height; ++y) {
        const std::uint8_t* row = pixels + static_cast<std::ptrdiff_t>(y) * stride;
        for (int x = 0; x < size.width; ++x) {
            out[static_cast<std::size_t>(y) * size.width + x] = palette[row[x]];
        }
    }
    return out;
}

std::vector<std::uint8_t> overlay_mask(const std::uint32_t* captured, Size captured_size,
                                       Rect view, const std::vector<std::uint32_t>& game) {
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(view.width) * view.height, 0);
    for (int y = 0; y < view.height; ++y) {
        const int cy = view.y + y;
        if (cy < 0 || cy >= captured_size.height) {
            continue;
        }
        for (int x = 0; x < view.width; ++x) {
            const int cx = view.x + x;
            if (cx < 0 || cx >= captured_size.width) {
                continue;
            }
            const std::size_t i = static_cast<std::size_t>(y) * view.width + x;
            const std::uint32_t seen =
                captured[static_cast<std::size_t>(cy) * captured_size.width + cx] & 0xFFFFFFu;
            mask[i] = seen == (game[i] & 0xFFFFFFu) ? 1 : 0;
        }
    }
    return mask;
}

std::vector<std::uint32_t> compose_overlay(const std::uint8_t* hires, int stride, Size size,
                                           int factor, const Palette& palette,
                                           const std::vector<std::uint8_t>& mask) {
    const int width = size.width * factor;
    const int height = size.height * factor;
    std::vector<std::uint32_t> out(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = hires + static_cast<std::ptrdiff_t>(y) * stride;
        const std::uint8_t* mask_row = mask.data() + static_cast<std::size_t>(y / factor) * size.width;
        for (int x = 0; x < width; ++x) {
            const std::uint32_t alpha = mask_row[x / factor] != 0 ? 0xFF000000u : 0u;
            out[static_cast<std::size_t>(y) * width + x] = alpha | (palette[row[x]] & 0xFFFFFFu);
        }
    }
    return out;
}

} // namespace hr::core
```
Add `overlay.cpp` to `hr_core`.

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset x86-debug --target core_tests; ctest --preset x86-debug -L overlay`
Expected: 4 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/core/overlay.h src/core/overlay.cpp src/core/CMakeLists.txt tests/core/overlay_test.cpp tests/core/CMakeLists.txt
git commit -m "core: B1 overlay pixels, mask and composition"
```

---

### Task 5: `core::Latest<T>` and `core::ShadowFrame`

**Files:**
- Create: `src/core/latest.h`, `src/core/shadow_frame.h`
- Test: `tests/core/latest_test.cpp`; modify `tests/core/CMakeLists.txt`

**Interfaces:**
- Produces:
  ```cpp
  template <typename T> class hr::core::Latest { public: void publish(T value); bool take_newer(std::uint64_t& seen, T& out) const; };
  struct hr::core::ShadowFrame { std::vector<std::uint8_t> hires; int stride; Size size; int factor; std::vector<std::uint8_t> game; Palette palette; std::uintptr_t view_window; double time_s; };
  ```

- [ ] **Step 1: Write the failing tests**

`tests/core/latest_test.cpp`:
```cpp
#include "core/latest.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using hr::core::Latest;

TEST_CASE("nothing is taken before the first publish", "[latest]") {
    const Latest<std::string> box;
    std::uint64_t seen = 0;
    std::string out = "unchanged";
    CHECK_FALSE(box.take_newer(seen, out));
    CHECK(out == "unchanged");
}

TEST_CASE("a published value is taken once", "[latest]") {
    Latest<std::string> box;
    box.publish("a");
    std::uint64_t seen = 0;
    std::string out;
    CHECK(box.take_newer(seen, out));
    CHECK(out == "a");
    CHECK_FALSE(box.take_newer(seen, out));
}

TEST_CASE("only the newest value is taken", "[latest]") {
    Latest<std::string> box;
    box.publish("a");
    box.publish("b");
    std::uint64_t seen = 0;
    std::string out;
    CHECK(box.take_newer(seen, out));
    CHECK(out == "b");
}

TEST_CASE("each reader keeps its own place", "[latest]") {
    Latest<int> box;
    box.publish(1);
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    int out = 0;
    CHECK(box.take_newer(first, out));
    box.publish(2);
    CHECK(box.take_newer(second, out));
    CHECK(out == 2);
    CHECK(box.take_newer(first, out));
    CHECK(out == 2);
}
```
Add `latest_test.cpp` to `core_tests`.

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset x86-debug --target core_tests`
Expected: compile error, `core/latest.h` not found.

- [ ] **Step 3: Write minimal implementation**

`src/core/latest.h`:
```cpp
#pragma once

#include <cstdint>
#include <mutex>
#include <utility>

namespace hr::core {

// The newest value from one thread for readers on others (Stage B1: the game thread's second
// render for the presenter). Readers copy the value only when it is newer than what they saw.
template <typename T> class Latest {
  public:
    void publish(T value) {
        const std::lock_guard lock(mutex_);
        value_ = std::move(value);
        ++sequence_;
    }

    // Copies the value into `out` and returns true if it is newer than `seen` (updated).
    bool take_newer(std::uint64_t& seen, T& out) const {
        const std::lock_guard lock(mutex_);
        if (sequence_ == seen) {
            return false;
        }
        out = value_;
        seen = sequence_;
        return true;
    }

  private:
    mutable std::mutex mutex_;
    T value_{};
    std::uint64_t sequence_ = 0;
};

} // namespace hr::core
```

`src/core/shadow_frame.h`:
```cpp
#pragma once

#include "core/geometry.h"
#include "core/overlay.h"

#include <cstdint>
#include <vector>

namespace hr::core {

// One second render of the battle's 3D view (spec 2026-10-07 §5.1), handed from the game thread
// to the presenter.
struct ShadowFrame {
    std::vector<std::uint8_t> hires; // factor× palette indices, rows of `stride` bytes
    int stride = 0;
    Size size{};                     // the view's 1× size (the visible picture is size·factor)
    int factor = 1;
    std::vector<std::uint8_t> game;  // the game's own 1× indices, rows of size.width
    Palette palette{};               // the game's palette (shared by both renders)
    std::uintptr_t view_window = 0;  // the Map3DClass window
    double time_s = 0.0;             // steady clock, seconds
};

} // namespace hr::core
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset x86-debug --target core_tests; ctest --preset x86-debug -L latest`
Expected: 4 tests pass.

- [ ] **Step 5: Commit**

```bash
git add src/core/latest.h src/core/shadow_frame.h tests/core/latest_test.cpp tests/core/CMakeLists.txt
git commit -m "core: newest-value mailbox and the B1 shadow frame"
```

---

### Task 6: Presenter overlay pass

**Files:**
- Modify: `src/platform/presenter_d3d11.h`, `src/platform/presenter_d3d11.cpp`
- Test: `tests/platform/presenter_d3d11_test.cpp`

**Interfaces:**
- Produces:
  ```cpp
  bool hr::platform::D3DPresenter::set_overlay(const std::uint32_t* bgra, core::Size size);
  void hr::platform::D3DPresenter::draw_overlay(core::Rect where, Filter filter);
  ```
  `bgra` is straight-alpha 0xAARRGGBB; `draw_overlay` blends it over what `render()` drew, scaled like the frame; no-op without an overlay.

- [ ] **Step 1: Write the failing tests**

Append to `tests/platform/presenter_d3d11_test.cpp`:
```cpp
TEST_CASE("the overlay is blended over the frame where it is opaque", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const std::vector<std::uint32_t> frame(64 * 48, 0x00102030u);
    // 2x2: opaque red, transparent (colour kept), opaque green, opaque blue
    const std::uint32_t overlay[] = {0xFFFF0000u, 0x00FF0000u, 0xFF00FF00u, 0xFF0000FFu};
    REQUIRE(presenter->set_overlay(overlay, {2, 2}));
    presenter->render(frame.data(), {64, 48}, {0, 0, 64, 48}, D3DPresenter::Filter::Point);
    presenter->draw_overlay({8, 8, 4, 4}, D3DPresenter::Filter::Point);
    const auto pixels = presenter->read_back();
    REQUIRE(pixels.size() == 64u * 48u);
    CHECK(at(pixels, 64, 8, 8) == 0xFF0000u);
    CHECK(at(pixels, 64, 9, 9) == 0xFF0000u);
    CHECK(at(pixels, 64, 10, 8) == 0x102030u); // transparent texel
    CHECK(at(pixels, 64, 8, 10) == 0x00FF00u);
    CHECK(at(pixels, 64, 11, 11) == 0x0000FFu);
    CHECK(at(pixels, 64, 12, 12) == 0x102030u); // outside
    presenter->present(false);
    DestroyWindow(window);
}

TEST_CASE("drawing without an overlay leaves the frame alone", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const std::vector<std::uint32_t> frame(64 * 48, 0x00405060u);
    presenter->render(frame.data(), {64, 48}, {0, 0, 64, 48}, D3DPresenter::Filter::Point);
    presenter->draw_overlay({0, 0, 64, 48}, D3DPresenter::Filter::Point);
    const auto pixels = presenter->read_back();
    CHECK(at(pixels, 64, 5, 5) == 0x405060u);
    DestroyWindow(window);
}

TEST_CASE("an overlay of a new size replaces the old one", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const std::vector<std::uint32_t> frame(64 * 48, 0u);
    const std::uint32_t small[] = {0xFFFF0000u};
    const std::uint32_t big[] = {0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u};
    REQUIRE(presenter->set_overlay(small, {1, 1}));
    REQUIRE(presenter->set_overlay(big, {2, 2}));
    presenter->render(frame.data(), {64, 48}, {0, 0, 64, 48}, D3DPresenter::Filter::Point);
    presenter->draw_overlay({0, 0, 4, 4}, D3DPresenter::Filter::Point);
    const auto pixels = presenter->read_back();
    CHECK(at(pixels, 64, 3, 3) == 0x00FF00u);
    DestroyWindow(window);
}
```
(`at` is the helper already defined in the test file's second anonymous namespace; the new cases go after it.)

- [ ] **Step 2: Run tests to verify they fail**

Run: `cmake --build --preset x86-debug --target platform_tests`
Expected: compile error, no member `set_overlay`.

- [ ] **Step 3: Write minimal implementation**

`src/platform/presenter_d3d11.h`, public, after `present`:
```cpp
    // Stage B1 (spec 2026-10-07 §5.3): a straight-alpha 0xAARRGGBB picture blended over what
    // render() drew, scaled into `where` like the frame. set_overlay uploads it (false if the
    // texture cannot be made); draw_overlay does nothing until one is set.
    bool set_overlay(const std::uint32_t* bgra, core::Size size);
    void draw_overlay(core::Rect where, Filter filter);
```
private, replace `bool ensure_frame_texture(core::Size frame);` with
```cpp
    // (Re)creates `texture`/`view` as a size-sized BGRA texture unless `current` already is it.
    bool ensure_texture(Microsoft::WRL::ComPtr<ID3D11Texture2D>& texture,
                        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>& view,
                        core::Size& current, core::Size size);
```
and add members:
```cpp
    Microsoft::WRL::ComPtr<ID3D11Texture2D> overlay_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> overlay_view_;
    core::Size overlay_size_{};
```
`src/platform/presenter_d3d11.cpp`: replace `ensure_frame_texture` with
```cpp
bool D3DPresenter::ensure_texture(ComPtr<ID3D11Texture2D>& texture,
                                  ComPtr<ID3D11ShaderResourceView>& view, core::Size& current,
                                  core::Size size) {
    if (texture && size == current) {
        return true;
    }
    texture.Reset();
    view.Reset();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(size.width);
    desc.Height = static_cast<UINT>(size.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &texture)) ||
        FAILED(device_->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
        texture.Reset();
        view.Reset();
        return false;
    }
    current = size;
    return true;
}
```
in `render()` replace `!ensure_frame_texture(frame)` with `!ensure_texture(frame_, frame_view_, frame_size_, frame)`, and add:
```cpp
bool D3DPresenter::set_overlay(const std::uint32_t* bgra, core::Size size) {
    if (bgra == nullptr || size.width <= 0 || size.height <= 0 ||
        !ensure_texture(overlay_, overlay_view_, overlay_size_, size)) {
        return false;
    }
    context_->UpdateSubresource(overlay_.Get(), 0, nullptr, bgra,
                                static_cast<UINT>(size.width) * 4, 0);
    return true;
}

void D3DPresenter::draw_overlay(core::Rect where, Filter filter) {
    if (!overlay_view_ || where.width <= 0 || where.height <= 0) {
        return;
    }
    const core::Size prescale = core::sharp_prescale(overlay_size_, where);
    const Constants constants{
        {static_cast<float>(overlay_size_.width), static_cast<float>(overlay_size_.height)},
        {static_cast<float>(prescale.width), static_cast<float>(prescale.height)},
        filter == Filter::Sharp ? 1u : 0u,
        {}};
    context_->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);
    draw_quad(overlay_view_.Get(), where, alpha_blend_.Get());
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run: `cmake --build --preset x86-debug --target platform_tests; ctest --preset x86-debug -L presenter_d3d11`
Expected: all presenter tests pass (old and 3 new).

- [ ] **Step 5: Commit**

```bash
git add src/platform/presenter_d3d11.h src/platform/presenter_d3d11.cpp tests/platform/presenter_d3d11_test.cpp
git commit -m "platform: presenter overlay pass (straight alpha, scaled like the frame)"
```

---

### Task 7: `wing32/rl_shadow` — the production second render

Not unit-testable (it calls into the running game). Replaces the spike's body; keeps its proven calls.

**Files:**
- Modify: `src/wing32/rl_shadow.h`, `src/wing32/rl_shadow.cpp`
- Delete: `src/core/indexed_frame.h`, `src/core/indexed_frame.cpp`, `tests/core/indexed_frame_test.cpp` (and their CMake entries)

**Interfaces:**
- Consumes: `core::gamef_layout`, `platform::sha256_file`, `core::RenderBudget`, `core::Latest`, `core::ShadowFrame`, `core::Palette`, `a2::active()`, `settings().config.render_scale`.
- Produces:
  ```cpp
  namespace hr::wing32::rl_shadow {
  void on_window_created(HWND window, const std::string& class_name); // game thread
  void before_blit(HDC dest, HDC src);                                 // game thread
  void set_factor(int factor);                                         // presenter thread
  bool take_frame(std::uint64_t& seen, core::ShadowFrame& out);        // presenter thread
  struct Stats { int frames = 0; double total_ms = 0.0; double max_ms = 0.0; int factor = 0; };
  Stats take_stats();                                                  // presenter thread
  double now_s();                                                      // steady clock seconds
  }
  ```

- [ ] **Step 1: Replace `src/wing32/rl_shadow.h`**

```cpp
#pragma once

#include "core/shadow_frame.h"

#include <windows.h>

#include <cstdint>
#include <string>

// Stage B1 (spec 2026-10-07 §5.1): when the game blits its battle 3D view, renders the same scene
// a second time through our own Reality Lab device at the factor the presenter asks for, and
// publishes the frame for the presenter's overlay.
namespace hr::wing32::rl_shadow {

// Game thread, after every successful CreateWindowExA in a game module: remembers the
// Map3DClass window and subclasses it so our device goes before the game's own WM_DESTROY handler.
void on_window_created(HWND window, const std::string& class_name);

// Game thread, from WinGBitBlt / WinGStretchBlt before the original.
void before_blit(HDC dest, HDC src);

// Presenter thread: the factor to render at (1 or less = do not render).
void set_factor(int factor);

// Presenter thread: copies the newest frame into `out` if newer than `seen`.
bool take_frame(std::uint64_t& seen, core::ShadowFrame& out);

// Presenter thread: render times since the last call (for the 10 s performance line).
struct Stats {
    int frames = 0;
    double total_ms = 0.0;
    double max_ms = 0.0;
    int factor = 0;
};
Stats take_stats();

// Steady clock in seconds; ShadowFrame::time_s uses it.
double now_s();

} // namespace hr::wing32::rl_shadow
```

- [ ] **Step 2: Replace `src/wing32/rl_shadow.cpp`**

```cpp
#include "wing32/rl_shadow.h"

#include "core/gamef_layout.h"
#include "core/latest.h"
#include "core/render_scale.h"
#include "platform/log.h"
#include "platform/sha256.h"
#include "platform/window_subclass.h"
#include "wing32/a2.h"
#include "wing32/runtime.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
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

struct Device {
    std::array<std::uint32_t, 15> desc{};
    std::vector<std::uint8_t> bits;
    int width = 0; // 1× size of the view
    int height = 0;
    int factor = 0;
    int stride = 0; // align4(width)·factor
    u32 device = 0;
    u32 viewport = 0;
};

enum class State { Unresolved, Off, On };

// Game thread only.
State g_state = State::Unresolved;
std::optional<core::GamefLayout> g_layout;
std::uintptr_t g_base = 0;
HWND g_view = nullptr;
std::unique_ptr<platform::Subclass> g_subclass;
std::unique_ptr<Device> g_device;
core::RenderBudget g_budget;
bool g_logged_half = false;

// Shared with the presenter thread.
std::atomic<int> g_factor{1};
core::Latest<core::ShadowFrame> g_frames;
std::mutex g_stats_mutex;
Stats g_stats;

template <typename F> F fn(u32 rva) { return reinterpret_cast<F>(g_base + rva); }

template <typename T> T read(std::uintptr_t at) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof value);
    return value;
}

int align4(int v) { return ((v + 3) / 4) * 4; }

// The ramp palette is shared by all devices (B1.0), so no colour ever arrives here.
void __cdecl on_palette(void*, void*, int, int, int, int) {}

void off(const std::string& why) {
    g_state = State::Off;
    platform::log("b1: off, " + why);
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
    if (!g_layout) {
        off("unknown GAMEF.DLL build (sha256 " + hash.value_or("?") + ")");
        return;
    }
    g_base = reinterpret_cast<std::uintptr_t>(gamef);
    g_state = State::On;
    platform::log("b1: known GAMEF.DLL build, sharp 3D view available");
}

// SEH boundary around calls into the game: no objects with destructors in this function.
template <typename F> bool seh(F&& body) {
    __try {
        body();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void destroy_device() {
    if (!g_device) {
        return;
    }
    const auto& l = *g_layout;
    Device* d = g_device.get();
    const bool ok = seh([&] {
        if (d->viewport != 0) {
            fn<Handle>(l.destroy_object)(d->viewport);
        }
        if (d->device != 0) {
            fn<Handle>(l.destroy_object)(d->device);
        }
    });
    if (!ok) {
        off("exception while destroying the device");
    }
    g_device.reset();
}

bool create_device(std::uintptr_t w, int width, int height, int factor) {
    const auto& l = *g_layout;
    auto device = std::make_unique<Device>();
    device->width = width;
    device->height = height;
    device->factor = factor;
    device->stride = align4(width) * factor;
    device->bits.assign(static_cast<std::size_t>(device->stride) *
                            static_cast<std::size_t>(height * factor),
                        0);
    std::memcpy(device->desc.data(), reinterpret_cast<const void*>(w + l.w_device_desc),
                sizeof device->desc);
    device->desc[0] = static_cast<std::uint32_t>(device->stride);
    device->desc[1] = static_cast<std::uint32_t>(height * factor);
    device->desc[6] = static_cast<std::uint32_t>(device->stride);
    device->desc[7] =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(device->bits.data()));
    const u32 camera = read<u32>(w + l.w_camera);
    Device* d = device.get();
    const bool ok = seh([&] {
        d->device = fn<CreateDevice>(l.create_device)(d->desc.data(), 0, 0, 0, &on_palette, d);
        if (d->device == 0) {
            return;
        }
        fn<HandleInt>(l.set_device_mode)(d->device, 0);
        fn<HandleInt>(l.set_device_dither)(d->device, 0);
        fn<HandleInt>(l.set_device_shades)(d->device, 8);
        d->viewport = fn<CreateViewport>(l.create_viewport)(d->device, camera, 0, 0, d->stride,
                                                            height * factor);
        if (d->viewport != 0) {
            fn<ConfigureViewport>(l.configure_viewport)(d->viewport, 0, 0, d->stride,
                                                        height * factor);
        }
    });
    g_device = std::move(device);
    if (!ok) {
        off("exception while creating the device");
        return false;
    }
    if (d->device == 0 || d->viewport == 0) {
        destroy_device();
        off("device or viewport creation failed");
        return false;
    }
    platform::log("b1: device " + std::to_string(d->stride) + "x" +
                  std::to_string(height * factor) + " (x" + std::to_string(factor) + ")");
    return true;
}

// Copies the game viewport's projection settings to ours (game thread, inside seh()).
bool copy_viewport(std::uintptr_t w) {
    const auto& l = *g_layout;
    int object = 0;
    if (fn<Lookup>(l.lookup_object)(&object, read<u32>(w + l.w_viewport),
                                    reinterpret_cast<const void*>(g_base + l.viewport_type)) != 0 ||
        object == 0) {
        return false;
    }
    const auto o = static_cast<std::uintptr_t>(object);
    const u32 vp = g_device->viewport;
    fn<SetPlane>(l.set_viewport_plane)(vp, read<float>(o + l.vp_plane_a),
                                       read<float>(o + l.vp_plane_b),
                                       read<float>(o + l.vp_plane_c),
                                       read<float>(o + l.vp_plane_d));
    fn<SetFloat>(l.set_viewport_back)(vp, read<float>(o + l.vp_back));
    fn<HandleInt>(l.set_viewport_flag_d8)(vp, read<int>(o + l.vp_flag_d8));
    fn<HandleInt>(l.set_viewport_flag_44)(vp, read<int>(o + l.vp_flag_44));
    return true;
}

void publish(std::uintptr_t w, HDC src) {
    const auto& l = *g_layout;
    const Device& d = *g_device;
    core::ShadowFrame frame;
    frame.hires = d.bits;
    frame.stride = d.stride;
    frame.size = {d.width, d.height};
    frame.factor = d.factor;
    const auto* game_bits =
        reinterpret_cast<const std::uint8_t*>(read<std::uintptr_t>(w + l.w_bits));
    const int game_stride = static_cast<int>(read<std::uint32_t>(w + l.w_device_desc));
    frame.game.resize(static_cast<std::size_t>(d.width) * d.height);
    for (int y = 0; y < d.height; ++y) {
        std::memcpy(frame.game.data() + static_cast<std::size_t>(y) * d.width,
                    game_bits + static_cast<std::ptrdiff_t>(y) * game_stride,
                    static_cast<std::size_t>(d.width));
    }
    std::array<RGBQUAD, 256> quads{};
    GetDIBColorTable(src, 0, 256, quads.data());
    for (std::size_t i = 0; i < 256; ++i) {
        frame.palette[i] = (static_cast<std::uint32_t>(quads[i].rgbRed) << 16) |
                           (static_cast<std::uint32_t>(quads[i].rgbGreen) << 8) |
                           quads[i].rgbBlue;
    }
    frame.view_window = reinterpret_cast<std::uintptr_t>(g_view);
    frame.time_s = now_s();
    g_frames.publish(std::move(frame));
}

void render(HDC src) {
    const auto& l = *g_layout;
    const auto w = static_cast<std::uintptr_t>(GetWindowLongA(GetParent(g_view), 4));
    if (w == 0) {
        return;
    }
    if (read<int>(w + l.w_divisor) != 1) {
        if (!g_logged_half) {
            g_logged_half = true;
            platform::log("b1: half detail, sharp 3D view paused");
        }
        return;
    }
    const int factor = std::min(g_factor.load(), g_budget.cap());
    if (factor <= 1) {
        return;
    }
    const int width = read<int>(w + l.w_active_width);
    const int height = read<int>(w + l.w_active_height);
    if (!g_device || g_device->width != width || g_device->height != height ||
        g_device->factor != factor) {
        destroy_device();
        if (g_state != State::On || !create_device(w, width, height, factor)) {
            return;
        }
    }
    const u32 scene = read<u32>(w + l.w_scene);
    const auto t0 = std::chrono::steady_clock::now();
    bool copied = false;
    Device* d = g_device.get();
    const bool ok = seh([&] {
        copied = copy_viewport(w);
        if (copied) {
            fn<Handle>(l.clear_viewport)(d->viewport);
            fn<Render>(l.render_viewport)(d->viewport, scene);
        }
    });
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!ok) {
        off("exception while rendering");
        return;
    }
    if (!copied) {
        return;
    }
    const int cap_before = g_budget.cap();
    g_budget.add(now_s(), ms, factor);
    if (g_budget.cap() != cap_before) {
        platform::log("b1: second render too slow, factor limited to " +
                      std::to_string(g_budget.cap()));
    }
    {
        const std::lock_guard lock(g_stats_mutex);
        ++g_stats.frames;
        g_stats.total_ms += ms;
        g_stats.max_ms = ms > g_stats.max_ms ? ms : g_stats.max_ms;
        g_stats.factor = factor;
    }
    publish(w, src);
}

} // namespace

double now_s() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void on_window_created(HWND window, const std::string& class_name) {
    if (settings().config.render_scale == 1 || class_name != "Map3DClass") {
        return;
    }
    if (g_state == State::Unresolved) {
        resolve();
    }
    if (g_state != State::On) {
        return;
    }
    g_view = window;
    g_subclass = std::make_unique<platform::Subclass>(
        window, [](HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, WNDPROC original) {
            if (msg == WM_DESTROY) {
                destroy_device();
                g_view = nullptr;
            }
            return CallWindowProcA(original, hwnd, msg, wparam, lparam);
        });
}

void before_blit(HDC dest, HDC src) {
    if (g_state != State::On || g_view == nullptr || !a2::active() ||
        WindowFromDC(dest) != g_view) {
        return;
    }
    render(src);
}

void set_factor(int factor) { g_factor = factor; }

bool take_frame(std::uint64_t& seen, core::ShadowFrame& out) {
    return g_frames.take_newer(seen, out);
}

Stats take_stats() {
    const std::lock_guard lock(g_stats_mutex);
    const Stats stats = g_stats;
    g_stats = Stats{};
    return stats;
}

} // namespace hr::wing32::rl_shadow
```
Add `#include <algorithm>` for `std::min`. Delete `src/core/indexed_frame.{h,cpp}` and `tests/core/indexed_frame_test.cpp` and remove them from both CMake lists. Remove the spike key: delete `Config::b1_spike` (`src/core/config.h`), the `debug.b1_spike` field (`src/core/config.cpp`) and the three `b1_spike` test cases (`tests/core/config_test.cpp`).

- [ ] **Step 3: Build and run all tests**

Run: `. ./tools/vsenv.ps1; cmake --build --preset x86-debug; ctest --preset x86-debug`
Expected: builds without warnings; all tests pass.

- [ ] **Step 4: Commit**

```bash
git add -A src/wing32/rl_shadow.h src/wing32/rl_shadow.cpp src/core tests/core
git commit -m "wing32: B1 second render publishes frames for the presenter (spike removed)"
```

---

### Task 8: The presenter draws the sharp 3D view

**Files:**
- Modify: `src/wing32/a2.cpp`

**Interfaces:**
- Consumes: `rl_shadow::set_factor`, `rl_shadow::take_frame`, `rl_shadow::take_stats`, `rl_shadow::now_s`, `core::render_factor`, `core::to_bgra`, `core::overlay_mask`, `core::compose_overlay`, `core::map_rect`, `D3DPresenter::set_overlay`, `D3DPresenter::draw_overlay`.

- [ ] **Step 1: Add the overlay helper to `a2.cpp`**

Includes: `core/overlay.h`, `core/render_scale.h`, `core/shadow_frame.h`, `wing32/rl_shadow.h`. In the anonymous namespace (above the presenter thread function):
```cpp
// Stage B1 (spec 2026-10-07 §5.3): draws the newest second render of the battle's 3D view over
// the captured picture where the capture shows the game's own render. Presenter thread.
class BattleOverlay {
  public:
    void draw(platform::D3DPresenter& d3d, HWND glue, core::Rect content, core::Rect dest,
              const std::uint32_t* captured, core::Size captured_size,
              platform::D3DPresenter::Filter filter) {
        if (rl_shadow::take_frame(seen_, frame_)) {
            game_ = core::to_bgra(frame_.game.data(), frame_.size.width, frame_.size,
                                  frame_.palette);
            dirty_ = true;
            have_ = true;
        }
        const auto view = reinterpret_cast<HWND>(frame_.view_window);
        if (!have_ || rl_shadow::now_s() - frame_.time_s > 0.25 || !IsWindow(view) ||
            !IsWindowVisible(view)) {
            return;
        }
        RECT r{};
        GetWindowRect(view, &r);
        MapWindowPoints(HWND_DESKTOP, glue, reinterpret_cast<POINT*>(&r), 2);
        const core::Rect in_content{r.left - content.x, r.top - content.y, frame_.size.width,
                                    frame_.size.height};
        auto mask = core::overlay_mask(captured, captured_size, in_content, game_);
        if (dirty_ || mask != mask_) {
            mask_ = std::move(mask);
            const auto pixels = core::compose_overlay(frame_.hires.data(), frame_.stride,
                                                      frame_.size, frame_.factor, frame_.palette,
                                                      mask_);
            if (!d3d.set_overlay(pixels.data(), {frame_.size.width * frame_.factor,
                                                 frame_.size.height * frame_.factor})) {
                return;
            }
            dirty_ = false;
        }
        d3d.draw_overlay(core::map_rect(in_content, captured_size, dest), filter);
    }

  private:
    std::uint64_t seen_ = 0;
    core::ShadowFrame frame_;
    std::vector<std::uint32_t> game_;
    std::vector<std::uint8_t> mask_;
    bool dirty_ = false;
    bool have_ = false;
};
```

- [ ] **Step 2: Use it in the frame**

In the presenter thread, next to `core::FrameStats stats;`, add `BattleOverlay battle_overlay;`. In the `frame` lambda, after `input.set_dest(dest);`:
```cpp
            rl_shadow::set_factor(config.render_scale == 1
                                      ? 1
                                      : core::render_factor(config.render_scale,
                                                            {content.width, content.height}, dest));
```
after `d3d->render(capture.pixels(), capture.size(), dest, filter);`:
```cpp
            battle_overlay.draw(*d3d, glue, content, dest, capture.pixels(), capture.size(),
                                filter);
```
and where the 10 s line is logged (`platform::log("presenter: " + stats.take_line(10));`), add after it:
```cpp
                const auto shadow = rl_shadow::take_stats();
                if (shadow.frames > 0) {
                    char line[128];
                    std::snprintf(line, sizeof line,
                                  "presenter: 3D view x%d, %d renders, %.2f ms avg, %.2f ms max",
                                  shadow.factor, shadow.frames, shadow.total_ms / shadow.frames,
                                  shadow.max_ms);
                    platform::log(line);
                }
```

- [ ] **Step 3: Build, test, format, install**

Run: `. ./tools/vsenv.ps1; cmake --build --preset x86-debug; ctest --preset x86-debug; cmake --build --preset x86-release; clang-format -i src/wing32/a2.cpp src/wing32/rl_shadow.cpp; clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.h')`, then install the release build.
Expected: no warnings, all tests pass, format clean.

- [ ] **Step 4: Commit, push, PR**

```bash
git add src/wing32/a2.cpp src/wing32/rl_shadow.cpp
git commit -m "wing32: the presenter shows the sharp battle 3D view (B1.1)"
git push -u origin stage-b1-1
```
Open a draft PR (base `stage-b1` if PR #13 is still open, else `main`), bind it, Auto-fix on.

---

### Task 9: In-game check, smoke test, exit notes

**Files:**
- Modify: `docs/smoke-test.md`
- Create: `docs/re-notes/b1-1-exit.md`
- Modify: `docs/superpowers/specs/2026-10-07-stage-b-battle-view-design.md` (§9 B1.1 result line)

- [ ] **Step 1: Smoke-test items**

Append to `docs/smoke-test.md`:
```markdown
### Stage B1 sharp 3D view (from B1.1)
- [ ] `render_scale = auto` at 1280×960: the battle 3D view is visibly sharper than the rest of the
      picture (smooth unit outlines and edges); the log shows `3D view x2`.
- [ ] Fullscreen at 1920×1080 (`scale = sharp`): `3D view x3`; still sharp; no stutter.
- [ ] Whatever the game draws over the 3D view (unit info, menus, dialogs, the exit question) is
      shown normally, not hidden behind the sharp view.
- [ ] Camera movement and unit movement stay smooth; the sharp view is never behind the rest of
      the picture.
- [ ] `render_scale = 1`: the 3D view looks exactly like vanilla.
- [ ] `horned-rat.log`: `b1: known GAMEF.DLL build`, `b1: device …`, no `b1: off` line.
```

- [ ] **Step 2: Runs with the user**

Ask the user to run, in turn (each ini change needs their OK; restore afterwards):
1. Default (`render_scale = auto`, a 1280×960 window): a battle with camera and unit movement; open something over the 3D view (a unit's info, the menu, the exit question).
2. Fullscreen (Alt+Enter): the same, briefly.
3. `render_scale = 1`: the 3D view looks as before.
Read `horned-rat.log` after each: `b1:` lines, the `3D view xK` performance lines, no `b1: off`.

- [ ] **Step 3: Exit notes and spec**

`docs/re-notes/b1-1-exit.md`: setup, runs and what the user saw, log figures (render ms per factor, presenter frame times), problems found and fixed, open items (filed as issues). Add a result line to the spec's §9 B1.1.

- [ ] **Step 4: Commit, push, mark ready**

```bash
git add docs/smoke-test.md docs/re-notes/b1-1-exit.md docs/superpowers/specs/2026-10-07-stage-b-battle-view-design.md
git commit -m "docs: B1.1 results and smoke-test items"
```
Check the PR is open, push, `gh pr ready`.
