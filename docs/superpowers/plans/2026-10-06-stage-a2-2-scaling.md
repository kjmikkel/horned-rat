# Stage A2.2: Scaling Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show the game in a resizable presenter window (or borderless full screen) at any size, scaled with the configured `integer` / `sharp` / `nearest-fit` mode and aspect, with Alt+Enter toggling borderless and pixel-exact mouse mapping.

**Architecture:** `core` gains pure scaling maths (destination rect per scale × aspect, sharp-bilinear prescale, fitting a window to the work area, Alt+Enter detection). `platform/presenter_d3d11` replaces A2.1's 1:1 copy with a shader pass (point or sharp-bilinear) drawn into the destination viewport, and learns to resize its swap chain. `platform/presenter_window` becomes resizable, can go borderless, keeps redrawing during the modal size/move loop, and is per-monitor DPI aware so Windows never blurs it. `wing32/a2.cpp` wires the config into one per-frame function.

**Tech Stack:** C++20, MSVC x86 (`/W4 /WX`, static CRT), CMake/Ninja presets, Catch2 v3.8.1, Direct3D 11 + DXGI flip model, HLSL compiled at runtime with `D3DCompile` (`d3dcompiler_47.dll`, part of Windows 10+), WARP for tests.

**Spec:** `docs/superpowers/specs/2026-10-05-stage-a2-presenter-design.md` (milestone A2.2, §4, §5.1, §7, §8), scale/aspect definitions from `docs/superpowers/specs/2026-10-05-stage-a-display-wrapper-design.md` §5.3/§6.1. A2.1 results: `docs/re-notes/a2-1-exit.md`.

## Global Constraints

- Layering: `core` → `platform` → `wing32`; `core` includes no Windows headers. Win32 values used in `core` are mirrored in `core::win` and `static_assert`ed in a platform test.
- Red/green TDD for every behaviour: write the test, see it fail for the right reason, then implement. `wing32` glue is thin and verified by the manual smoke test.
- C++20, `/W4 /WX`; clang-format 19.1.7 (LLVM base, 4 spaces, 100 columns, `PointerAlignment: Left`). CI runs a format check — run `clang-format -i` on touched files before committing.
- Build and test (PowerShell, repo root): `. ./tools/vsenv.ps1; cmake --build --preset x86-debug; ctest --preset x86-debug`. Run one test binary with a tag: `./build/x86-debug/bin/core_tests.exe "[scaling]"`.
- Never commit game files, logs, captures, probe CSVs or decompiled code. Ask the user before installing into or changing the GOG game folder.
- "Never worse than vanilla" (spec §7): every presenter failure unhides the game window and leaves a playable game.
- Config keys already exist (spec §6.1): `mode = windowed | borderless`, `window_size = 1280x960` (initial client size), `scale = integer | sharp | nearest-fit` (default `sharp`), `aspect = 4:3 | stretch`, `vsync`.
- Scale modes (Stage A spec §5.3): `integer` = largest whole multiple of the picture that fits, centred, bars elsewhere; `sharp` = integer prescale (nearest) then bilinear to the largest aspect-correct fit; `nearest-fit` = nearest neighbour to the largest aspect-correct fit; `aspect = stretch` fills the client area ignoring 4:3.
- Tests must not assume a big screen: GitHub's Windows runners have a 1024×768 desktop.
- Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`. Work on branch `stage-a2-2`; PR bodies end with `🤖 Generated with [Claude Code](https://claude.com/claude-code)`; turn Auto-fix on for the PR.

## Decisions taken in this plan (record in the spec at Task 9)

1. **`integer` + `aspect = stretch`** uses the largest whole factor **per axis** (e.g. 3×2 on 1920×1080): it ignores 4:3 as `stretch` asks while every game pixel stays a whole number of screen pixels.
2. **Shaders compile at runtime** (`D3DCompile`). If compilation fails, `D3DPresenter::create` fails, so the presenter is disabled and the game unhidden (spec §7) — there is no separate 1:1 fallback path.
3. **Live redraw while resizing:** Windows runs a modal loop while the user drags the window frame, which stops the presenter loop. The window starts a 15 ms timer on `WM_ENTERSIZEMOVE` and reports `on_tick()`; `a2` renders a frame on each tick.
4. **Per-monitor DPI awareness for the presenter window only** (not in the A2 spec). The game process is DPI-unaware, so on a 125 %/150 % desktop Windows would bitmap-stretch our window and blur it. The presenter window is created per-monitor aware (V2, else V1); the game's window stays unaware. Code that touches game windows runs in the game's DPI context, code that measures the presenter in the presenter's. The APIs are looked up at runtime so older Windows simply skips this.
5. Sharp-bilinear is the single-pass "sharp-bilinear-simple" formulation: inside each texel's centre region the sample snaps to the texel centre; only a band of width `1/prescale` at texel edges is blended.

## File Structure

| File | Responsibility |
|---|---|
| `src/core/scaling.{h,cpp}` (new) | `dest_rect`, `sharp_prescale`, `fit_inside` |
| `src/core/input_map.{h,cpp}` | + `toggles_borderless` (Alt+Enter press) |
| `src/platform/presenter_d3d11.{h,cpp}` | shader pipeline, `Filter`, `resize`, `size` |
| `src/platform/presenter_window.{h,cpp}` | resizable, placement, min size, `client_size`, tick timer, borderless, Alt+Enter, DPI |
| `src/platform/dpi_context.{h,cpp}` (new) | `current_dpi_context`, `ScopedDpiContext` |
| `src/wing32/a2.cpp` | config wiring, per-frame function, tick, logging, DPI scoping |
| `tests/core/scaling_test.cpp` (new), `tests/core/input_map_test.cpp` | core tests |
| `tests/platform/presenter_d3d11_test.cpp`, `presenter_window_test.cpp`, `dpi_context_test.cpp` (new) | platform tests |
| `docs/smoke-test.md`, `docs/re-notes/a2-2-exit.md` (new), A2 spec | exit |

---

### Task 1: Scaling maths in core

**Files:**
- Create: `src/core/scaling.h`, `src/core/scaling.cpp`, `tests/core/scaling_test.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`

**Interfaces:**
- Consumes: `core::Size`, `core::Rect` (`core/geometry.h`); `core::ScaleMode`, `core::AspectMode` (`core/config.h`); `core::centered_rect(Size client, Size content) -> Rect` (`core/viewport.h`, centres and allows overflow).
- Produces:
  - `core::Rect core::dest_rect(core::Size client, core::Size content, core::ScaleMode scale, core::AspectMode aspect)`
  - `core::Size core::sharp_prescale(core::Size content, core::Rect dest)`
  - `core::Size core::fit_inside(core::Size wanted, core::Size available)`

- [ ] **Step 1: Create the branch**

```bash
git switch main
git pull
git switch -c stage-a2-2
```

- [ ] **Step 2: Write the failing tests**

`tests/core/scaling_test.cpp`:

```cpp
#include "core/scaling.h"

#include <catch2/catch_test_macros.hpp>

using namespace hr::core;

namespace {
constexpr Size kGame{640, 480};
}

TEST_CASE("integer scaling uses the largest whole factor that fits, centred", "[scaling]") {
    CHECK(dest_rect({640, 480}, kGame, ScaleMode::Integer, AspectMode::Keep4x3) ==
          Rect{0, 0, 640, 480});
    CHECK(dest_rect({1920, 1080}, kGame, ScaleMode::Integer, AspectMode::Keep4x3) ==
          Rect{320, 60, 1280, 960});
    CHECK(dest_rect({2560, 1440}, kGame, ScaleMode::Integer, AspectMode::Keep4x3) ==
          Rect{320, 0, 1920, 1440});
    CHECK(dest_rect({1279, 959}, kGame, ScaleMode::Integer, AspectMode::Keep4x3) ==
          Rect{319, 239, 640, 480});
}

TEST_CASE("integer scaling never goes below 1x and overflows a small client", "[scaling]") {
    CHECK(dest_rect({600, 400}, kGame, ScaleMode::Integer, AspectMode::Keep4x3) ==
          Rect{-20, -40, 640, 480});
}

TEST_CASE("integer scaling with stretch uses a whole factor per axis", "[scaling]") {
    CHECK(dest_rect({1920, 1080}, kGame, ScaleMode::Integer, AspectMode::Stretch) ==
          Rect{0, 60, 1920, 960});
}

TEST_CASE("sharp and nearest-fit fill the client with the picture's aspect", "[scaling]") {
    for (const ScaleMode mode : {ScaleMode::Sharp, ScaleMode::NearestFit}) {
        CHECK(dest_rect({1920, 1080}, kGame, mode, AspectMode::Keep4x3) ==
              Rect{240, 0, 1440, 1080});
        CHECK(dest_rect({1280, 1024}, kGame, mode, AspectMode::Keep4x3) ==
              Rect{0, 32, 1280, 960});
        CHECK(dest_rect({1280, 960}, kGame, mode, AspectMode::Keep4x3) == Rect{0, 0, 1280, 960});
        CHECK(dest_rect({1001, 700}, kGame, mode, AspectMode::Keep4x3) ==
              Rect{34, 0, 933, 700});
        CHECK(dest_rect({320, 240}, kGame, mode, AspectMode::Keep4x3) == Rect{0, 0, 320, 240});
    }
}

TEST_CASE("sharp and nearest-fit with stretch fill the whole client", "[scaling]") {
    CHECK(dest_rect({1920, 1080}, kGame, ScaleMode::Sharp, AspectMode::Stretch) ==
          Rect{0, 0, 1920, 1080});
    CHECK(dest_rect({1001, 700}, kGame, ScaleMode::NearestFit, AspectMode::Stretch) ==
          Rect{0, 0, 1001, 700});
}

TEST_CASE("the aspect is the content's own, also for the full-client fallback", "[scaling]") {
    CHECK(dest_rect({1300, 982}, {650, 491}, ScaleMode::Sharp, AspectMode::Keep4x3) ==
          Rect{0, 0, 1300, 982});
}

TEST_CASE("an empty client or picture gives an empty destination", "[scaling]") {
    CHECK(dest_rect({0, 0}, kGame, ScaleMode::Sharp, AspectMode::Keep4x3) == Rect{0, 0, 0, 0});
    CHECK(dest_rect({800, 600}, {0, 0}, ScaleMode::Integer, AspectMode::Keep4x3) ==
          Rect{0, 0, 0, 0});
}

TEST_CASE("the sharp prescale is the whole part of the scale per axis, at least 1",
          "[scaling]") {
    CHECK(sharp_prescale(kGame, {240, 0, 1440, 1080}) == Size{2, 2});
    CHECK(sharp_prescale(kGame, {0, 0, 1280, 960}) == Size{2, 2});
    CHECK(sharp_prescale(kGame, {0, 0, 1920, 960}) == Size{3, 2});
    CHECK(sharp_prescale(kGame, {0, 0, 600, 450}) == Size{1, 1});
    CHECK(sharp_prescale({0, 0}, {0, 0, 600, 450}) == Size{1, 1});
}

TEST_CASE("fit_inside keeps sizes that fit and shrinks others keeping their aspect",
          "[scaling]") {
    CHECK(fit_inside({1280, 960}, {1920, 1040}) == Size{1280, 960});
    CHECK(fit_inside({1280, 960}, {1366, 728}) == Size{970, 728});
    CHECK(fit_inside({2000, 1000}, {1000, 1000}) == Size{1000, 500});
    CHECK(fit_inside({1280, 960}, {1280, 960}) == Size{1280, 960});
}
```

Add `scaling_test.cpp` to the `add_executable(core_tests ...)` list in `tests/core/CMakeLists.txt`, and create an empty-bodied header so the test compiles but fails to link:

`src/core/scaling.h`:

```cpp
#pragma once

#include "core/config.h"
#include "core/geometry.h"

namespace hr::core {

// Where the picture goes in the presenter's client area (Stage A spec §5.3):
// - integer: the largest whole multiple that fits (with aspect = stretch, per axis), at least 1x,
//   centred; it overflows a client smaller than the picture.
// - sharp / nearest-fit: the largest fit with the picture's own aspect, centred; with
//   aspect = stretch, the whole client.
// An empty client or picture gives an empty rect.
Rect dest_rect(Size client, Size content, ScaleMode scale, AspectMode aspect);

// Whole-number prescale for sharp-bilinear: the whole part of dest / content per axis, at least 1.
Size sharp_prescale(Size content, Rect dest);

// `wanted` shrunk, keeping its aspect, until it fits `available`; unchanged when it fits.
Size fit_inside(Size wanted, Size available);

} // namespace hr::core
```

- [ ] **Step 3: Run the tests to verify they fail**

Run: `. ./tools/vsenv.ps1; cmake --build --preset x86-debug`
Expected: link error `unresolved external symbol ... dest_rect` (and `sharp_prescale`, `fit_inside`).

- [ ] **Step 4: Implement**

`src/core/scaling.cpp`:

```cpp
#include "core/scaling.h"

#include "core/viewport.h"

#include <algorithm>

namespace hr::core {

Rect dest_rect(Size client, Size content, ScaleMode scale, AspectMode aspect) {
    if (client.width <= 0 || client.height <= 0 || content.width <= 0 || content.height <= 0) {
        return {0, 0, 0, 0};
    }
    if (scale == ScaleMode::Integer) {
        int fx = std::max(1, client.width / content.width);
        int fy = std::max(1, client.height / content.height);
        if (aspect == AspectMode::Keep4x3) {
            fx = fy = std::min(fx, fy);
        }
        return centered_rect(client, {content.width * fx, content.height * fy});
    }
    if (aspect == AspectMode::Stretch) {
        return {0, 0, client.width, client.height};
    }
    // Width-limited when client.w / client.h <= content.w / content.h (compared without division).
    const long long width_limited = static_cast<long long>(client.width) * content.height;
    const long long height_limited = static_cast<long long>(client.height) * content.width;
    if (width_limited <= height_limited) {
        const int height = static_cast<int>(width_limited / content.width);
        return centered_rect(client, {client.width, height});
    }
    const int width = static_cast<int>(height_limited / content.height);
    return centered_rect(client, {width, client.height});
}

Size sharp_prescale(Size content, Rect dest) {
    if (content.width <= 0 || content.height <= 0) {
        return {1, 1};
    }
    return {std::max(1, dest.width / content.width), std::max(1, dest.height / content.height)};
}

Size fit_inside(Size wanted, Size available) {
    if (wanted.width <= available.width && wanted.height <= available.height) {
        return wanted;
    }
    if (wanted.width <= 0 || wanted.height <= 0) {
        return {std::min(wanted.width, available.width), std::min(wanted.height, available.height)};
    }
    const long long width_limited = static_cast<long long>(available.width) * wanted.height;
    const long long height_limited = static_cast<long long>(available.height) * wanted.width;
    if (width_limited <= height_limited) {
        return {available.width, static_cast<int>(width_limited / wanted.width)};
    }
    return {static_cast<int>(height_limited / wanted.height), available.height};
}

} // namespace hr::core
```

Add `scaling.cpp` to `add_library(hr_core STATIC ...)` in `src/core/CMakeLists.txt`.

Check the expected values against the code: `{1001, 700}`: 1001·480 = 480480 > 700·640 = 448000 → height-limited, width = 448000 / 480 = 933, x = (1001 − 933) / 2 = 34 ✓. `{1366, 728}` fit of 1280×960: 1366·960 = 1311360 > 728·1280 = 931840 → width = 931840 / 960 = 970 ✓.

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/core_tests.exe "[scaling]"`
Expected: `All tests passed` (9 test cases). Then `ctest --preset x86-debug`: all pass.

- [ ] **Step 6: Commit, push and open the PR**

```bash
clang-format -i src/core/scaling.h src/core/scaling.cpp tests/core/scaling_test.cpp
git add src/core tests/core
git commit -m "core: destination rect, sharp prescale and window fitting for A2.2" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push -u origin stage-a2-2
gh pr create --draft --base main --title "Stage A2.2: scaling" --body "Implements milestone **A2.2** of \`docs/superpowers/specs/2026-10-05-stage-a2-presenter-design.md\`. Plan: \`docs/superpowers/plans/2026-10-06-stage-a2-2-scaling.md\`. Draft until Task 9.

🤖 Generated with [Claude Code](https://claude.com/claude-code)"
```

Then bind the PR in the app and turn Auto-fix on.

---

### Task 2: Alt+Enter detection in core

**Files:**
- Modify: `src/core/input_map.h`, `src/core/input_map.cpp`, `tests/core/input_map_test.cpp`

**Interfaces:**
- Consumes: `core::win::kWmSysKeyDown`, `core::win::kVkReturn`.
- Produces: `bool core::toggles_borderless(unsigned message, std::uintptr_t wparam, std::intptr_t lparam)`.

- [ ] **Step 1: Write the failing test**

Append to `tests/core/input_map_test.cpp`:

```cpp
TEST_CASE("only a fresh Alt+Enter press toggles borderless", "[input_map]") {
    using namespace hr::core::win;
    const std::intptr_t alt = std::intptr_t{1} << 29;
    const std::intptr_t repeat = std::intptr_t{1} << 30;
    CHECK(hr::core::toggles_borderless(kWmSysKeyDown, kVkReturn, alt));
    CHECK_FALSE(hr::core::toggles_borderless(kWmSysKeyDown, kVkReturn, alt | repeat));
    CHECK_FALSE(hr::core::toggles_borderless(kWmSysKeyUp, kVkReturn, alt));
    CHECK_FALSE(hr::core::toggles_borderless(kWmKeyDown, kVkReturn, 0));
    CHECK_FALSE(hr::core::toggles_borderless(kWmSysKeyDown, 'A', alt));
    CHECK_FALSE(hr::core::toggles_borderless(kWmSysKeyDown, kVkReturn, 0));
}
```

Declare it in `src/core/input_map.h` after `classify`:

```cpp
// True for the key-down message of a fresh Alt+Enter press (not auto-repeat): the presenter
// toggles borderless. lparam bit 29 = Alt held, bit 30 = key was already down.
bool toggles_borderless(unsigned message, std::uintptr_t wparam, std::intptr_t lparam);
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build --preset x86-debug`
Expected: link error `unresolved external symbol ... toggles_borderless`.

- [ ] **Step 3: Implement** in `src/core/input_map.cpp` after `classify`:

```cpp
bool toggles_borderless(unsigned message, std::uintptr_t wparam, std::intptr_t lparam) {
    constexpr std::intptr_t kAltDown = std::intptr_t{1} << 29;
    constexpr std::intptr_t kWasDown = std::intptr_t{1} << 30;
    return message == kWmSysKeyDown && wparam == static_cast<std::uintptr_t>(kVkReturn) &&
           (lparam & kAltDown) != 0 && (lparam & kWasDown) == 0;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/core_tests.exe "[input_map]"`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
clang-format -i src/core/input_map.h src/core/input_map.cpp tests/core/input_map_test.cpp
git add src/core tests/core
git commit -m "core: recognise a fresh Alt+Enter press" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Shader pipeline with point scaling, and swap-chain resize

**Files:**
- Modify: `src/platform/presenter_d3d11.h`, `src/platform/presenter_d3d11.cpp`, `src/platform/CMakeLists.txt`, `tests/platform/presenter_d3d11_test.cpp`, `src/wing32/a2.cpp` (call site)

**Interfaces:**
- Consumes: `core::sharp_prescale` (Task 1).
- Produces:
  - `enum class platform::D3DPresenter::Filter { Point, Sharp };`
  - `void D3DPresenter::render(const std::uint32_t* bgra, core::Size frame, core::Rect dest, Filter filter)` — clears to black, draws the frame scaled into `dest` (clipped to the back buffer). Task 3 implements `Point`; `Sharp` behaves like `Point` until Task 4.
  - `bool D3DPresenter::resize(core::Size size)` — no-op for the current or an empty size; false if the swap chain could not be resized (the presenter is then unusable).
  - `core::Size D3DPresenter::size() const`

- [ ] **Step 1: Write the failing tests**

In `tests/platform/presenter_d3d11_test.cpp`, change the three existing `render(...)` calls to pass `D3DPresenter::Filter::Point` as a fourth argument (they keep passing: point sampling at 1:1 is a copy). Then append:

```cpp
namespace {
std::uint32_t at(const std::vector<std::uint32_t>& pixels, int width, int x, int y) {
    return pixels[static_cast<size_t>(y) * width + x] & 0xFFFFFF;
}
} // namespace

TEST_CASE("point scaling at 2x repeats every texel as a 2x2 block", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const auto frame = pattern(16, 8);
    presenter->render(frame.data(), {16, 8}, {4, 6, 32, 16}, D3DPresenter::Filter::Point);
    const auto pixels = presenter->read_back();
    int mismatches = 0;
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 64; ++x) {
            const bool inside = x >= 4 && x < 36 && y >= 6 && y < 22;
            const std::uint32_t expected =
                inside ? frame[static_cast<size_t>((y - 6) / 2) * 16 + (x - 4) / 2] : 0u;
            mismatches += at(pixels, 64, x, y) != expected;
        }
    }
    CHECK(mismatches == 0);
    DestroyWindow(window);
}

TEST_CASE("point scaling at a fractional factor picks the texel under each pixel centre",
          "[presenter_d3d11]") {
    // Odd frame sizes and even destination sizes: no pixel centre falls on a texel edge.
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const auto frame = pattern(15, 7);
    presenter->render(frame.data(), {15, 7}, {3, 2, 40, 20}, D3DPresenter::Filter::Point);
    const auto pixels = presenter->read_back();
    int mismatches = 0;
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 64; ++x) {
            const bool inside = x >= 3 && x < 43 && y >= 2 && y < 22;
            std::uint32_t expected = 0;
            if (inside) {
                const int sx = ((2 * (x - 3) + 1) * 15) / 80;
                const int sy = ((2 * (y - 2) + 1) * 7) / 40;
                expected = frame[static_cast<size_t>(sy) * 15 + sx];
            }
            mismatches += at(pixels, 64, x, y) != expected;
        }
    }
    CHECK(mismatches == 0);
    DestroyWindow(window);
}

TEST_CASE("resizing changes the back buffer size and drawing follows", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    REQUIRE(presenter->resize({100, 80}));
    CHECK(presenter->size() == hr::core::Size{100, 80});
    const auto frame = pattern(16, 8);
    presenter->render(frame.data(), {16, 8}, {70, 60, 16, 8}, D3DPresenter::Filter::Point);
    const auto pixels = presenter->read_back();
    REQUIRE(pixels.size() == 100u * 80u);
    CHECK(at(pixels, 100, 85, 67) == frame[7 * 16 + 15]);
    CHECK(presenter->resize({0, 0}));
    CHECK(presenter->size() == hr::core::Size{100, 80});
    DestroyWindow(window);
}
```

Also add `#include "core/geometry.h"` at the top if not already reachable (it is, through `presenter_d3d11.h`).

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build --preset x86-debug`
Expected: compile errors — `Filter` is not a member of `D3DPresenter`, `resize`/`size` unknown.

- [ ] **Step 3: Implement**

`src/platform/presenter_d3d11.h` — replace the class with:

```cpp
// Shows captured frames in a window through a flip-model D3D11 swap chain, scaled by a shader
// pass into a destination rect (A2 spec §5.1).
class D3DPresenter {
  public:
    enum class Driver { Hardware, Warp };
    enum class Filter { Point, Sharp };

    // Hardware device first (when allowed), then WARP; compiles the shaders. nullptr + `error`
    // if any of it fails.
    static std::unique_ptr<D3DPresenter> create(HWND window, core::Size size, bool allow_hardware,
                                                std::string& error);

    Driver driver() const { return driver_; }
    core::Size size() const { return size_; }

    // Resizes the back buffers. No-op for the current or an empty size; false = failed (the
    // presenter is then unusable).
    bool resize(core::Size size);

    // Clears to black and draws the BGRA frame scaled into `dest` (clipped to the back buffer).
    // Point = nearest neighbour; Sharp = sharp-bilinear with core::sharp_prescale.
    void render(const std::uint32_t* bgra, core::Size frame, core::Rect dest, Filter filter);
    void present(bool vsync);

    // The current back buffer as 0x??RRGGBB pixels (tests).
    std::vector<std::uint32_t> read_back();

  private:
    D3DPresenter() = default;
    bool create_pipeline(std::string& error);
    bool ensure_frame_texture(core::Size frame);
    bool create_target();

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> frame_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> frame_view_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> vertex_shader_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> pixel_shader_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> constants_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> point_sampler_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> linear_sampler_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasterizer_;
    core::Size frame_size_{};
    core::Size size_{};
    Driver driver_ = Driver::Warp;
};
```

`src/platform/presenter_d3d11.cpp` — add includes `"core/scaling.h"` and `<d3dcompiler.h>`; add in the anonymous namespace:

```cpp
// One quad from SV_VertexID (no vertex buffer), drawn into a viewport equal to the destination
// rect. mode 0 = point; mode 1 = sharp-bilinear: inside a texel's centre region the sample snaps
// to the texel centre, only a band of width 1/prescale at the edges is blended.
constexpr char kShader[] = R"(
cbuffer Params : register(b0) {
    float2 tex_size;
    float2 prescale;
    uint mode;
};
Texture2D frame_tex : register(t0);
SamplerState point_sampler : register(s0);
SamplerState linear_sampler : register(s1);

struct VsOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VsOut vs_main(uint id : SV_VertexID) {
    VsOut o;
    const float2 uv = float2(id & 1, id >> 1);
    o.uv = uv;
    o.pos = float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, 0.0, 1.0);
    return o;
}

float4 ps_main(VsOut i) : SV_Target {
    if (mode == 0) {
        return frame_tex.Sample(point_sampler, i.uv);
    }
    const float2 texel = i.uv * tex_size;
    const float2 base = floor(texel);
    const float2 dist = frac(texel) - 0.5;
    const float2 range = 0.5 - 0.5 / prescale;
    const float2 f = (dist - clamp(dist, -range, range)) * prescale + 0.5;
    return frame_tex.Sample(linear_sampler, (base + f) / tex_size);
}
)";

struct Constants {
    float tex_size[2];
    float prescale[2];
    std::uint32_t mode;
    std::uint32_t padding[3];
};
static_assert(sizeof(Constants) == 32);
```

In `create`, replace the block that gets the back buffer and creates the render target view with:

```cpp
    p->size_ = size;
    if (!p->create_target()) {
        error = "back buffer setup failed";
        return nullptr;
    }
    if (!p->create_pipeline(error)) {
        return nullptr;
    }
    return p;
```

and add the member functions:

```cpp
bool D3DPresenter::create_target() {
    return SUCCEEDED(swap_chain_->GetBuffer(0, IID_PPV_ARGS(&back_buffer_))) &&
           SUCCEEDED(device_->CreateRenderTargetView(back_buffer_.Get(), nullptr, &target_));
}

bool D3DPresenter::create_pipeline(std::string& error) {
    ComPtr<ID3DBlob> vs;
    ComPtr<ID3DBlob> ps;
    ComPtr<ID3DBlob> messages;
    HRESULT hr = D3DCompile(kShader, sizeof kShader - 1, "presenter", nullptr, nullptr, "vs_main",
                            "vs_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &vs, &messages);
    if (SUCCEEDED(hr)) {
        hr = D3DCompile(kShader, sizeof kShader - 1, "presenter", nullptr, nullptr, "ps_main",
                        "ps_4_0", D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &ps, &messages);
    }
    if (FAILED(hr)) {
        error = hresult_text("shader compile", hr);
        if (messages) {
            error += ": ";
            error.append(static_cast<const char*>(messages->GetBufferPointer()),
                         messages->GetBufferSize());
        }
        return false;
    }
    if (FAILED(hr = device_->CreateVertexShader(vs->GetBufferPointer(), vs->GetBufferSize(),
                                                nullptr, &vertex_shader_)) ||
        FAILED(hr = device_->CreatePixelShader(ps->GetBufferPointer(), ps->GetBufferSize(),
                                               nullptr, &pixel_shader_))) {
        error = hresult_text("shader creation", hr);
        return false;
    }

    D3D11_BUFFER_DESC buffer{};
    buffer.ByteWidth = sizeof(Constants);
    buffer.Usage = D3D11_USAGE_DEFAULT;
    buffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    D3D11_SAMPLER_DESC sampler{};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sampler.AddressU = sampler.AddressV = sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    D3D11_SAMPLER_DESC linear = sampler;
    linear.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    D3D11_RASTERIZER_DESC raster{};
    raster.FillMode = D3D11_FILL_SOLID;
    raster.CullMode = D3D11_CULL_NONE;
    raster.DepthClipEnable = TRUE;
    if (FAILED(hr = device_->CreateBuffer(&buffer, nullptr, &constants_)) ||
        FAILED(hr = device_->CreateSamplerState(&sampler, &point_sampler_)) ||
        FAILED(hr = device_->CreateSamplerState(&linear, &linear_sampler_)) ||
        FAILED(hr = device_->CreateRasterizerState(&raster, &rasterizer_))) {
        error = hresult_text("pipeline state", hr);
        return false;
    }
    return true;
}

bool D3DPresenter::resize(core::Size size) {
    if (size == size_ || size.width <= 0 || size.height <= 0) {
        return true;
    }
    context_->OMSetRenderTargets(0, nullptr, nullptr);
    target_.Reset();
    back_buffer_.Reset();
    context_->Flush();
    if (FAILED(swap_chain_->ResizeBuffers(0, static_cast<UINT>(size.width),
                                          static_cast<UINT>(size.height), DXGI_FORMAT_UNKNOWN,
                                          0))) {
        return false;
    }
    size_ = size;
    return create_target();
}
```

In `ensure_frame_texture`, also reset and recreate the view: after `frame_.Reset();` add `frame_view_.Reset();`, and replace the `CreateTexture2D` check with:

```cpp
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &frame_)) ||
        FAILED(device_->CreateShaderResourceView(frame_.Get(), nullptr, &frame_view_))) {
        frame_.Reset();
        return false;
    }
```

Replace `render` with:

```cpp
void D3DPresenter::render(const std::uint32_t* bgra, core::Size frame, core::Rect dest,
                          Filter filter) {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context_->ClearRenderTargetView(target_.Get(), black);
    if (bgra == nullptr || frame.width <= 0 || frame.height <= 0 || dest.width <= 0 ||
        dest.height <= 0 || !ensure_frame_texture(frame)) {
        return;
    }
    context_->UpdateSubresource(frame_.Get(), 0, nullptr, bgra, static_cast<UINT>(frame.width) * 4,
                                0);

    const core::Size prescale = core::sharp_prescale(frame, dest);
    const Constants constants{{static_cast<float>(frame.width), static_cast<float>(frame.height)},
                              {static_cast<float>(prescale.width),
                               static_cast<float>(prescale.height)},
                              filter == Filter::Sharp ? 1u : 0u,
                              {}};
    context_->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);

    // The viewport is the destination rect; parts outside the back buffer are clipped.
    const D3D11_VIEWPORT viewport{static_cast<float>(dest.x),     static_cast<float>(dest.y),
                                  static_cast<float>(dest.width), static_cast<float>(dest.height),
                                  0.0f,                           1.0f};
    ID3D11RenderTargetView* const targets[] = {target_.Get()};
    ID3D11Buffer* const buffers[] = {constants_.Get()};
    ID3D11ShaderResourceView* const views[] = {frame_view_.Get()};
    ID3D11SamplerState* const samplers[] = {point_sampler_.Get(), linear_sampler_.Get()};
    context_->OMSetRenderTargets(1, targets, nullptr);
    context_->RSSetViewports(1, &viewport);
    context_->RSSetState(rasterizer_.Get());
    context_->IASetInputLayout(nullptr);
    context_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    context_->VSSetShader(vertex_shader_.Get(), nullptr, 0);
    context_->PSSetShader(pixel_shader_.Get(), nullptr, 0);
    context_->PSSetConstantBuffers(0, 1, buffers);
    context_->PSSetShaderResources(0, 1, views);
    context_->PSSetSamplers(0, 2, samplers);
    context_->Draw(4, 0);
}
```

Remove `<algorithm>` if no longer used. In `src/platform/CMakeLists.txt` link the compiler: `target_link_libraries(hr_platform PUBLIC hr_core d3d11 dxgi d3dcompiler PRIVATE hr_warnings)`.

In `src/wing32/a2.cpp`, keep A2.1 behaviour by passing the filter at the existing call:
`d3d->render(capture.pixels(), capture.size(), dest, platform::D3DPresenter::Filter::Point);`

- [ ] **Step 4: Run to verify they pass**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/platform_tests.exe "[presenter_d3d11]"`
Expected: all 6 test cases pass. Then `ctest --preset x86-debug`: all pass.

- [ ] **Step 5: Commit**

```bash
clang-format -i src/platform/presenter_d3d11.h src/platform/presenter_d3d11.cpp tests/platform/presenter_d3d11_test.cpp src/wing32/a2.cpp
git add src tests
git commit -m "platform: shader pass with point scaling and swap-chain resize" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

---

### Task 4: Sharp-bilinear filter

**Files:**
- Modify: `tests/platform/presenter_d3d11_test.cpp` (the shader from Task 3 already contains the sharp path; this task proves it and fixes it if the tests disagree)

**Interfaces:**
- Consumes: `D3DPresenter::render(..., Filter::Sharp)` (Task 3).
- Produces: verified `Filter::Sharp` behaviour.

The shader code for `Sharp` was written in Task 3 because it shares the pipeline. To keep red/green honest, **first** make the sharp path deliberately inert, see the tests fail, then restore it: in `ps_main`, temporarily change `if (mode == 0)` to `if (true)`.

- [ ] **Step 1: Write the tests**

Append to `tests/platform/presenter_d3d11_test.cpp`:

```cpp
namespace {
int channel(std::uint32_t pixel, int shift) {
    return static_cast<int>((pixel >> shift) & 0xFF);
}
bool near_equal(std::uint32_t a, std::uint32_t b) {
    for (const int shift : {16, 8, 0}) {
        if (std::abs(channel(a, shift) - channel(b, shift)) > 1) {
            return false;
        }
    }
    return true;
}
} // namespace

TEST_CASE("sharp scaling at a whole factor equals point scaling", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const auto frame = pattern(16, 8);
    presenter->render(frame.data(), {16, 8}, {4, 6, 32, 16}, D3DPresenter::Filter::Sharp);
    const auto pixels = presenter->read_back();
    int mismatches = 0;
    for (int y = 6; y < 22; ++y) {
        for (int x = 4; x < 36; ++x) {
            mismatches += !near_equal(at(pixels, 64, x, y),
                                 frame[static_cast<size_t>((y - 6) / 2) * 16 + (x - 4) / 2]);
        }
    }
    CHECK(mismatches == 0);
    DestroyWindow(window);
}

TEST_CASE("sharp scaling at a fractional factor keeps texel centres and blends only at edges",
          "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const auto frame = pattern(15, 7);
    presenter->render(frame.data(), {15, 7}, {3, 2, 40, 20}, D3DPresenter::Filter::Sharp);
    const auto pixels = presenter->read_back();
    int exact = 0;
    int blended = 0;
    int out_of_range = 0;
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 40; ++x) {
            const int sx = ((2 * x + 1) * 15) / 80;
            const int sy = ((2 * y + 1) * 7) / 40;
            const std::uint32_t p = at(pixels, 64, x + 3, y + 2);
            for (const int shift : {16, 8, 0}) {
                int lo = 255;
                int hi = 0;
                for (int ny = std::max(sy - 1, 0); ny <= std::min(sy + 1, 6); ++ny) {
                    for (int nx = std::max(sx - 1, 0); nx <= std::min(sx + 1, 14); ++nx) {
                        const int c = channel(frame[static_cast<size_t>(ny) * 15 + nx], shift);
                        lo = std::min(lo, c);
                        hi = std::max(hi, c);
                    }
                }
                out_of_range += channel(p, shift) < lo - 1 || channel(p, shift) > hi + 1;
            }
            if (near_equal(p, frame[static_cast<size_t>(sy) * 15 + sx])) {
                ++exact;
            } else {
                ++blended;
            }
        }
    }
    CHECK(out_of_range == 0);
    CHECK(exact >= 40 * 20 / 5); // the centre regions are 1/4 of the area at prescale 2
    CHECK(blended > 0);          // and it is not plain nearest neighbour
    DestroyWindow(window);
}
```

Add `#include <algorithm>` and `#include <cstdlib>` to the test file.

- [ ] **Step 2: Run with the sharp path made inert (`if (true)`) to verify the fractional test fails**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/platform_tests.exe "[presenter_d3d11]"`
Expected: "sharp scaling at a fractional factor ..." FAILS on `CHECK(blended > 0)` (point sampling never blends). The whole-factor test passes either way — it guards against a sharp path that blurs.

- [ ] **Step 3: Restore `if (mode == 0)`**

- [ ] **Step 4: Run to verify both pass**

Run: same command. Expected: all 8 `[presenter_d3d11]` test cases pass. If the whole-factor test fails by more than ±1 per channel, the snap formula is wrong — at prescale 2 a pixel centre sits at texel fraction 0.25/0.75, `range` = 0.25, so `f` must be exactly 0.5.

- [ ] **Step 5: Commit**

```bash
clang-format -i tests/platform/presenter_d3d11_test.cpp
git add tests/platform/presenter_d3d11_test.cpp
git commit -m "platform: tests for the sharp-bilinear filter" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Resizable presenter window with redraw during sizing

**Files:**
- Modify: `src/platform/presenter_window.h`, `src/platform/presenter_window.cpp`, `tests/platform/presenter_window_test.cpp`, `src/wing32/a2.cpp` (stub `on_tick`)

**Interfaces:**
- Consumes: `core::fit_inside` (Task 1), `core::centered_rect`.
- Produces:
  - `virtual void PresenterInput::on_tick() = 0;` — called every 15 ms while the user moves or resizes the window (the modal loop blocks the owner's own loop).
  - `inline constexpr core::Size platform::kMinClient{320, 240};`
  - `PresenterWindow(PresenterInput&, core::Size client, const char* title)` — now resizable; the window is sized for `client` shrunk to fit the work area (keeping aspect, at least `kMinClient`) and centred on the work area.
  - `core::Size PresenterWindow::client_size() const`

- [ ] **Step 1: Write the failing tests**

In `tests/platform/presenter_window_test.cpp`: add `#include "test_window.h"` and `#include <cstdlib>`; give `FakeInput` a tick counter:

```cpp
    int ticks = 0;
    void on_tick() override { ++ticks; }
```

Append:

```cpp
namespace {
RECT work_area_of(HWND window) {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof monitor;
    GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor);
    return monitor.rcWork;
}
} // namespace

TEST_CASE("the presenter window is resizable and reports its client size", "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    const LONG_PTR style = GetWindowLongPtrA(window.hwnd(), GWL_STYLE);
    CHECK((style & WS_THICKFRAME) != 0);
    CHECK((style & WS_MAXIMIZEBOX) != 0);
    CHECK(window.client_size() == hr::core::Size{640, 480});

    RECT frame{0, 0, 700, 500};
    AdjustWindowRect(&frame, static_cast<DWORD>(style), FALSE);
    SetWindowPos(window.hwnd(), nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    CHECK(window.client_size() == hr::core::Size{700, 500});
}

TEST_CASE("the presenter window is centred on the work area", "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {400, 300}, "test");
    RECT r{};
    GetWindowRect(window.hwnd(), &r);
    const RECT work = work_area_of(window.hwnd());
    CHECK(std::abs((r.left + r.right) - (work.left + work.right)) <= 1);
    CHECK(std::abs((r.top + r.bottom) - (work.top + work.bottom)) <= 1);
}

TEST_CASE("a client size larger than the work area is shrunk to fit, keeping its aspect",
          "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {40000, 30000}, "test");
    RECT r{};
    GetWindowRect(window.hwnd(), &r);
    const RECT work = work_area_of(window.hwnd());
    CHECK(r.left >= work.left);
    CHECK(r.top >= work.top);
    CHECK(r.right <= work.right);
    CHECK(r.bottom <= work.bottom);
    const hr::core::Size client = window.client_size();
    CHECK(std::abs(client.width * 3 - client.height * 4) <= 4);
}

TEST_CASE("the presenter window cannot be made smaller than the minimum client size",
          "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    SetWindowPos(window.hwnd(), nullptr, 0, 0, 50, 50, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    CHECK(window.client_size().width >= hr::platform::kMinClient.width);
    CHECK(window.client_size().height >= hr::platform::kMinClient.height);
}

TEST_CASE("the presenter window ticks while it is being moved or resized", "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    SendMessageA(window.hwnd(), WM_ENTERSIZEMOVE, 0, 0);
    for (int i = 0; i < 200 && input.ticks == 0; ++i) {
        hr::test::pump();
        Sleep(5);
    }
    CHECK(input.ticks > 0);

    SendMessageA(window.hwnd(), WM_EXITSIZEMOVE, 0, 0);
    hr::test::pump();
    const int after_exit = input.ticks;
    for (int i = 0; i < 20; ++i) {
        hr::test::pump();
        Sleep(5);
    }
    CHECK(input.ticks == after_exit);
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build --preset x86-debug`
Expected: compile errors — `on_tick` marked `override` but does not override, `client_size` and `kMinClient` unknown.

- [ ] **Step 3: Implement**

`src/platform/presenter_window.h`:

```cpp
#pragma once

#include "core/geometry.h"

#include <windows.h>

namespace hr::platform {

// What the presenter window reports to its owner (all calls on the window's thread).
class PresenterInput {
  public:
    virtual ~PresenterInput() = default;
    virtual void on_mouse(UINT msg, WPARAM wparam, POINT client) = 0;
    virtual void on_key(UINT msg, WPARAM wparam, LPARAM lparam) = 0;
    virtual void on_close() = 0;
    // Every 15 ms while the user moves or resizes the window: Windows runs a modal loop then, so
    // the owner's own loop does not run.
    virtual void on_tick() = 0;
    virtual HCURSOR cursor() = 0; // nullptr = standard arrow
};

inline constexpr core::Size kMinClient{320, 240};

// Our visible window: resizable, centred on the work area when created.
class PresenterWindow {
  public:
    // The client area is `client`, shrunk (keeping its aspect) to fit the work area of the
    // primary monitor, and at least kMinClient.
    PresenterWindow(PresenterInput& input, core::Size client, const char* title);
    ~PresenterWindow();
    PresenterWindow(const PresenterWindow&) = delete;
    PresenterWindow& operator=(const PresenterWindow&) = delete;

    HWND hwnd() const { return hwnd_; }
    void show();
    core::Size client_size() const;

  private:
    static LRESULT CALLBACK proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam);
    void place(core::Size client);

    PresenterInput& input_;
    HWND hwnd_ = nullptr;
};

} // namespace hr::platform
```

`src/platform/presenter_window.cpp` — changes:

1. Includes: add `"core/scaling.h"`, `"core/viewport.h"`, `<algorithm>`.
2. Constants: replace `kStyle` with

```cpp
constexpr DWORD kWindowedStyle = WS_OVERLAPPEDWINDOW;
constexpr UINT_PTR kTickTimer = 1;
constexpr UINT kTickMs = 15;
```

3. Constructor and `place`:

```cpp
PresenterWindow::PresenterWindow(PresenterInput& input, core::Size client, const char* title)
    : input_(input) {
    static const bool registered = [] {
        WNDCLASSA wc{};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = &PresenterWindow::proc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = kClassName;
        return RegisterClassA(&wc) != 0;
    }();
    (void)registered;
    hwnd_ = CreateWindowExA(0, kClassName, title, kWindowedStyle, CW_USEDEFAULT, CW_USEDEFAULT,
                            kMinClient.width, kMinClient.height, nullptr, nullptr,
                            GetModuleHandleA(nullptr), this);
    if (hwnd_ != nullptr) {
        place(client);
    }
}

// Sizes the window for `client` (fitted to the work area, at least kMinClient) and centres it.
// The frame is measured on the real window, so this is right at any DPI.
void PresenterWindow::place(core::Size client) {
    RECT outer{};
    RECT inner{};
    GetWindowRect(hwnd_, &outer);
    GetClientRect(hwnd_, &inner);
    const core::Size frame{(outer.right - outer.left) - inner.right,
                           (outer.bottom - outer.top) - inner.bottom};
    MONITORINFO monitor{};
    monitor.cbSize = sizeof monitor;
    GetMonitorInfoA(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const RECT& work = monitor.rcWork;
    const core::Size area{work.right - work.left, work.bottom - work.top};
    core::Size fitted =
        core::fit_inside(client, {area.width - frame.width, area.height - frame.height});
    fitted = {std::max(fitted.width, kMinClient.width), std::max(fitted.height, kMinClient.height)};
    const core::Size size{fitted.width + frame.width, fitted.height + frame.height};
    const core::Rect placed = core::centered_rect(area, size);
    SetWindowPos(hwnd_, nullptr, work.left + placed.x, work.top + placed.y, size.width,
                 size.height, SWP_NOZORDER | SWP_NOACTIVATE);
}

core::Size PresenterWindow::client_size() const {
    RECT client{};
    GetClientRect(hwnd_, &client);
    return {client.right, client.bottom};
}
```

4. In `proc`'s `switch`, add:

```cpp
    case WM_GETMINMAXINFO: {
        RECT min{0, 0, kMinClient.width, kMinClient.height};
        AdjustWindowRect(&min, static_cast<DWORD>(GetWindowLongPtrA(window, GWL_STYLE)), FALSE);
        auto* const info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize = POINT{min.right - min.left, min.bottom - min.top};
        return 0;
    }
    case WM_ENTERSIZEMOVE:
        SetTimer(window, kTickTimer, kTickMs, nullptr);
        return 0;
    case WM_EXITSIZEMOVE:
        KillTimer(window, kTickTimer);
        return 0;
    case WM_TIMER:
        if (wparam == kTickTimer) {
            self->input_.on_tick();
            return 0;
        }
        break;
```

In `src/wing32/a2.cpp`, give `Input` a stub so the DLL still builds (wired in Task 7):

```cpp
    void on_tick() override {}
```

- [ ] **Step 4: Run to verify they pass**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/platform_tests.exe "[presenter_window]"`
Expected: all pass (the existing "exactly the requested client size" test included). Then `ctest --preset x86-debug`.

- [ ] **Step 5: Commit**

```bash
clang-format -i src/platform/presenter_window.h src/platform/presenter_window.cpp tests/platform/presenter_window_test.cpp src/wing32/a2.cpp
git add src tests
git commit -m "platform: resizable presenter window that ticks while being sized" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

---

### Task 6: Borderless mode and Alt+Enter

**Files:**
- Modify: `src/platform/presenter_window.h`, `src/platform/presenter_window.cpp`, `tests/platform/presenter_window_test.cpp`

**Interfaces:**
- Consumes: `core::toggles_borderless` (Task 2).
- Produces:
  - `void PresenterWindow::set_borderless(bool on)` — on: a popup covering the window's monitor; off: the previous windowed size, position and state.
  - `bool PresenterWindow::borderless() const`
  - Alt+Enter in the window toggles borderless and is not reported through `on_key`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/platform/presenter_window_test.cpp`:

```cpp
namespace {
RECT monitor_of(HWND window) {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof monitor;
    GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor);
    return monitor.rcMonitor;
}
bool same(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
} // namespace

TEST_CASE("borderless covers the monitor and leaving it restores the window",
          "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    window.show();
    RECT before{};
    GetWindowRect(window.hwnd(), &before);

    window.set_borderless(true);
    CHECK(window.borderless());
    RECT r{};
    GetWindowRect(window.hwnd(), &r);
    CHECK(same(r, monitor_of(window.hwnd())));
    CHECK((GetWindowLongPtrA(window.hwnd(), GWL_STYLE) & WS_CAPTION) == 0);

    window.set_borderless(false);
    CHECK_FALSE(window.borderless());
    GetWindowRect(window.hwnd(), &r);
    CHECK(same(r, before));
    CHECK((GetWindowLongPtrA(window.hwnd(), GWL_STYLE) & WS_CAPTION) == WS_CAPTION);
}

TEST_CASE("Alt+Enter toggles borderless and is not passed on as a key", "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    window.show();
    const LPARAM alt = LPARAM{1} << 29;
    SendMessageA(window.hwnd(), WM_SYSKEYDOWN, VK_RETURN, alt);
    CHECK(window.borderless());
    SendMessageA(window.hwnd(), WM_SYSKEYDOWN, VK_RETURN, alt | (LPARAM{1} << 30));
    CHECK(window.borderless()); // auto-repeat does not toggle back
    SendMessageA(window.hwnd(), WM_SYSKEYDOWN, VK_RETURN, alt);
    CHECK_FALSE(window.borderless());
    CHECK(input.keys == std::vector<UINT>{WM_SYSKEYDOWN}); // only the auto-repeat, which a2 drops
}
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build --preset x86-debug`
Expected: compile errors — `set_borderless` / `borderless` are not members.

- [ ] **Step 3: Implement**

`presenter_window.h` — public, after `client_size()`:

```cpp
    // Borderless: a popup covering the window's monitor. Leaving it restores the previous windowed
    // size, position and state. Alt+Enter in the window toggles it.
    void set_borderless(bool on);
    bool borderless() const { return borderless_; }
```

private, after `hwnd_`:

```cpp
    WINDOWPLACEMENT windowed_{};
    bool borderless_ = false;
```

`presenter_window.cpp` — include `"core/input_map.h"`; add:

```cpp
void PresenterWindow::set_borderless(bool on) {
    if (on == borderless_ || hwnd_ == nullptr) {
        return;
    }
    const LONG_PTR style = GetWindowLongPtrA(hwnd_, GWL_STYLE);
    if (on) {
        windowed_.length = sizeof windowed_;
        MONITORINFO monitor{};
        monitor.cbSize = sizeof monitor;
        if (!GetWindowPlacement(hwnd_, &windowed_) ||
            !GetMonitorInfoA(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &monitor)) {
            return;
        }
        SetWindowLongPtrA(hwnd_, GWL_STYLE, (style & ~WS_OVERLAPPEDWINDOW) | WS_POPUP);
        const RECT& r = monitor.rcMonitor;
        SetWindowPos(hwnd_, HWND_TOP, r.left, r.top, r.right - r.left, r.bottom - r.top,
                     SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
    } else {
        SetWindowLongPtrA(hwnd_, GWL_STYLE, (style & ~WS_POPUP) | kWindowedStyle);
        SetWindowPlacement(hwnd_, &windowed_);
        SetWindowPos(hwnd_, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                         SWP_FRAMECHANGED);
    }
    borderless_ = on;
}
```

In `proc`, split the system-key cases:

```cpp
    case WM_SYSKEYDOWN:
        if (core::toggles_borderless(msg, wparam, lparam)) {
            self->set_borderless(!self->borderless_);
            return 0;
        }
        self->input_.on_key(msg, wparam, lparam);
        break; // DefWindowProc keeps Alt+F4 and the system menu working
    case WM_SYSCHAR:
        if (wparam == '\r') {
            return 0; // Alt+Enter: no beep
        }
        self->input_.on_key(msg, wparam, lparam);
        break;
    case WM_SYSKEYUP:
        self->input_.on_key(msg, wparam, lparam);
        break;
```

(remove `WM_SYSKEYDOWN`, `WM_SYSKEYUP`, `WM_SYSCHAR` from the old shared case).

- [ ] **Step 4: Run to verify they pass**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/platform_tests.exe "[presenter_window]"`
Expected: all pass.

- [ ] **Step 5: Commit**

```bash
clang-format -i src/platform/presenter_window.h src/platform/presenter_window.cpp tests/platform/presenter_window_test.cpp
git add src tests
git commit -m "platform: borderless presenter, toggled with Alt+Enter" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

---

### Task 7: Wire scaling into the presenter thread

**Files:**
- Modify: `src/wing32/a2.cpp`

**Interfaces:**
- Consumes: `core::dest_rect` (Task 1); `D3DPresenter::resize`, `render(..., Filter)` (Task 3); `PresenterWindow::client_size`, `set_borderless`, `PresenterInput::on_tick` (Tasks 5–6); `core::Config` fields `mode`, `window_size`, `scale`, `aspect`, `vsync`.
- Produces: the A2.2 deliverable. No unit tests (spec §8: `wing32` glue is verified by the smoke test); every function it calls is tested.

- [ ] **Step 1: Replace `Input` and `presenter_body`**

Add `#include <functional>` to `a2.cpp`. Replace the `Input` class with:

```cpp
class Input final : public platform::PresenterInput {
  public:
    void set_dest(core::Rect dest) { dest_ = dest; }
    void set_tick(std::function<void()> tick) { tick_ = std::move(tick); }

    void on_mouse(UINT msg, WPARAM wparam, POINT client) override {
        const core::Rect content = content_rect();
        const core::Point g =
            core::to_content({client.x, client.y}, dest_, {content.width, content.height});
        g_cursor_x = g.x;
        g_cursor_y = g.y;
        if (g_active && core::classify(msg, wparam) == core::Forward::Mouse) {
            platform::post_mouse(g_glue, msg, wparam, POINT{content.x + g.x, content.y + g.y});
        }
    }
    void on_key(UINT msg, WPARAM wparam, LPARAM lparam) override {
        if (g_active && core::classify(msg, wparam) == core::Forward::Keyboard) {
            platform::post_key(g_glue, msg, wparam, lparam);
        }
    }
    void on_close() override { PostMessageA(g_glue, WM_CLOSE, 0, 0); }
    void on_tick() override {
        if (tick_) {
            tick_();
        }
    }
    HCURSOR cursor() override { return g_cursor; }

  private:
    core::Rect dest_{0, 0, 640, 480};
    std::function<void()> tick_;
};
```

(The A2.1 per-click `diag:` log line is dropped: the click path is verified.)

Add, above `presenter_body`:

```cpp
const char* scale_name(core::ScaleMode scale) {
    switch (scale) {
    case core::ScaleMode::Integer:
        return "integer";
    case core::ScaleMode::Sharp:
        return "sharp";
    case core::ScaleMode::NearestFit:
        return "nearest-fit";
    }
    return "?";
}
```

Replace `presenter_body` with:

```cpp
void presenter_body() {
    const HWND glue = g_glue;
    const platform::HiddenState& hidden_state = g_hidden_state;
    try {
        const core::Config& config = settings().config;
        Input input;
        platform::PresenterWindow window(input, config.window_size, "Shadow of the Horned Rat");
        g_presenter = window.hwnd();
        if (config.mode == core::DisplayMode::Borderless) {
            window.set_borderless(true);
        }

        std::string error;
        auto d3d = platform::D3DPresenter::create(window.hwnd(), window.client_size(), true, error);
        if (!d3d) {
            stop_with_unhide(glue, hidden_state, "presenter disabled, " + error);
            return;
        }
        platform::log(
            std::string("presenter: D3D11 ") +
            (d3d->driver() == platform::D3DPresenter::Driver::Hardware ? "hardware" : "WARP"));

        platform::WindowCapture capture;
        store_content_rect(find_content_rect(glue));
        if (!capture.capture(glue, content_rect())) {
            stop_with_unhide(glue, hidden_state, "presenter disabled, first capture failed");
            return;
        }

        // Never destroyed: the game thread may be inside it at any time until the process ends.
        static platform::Subclass subclass(glue, &glue_proc);

        RECT hidden{};
        GetWindowRect(glue, &hidden);
        g_hidden_x = hidden.left;
        g_hidden_y = hidden.top;
        g_active = true;
        window.show();
        platform::log("presenter: running, game window hidden at (" + std::to_string(hidden.left) +
                      "," + std::to_string(hidden.top) + ")");

        const auto filter = config.scale == core::ScaleMode::Sharp
                                ? platform::D3DPresenter::Filter::Sharp
                                : platform::D3DPresenter::Filter::Point;
        std::string failure; // set when the presenter must give up (spec §7)
        core::Rect last_dest{-1, -1, -1, -1};
        auto last_good = Clock::now();
        auto stats_start = Clock::now();
        double capture_ms = 0.0;
        double present_ms = 0.0;
        int frames = 0;

        // One frame: follow the window size, capture, scale, present. Runs from the loop below
        // and from on_tick while the user drags the window frame.
        const auto frame = [&] {
            if (!failure.empty()) {
                return;
            }
            const core::Size client = window.client_size();
            if (!d3d->resize(client)) {
                failure = "swap chain resize failed";
                return;
            }
            const core::Rect content = find_content_rect(glue);
            store_content_rect(content);
            const core::Rect dest =
                core::dest_rect(client, {content.width, content.height}, config.scale,
                                config.aspect);
            input.set_dest(dest);
            if (dest != last_dest) {
                char line[160];
                std::snprintf(line, sizeof line,
                              "presenter: client %dx%d, scale %s, dest (%d,%d %dx%d)",
                              client.width, client.height, scale_name(config.scale), dest.x,
                              dest.y, dest.width, dest.height);
                platform::log(line);
                last_dest = dest;
            }

            const auto t0 = Clock::now();
            if (capture.capture(glue, content)) {
                last_good = t0;
            } else if (t0 - last_good > std::chrono::seconds(2)) {
                failure = "capture failed for 2 s";
                return;
            }
            const auto t1 = Clock::now();
            d3d->render(capture.pixels(), capture.size(), dest, filter);
            d3d->present(config.vsync);
            const auto t2 = Clock::now();

            capture_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
            present_ms += std::chrono::duration<double, std::milli>(t2 - t1).count();
            ++frames;
            if (t2 - stats_start >= std::chrono::seconds(10)) {
                char line[160];
                std::snprintf(line, sizeof line,
                              "presenter: %d frames in 10 s, capture %.2f ms, present %.2f ms",
                              frames, capture_ms / frames, present_ms / frames);
                platform::log(line);
                stats_start = t2;
                capture_ms = present_ms = 0.0;
                frames = 0;
            }
        };
        input.set_tick(frame);

        while (!g_stop && failure.empty()) {
            MSG msg;
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            frame();
            if (!config.vsync) {
                Sleep(1);
            }
        }
        input.set_tick(nullptr);
        if (!failure.empty()) {
            stop_with_unhide(glue, hidden_state, failure);
            return;
        }
        platform::log("presenter: game window destroyed, stopping");
        g_presenter = nullptr;
    } catch (const std::exception& e) {
        platform::log_exception("presenter", e.what());
        if (!g_stop) {
            stop_with_unhide(glue, hidden_state, "exception");
        }
    } catch (...) {
        platform::log_exception("presenter", nullptr);
        if (!g_stop) {
            stop_with_unhide(glue, hidden_state, "exception");
        }
    }
}
```

Remove `kPresenterSize` and the now-unused `#include "core/viewport.h"` only if nothing else uses them (`to_content`, `fake_cursor_screen` still come from `viewport.h` — keep that include). Add `#include "core/scaling.h"`.

- [ ] **Step 2: Build and run all tests**

Run: `cmake --build --preset x86-debug; ctest --preset x86-debug; cmake --build --preset x86-release; ctest --preset x86-release`
Expected: builds with no warnings; all tests pass.

- [ ] **Step 3: Quick manual check (ask the user first)**

Ask the user for permission to install, then: `./tools/install.ps1 -GameDir 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat' -SourceDll build/x86-release/bin/wing32.dll`. The user starts the game: the presenter opens at 1280×960 (or fitted), the picture fills it sharply, resizing scales live, menu clicks hit. Read `horned-rat.log` for the `presenter: client ..., scale sharp, dest (...)` lines and no `exception`.

- [ ] **Step 4: Commit**

```bash
clang-format -i src/wing32/a2.cpp
git add src/wing32/a2.cpp
git commit -m "wing32: scaled presenter following the window size and config" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

---

### Task 8: Per-monitor DPI awareness for the presenter

**Files:**
- Create: `src/platform/dpi_context.h`, `src/platform/dpi_context.cpp`, `tests/platform/dpi_context_test.cpp`
- Modify: `src/platform/CMakeLists.txt`, `tests/platform/CMakeLists.txt`, `src/platform/presenter_window.cpp`, `tests/platform/presenter_window_test.cpp`, `src/wing32/a2.cpp`

**Interfaces:**
- Produces:
  - `DPI_AWARENESS_CONTEXT platform::current_dpi_context()` — the calling thread's context; nullptr on Windows without per-thread DPI (before Windows 10 1607).
  - `class platform::ScopedDpiContext { explicit ScopedDpiContext(DPI_AWARENESS_CONTEXT preferred, DPI_AWARENESS_CONTEXT fallback = nullptr); ~ScopedDpiContext(); bool applied() const; }` — switches the calling thread for its lifetime; tries `fallback` if `preferred` is refused; does nothing for nullptr or where unsupported.
  - `PresenterWindow` is created per-monitor aware (V2, else V1) whatever the thread's awareness; `client_size()` and `set_borderless()` measure in the window's own context.

- [ ] **Step 1: Write the failing tests**

`tests/platform/dpi_context_test.cpp`:

```cpp
#include "platform/dpi_context.h"

#include <catch2/catch_test_macros.hpp>

using hr::platform::ScopedDpiContext;

namespace {
DPI_AWARENESS thread_awareness() {
    return GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext());
}
} // namespace

TEST_CASE("a scoped DPI context switches the thread and restores it", "[dpi_context]") {
    const DPI_AWARENESS before = thread_awareness();
    {
        ScopedDpiContext scope(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2,
                               DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
        REQUIRE(scope.applied());
        CHECK(thread_awareness() == DPI_AWARENESS_PER_MONITOR_AWARE);
        {
            ScopedDpiContext inner(DPI_AWARENESS_CONTEXT_UNAWARE);
            CHECK(thread_awareness() == DPI_AWARENESS_UNAWARE);
        }
        CHECK(thread_awareness() == DPI_AWARENESS_PER_MONITOR_AWARE);
    }
    CHECK(thread_awareness() == before);
}

TEST_CASE("a scoped DPI context for nullptr changes nothing", "[dpi_context]") {
    const DPI_AWARENESS before = thread_awareness();
    ScopedDpiContext scope(nullptr);
    CHECK_FALSE(scope.applied());
    CHECK(thread_awareness() == before);
}

TEST_CASE("current_dpi_context reports the thread's context", "[dpi_context]") {
    ScopedDpiContext scope(DPI_AWARENESS_CONTEXT_UNAWARE);
    CHECK(GetAwarenessFromDpiAwarenessContext(hr::platform::current_dpi_context()) ==
          DPI_AWARENESS_UNAWARE);
}
```

Append to `tests/platform/presenter_window_test.cpp` (add `#include "platform/dpi_context.h"`):

```cpp
TEST_CASE("the presenter window is per-monitor DPI aware even from a DPI-unaware thread",
          "[presenter_window]") {
    hr::platform::ScopedDpiContext unaware(DPI_AWARENESS_CONTEXT_UNAWARE);
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    CHECK(GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(window.hwnd())) ==
          DPI_AWARENESS_PER_MONITOR_AWARE);
    CHECK(GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext()) ==
          DPI_AWARENESS_UNAWARE);
    CHECK(window.client_size() == hr::core::Size{640, 480});
}
```

Create `src/platform/dpi_context.h` with the declarations below and an empty `dpi_context.cpp`; add `dpi_context.cpp` to `hr_platform` and `dpi_context_test.cpp` to `platform_tests`:

```cpp
#pragma once

#include <windows.h>

namespace hr::platform {

// The calling thread's DPI awareness context; nullptr on Windows without per-thread DPI
// awareness (before Windows 10 1607). The functions are looked up at runtime.
DPI_AWARENESS_CONTEXT current_dpi_context();

// Switches the calling thread's DPI awareness for its lifetime. Tries `fallback` when `preferred`
// is refused (per-monitor V2 needs Windows 10 1703). Does nothing for nullptr or where
// unsupported; applied() tells.
class ScopedDpiContext {
  public:
    explicit ScopedDpiContext(DPI_AWARENESS_CONTEXT preferred,
                              DPI_AWARENESS_CONTEXT fallback = nullptr);
    ~ScopedDpiContext();
    ScopedDpiContext(const ScopedDpiContext&) = delete;
    ScopedDpiContext& operator=(const ScopedDpiContext&) = delete;

    bool applied() const { return previous_ != nullptr; }

  private:
    DPI_AWARENESS_CONTEXT previous_ = nullptr;
};

} // namespace hr::platform
```

- [ ] **Step 2: Run to verify they fail**

Run: `cmake --build --preset x86-debug`
Expected: link errors for `current_dpi_context` and `ScopedDpiContext`.

- [ ] **Step 3: Implement `dpi_context.cpp`**

```cpp
#include "platform/dpi_context.h"

namespace hr::platform {
namespace {

using SetThreadContext = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
using GetThreadContext = DPI_AWARENESS_CONTEXT(WINAPI*)();

template <class Function> Function user32_function(const char* name) {
    return reinterpret_cast<Function>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("user32.dll"), name)));
}

SetThreadContext set_thread_context() {
    static const auto function = user32_function<SetThreadContext>("SetThreadDpiAwarenessContext");
    return function;
}

} // namespace

DPI_AWARENESS_CONTEXT current_dpi_context() {
    static const auto function = user32_function<GetThreadContext>("GetThreadDpiAwarenessContext");
    return function != nullptr ? function() : nullptr;
}

ScopedDpiContext::ScopedDpiContext(DPI_AWARENESS_CONTEXT preferred,
                                   DPI_AWARENESS_CONTEXT fallback) {
    const SetThreadContext set = set_thread_context();
    if (set == nullptr) {
        return;
    }
    if (preferred != nullptr) {
        previous_ = set(preferred);
    }
    if (previous_ == nullptr && fallback != nullptr) {
        previous_ = set(fallback);
    }
}

ScopedDpiContext::~ScopedDpiContext() {
    if (previous_ != nullptr) {
        set_thread_context()(previous_);
    }
}

} // namespace hr::platform
```

- [ ] **Step 4: Run the DPI tests to verify they pass, and the window test still fails**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/platform_tests.exe "[dpi_context]"; ./build/x86-debug/bin/platform_tests.exe "[presenter_window]"`
Expected: `[dpi_context]` passes; the new presenter-window test fails on the window's awareness (`DPI_AWARENESS_UNAWARE`).

- [ ] **Step 5: Make the presenter window per-monitor aware**

In `presenter_window.cpp` include `"platform/dpi_context.h"`. Put this line first in the constructor body, in `client_size()` and in `set_borderless()`:

```cpp
    // Per-monitor awareness, so Windows never bitmap-stretches the presenter. A window keeps the
    // awareness it was created with, and its messages run in it.
    const ScopedDpiContext dpi(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2,
                               DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
```

`place()` runs inside the constructor's scope; the window procedure runs in the window's context automatically.

- [ ] **Step 6: Run the window tests to verify they pass**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/platform_tests.exe "[presenter_window]"`
Expected: all pass.

- [ ] **Step 7: Run game-window work in the game's DPI context (a2)**

Messages of the presenter window (mouse, keys, ticks) now run per-monitor aware, but they touch the hidden game window (`find_content_rect`, `post_mouse`'s child hit-test, capture), whose coordinates must stay in the game's (unaware) context. In `a2.cpp` include `"platform/dpi_context.h"` and:

1. Add a global next to the others: `std::atomic<DPI_AWARENESS_CONTEXT> g_game_dpi{nullptr};`
2. First line of `presenter_body`: `g_game_dpi = platform::current_dpi_context();`
3. First line of `Input::on_mouse`, `Input::on_key` and `Input::on_tick`:

```cpp
        const platform::ScopedDpiContext game_dpi(g_game_dpi.load());
```

`window.client_size()` inside `frame` keeps measuring the presenter in its own context (Step 5), so the destination rect and mouse coordinates stay in physical pixels while the game's coordinates stay logical.

- [ ] **Step 8: Build, run everything, commit**

Run: `cmake --build --preset x86-debug; ctest --preset x86-debug; cmake --build --preset x86-release; ctest --preset x86-release`
Expected: all pass.

```bash
clang-format -i src/platform/dpi_context.h src/platform/dpi_context.cpp tests/platform/dpi_context_test.cpp src/platform/presenter_window.cpp tests/platform/presenter_window_test.cpp src/wing32/a2.cpp
git add src tests
git commit -m "platform: per-monitor DPI aware presenter window" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

---

### Task 9: A2.2 exit — smoke test, notes, spec, PR

**Files:**
- Modify: `docs/smoke-test.md`, `docs/superpowers/specs/2026-10-05-stage-a2-presenter-design.md`
- Create: `docs/re-notes/a2-2-exit.md`

This task needs the user; ask before installing into the game folder or changing `horned-rat.ini`, and restore any ini change afterwards.

- [ ] **Step 1: Extend the smoke test** — append to `docs/smoke-test.md`:

```markdown
### A2 scaling (from A2.2)
- [ ] The presenter opens centred at `window_size` (or the largest fit on a small screen).
- [ ] Dragging the window edges scales the picture live; maximize fills the screen.
- [ ] `scale = sharp`: the picture fills the window with crisp pixels.
- [ ] `scale = integer`: whole-number scaling with black bars; every game pixel is a clean block.
- [ ] `scale = nearest-fit`: fills the window; slightly uneven pixels are expected.
- [ ] `aspect = stretch`: the picture fills the whole client area (integer: whole factor per axis).
- [ ] `mode = borderless`: the game starts covering the monitor.
- [ ] Alt+Enter toggles windowed ↔ borderless and returns to the previous window size and position.
- [ ] Clicks hit precisely at large sizes, including small buttons at the picture's edges.
- [ ] With Windows display scaling at 125 % or 150 % (if available), the picture stays crisp.
- [ ] Carried from A2.1: typing a save-game name; battle mouse control and map scrolling;
      briefing videos; clicks on the desktop do not reach the game; dragging the presenter partly
      off-screen keeps it updating; other windows stay clickable over the hidden game window.
```

- [ ] **Step 2: Install the release build (ask the user first)**

Run: `. ./tools/vsenv.ps1; cmake --build --preset x86-release; ./tools/install.ps1 -GameDir 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat' -SourceDll build/x86-release/bin/wing32.dll`
Expected: `Installed horned-rat into '...\WARFB'.`

- [ ] **Step 3: The user runs the smoke test** — one run per scale mode (edit `scale` in `horned-rat.ini` between runs, with permission), one with `mode = borderless`. Record pass/fail per item and the `presenter:` log lines (client, dest, performance at the largest size).

- [ ] **Step 4: Write `docs/re-notes/a2-2-exit.md`** — date, build commit, smoke-test table (pass / fail / not tested), performance at the largest window size, anything that differs from vanilla, and bugs found with their fixes.

- [ ] **Step 5: Update the spec** — in §5.5 (deviations) add the five "Decisions taken in this plan"; in §10 add the A2.2 results.

- [ ] **Step 6: Commit, push, mark the PR ready**

Check the PR is still open first (`gh pr view --json state`); if it was merged, put the docs commit on a new branch from `origin/main` and open a follow-up PR.

```bash
git add docs
git commit -m "docs: A2.2 exit results" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
gh pr ready
```

**Milestone A2.2 is complete.** Next: plan A2.3 (cursor) with superpowers:writing-plans.
