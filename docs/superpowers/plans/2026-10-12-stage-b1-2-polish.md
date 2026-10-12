# Stage B1.2 Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Finish Stage B1. The sharp 3D view also works in the game's low (half) detail setting and in the wide layout, and the smoke test and notes cover both.

**Architecture:**
- In low detail the game renders its 3D view at half size and stretches it with `WinGStretchBlt`. Our second render does not change: it always renders the full view size × k, so low detail looks as sharp as high detail. The B1.1 pause in low detail is removed.
- The presenter's mask compares the capture with the game's frame **as it appears on screen**. So the game thread stretches the game's half-size frame to the view size the same way GDI does: centre sampling, measured to match GDI exactly at 208→417 and 312→624. It then publishes that frame.
- The layout switch (417 ↔ 624 wide) is already handled: the device is rebuilt when the view size changes. B1.2 tests it in game.

**Tech Stack:** C++20, MSVC x86, CMake presets, Catch2, Win32 GDI (tests).

**Spec:** `docs/superpowers/specs/2026-10-07-stage-b-battle-view-design.md` (§5.1, §5.3, §8, §9 B1.2). B1.1 results: `docs/re-notes/b1-1-exit.md`.

**Outcome (2026-10-12, after the first in-game runs):** the user chose to keep low detail as the game's own pixelated look, B1.1's behaviour, so Tasks 1 and 2 were reverted (`5e3f76d`, `704f9e6`):
- the setting exists to save CPU, and rendering sharp in it does the opposite;
- the one freeze seen happened only with the sharp render in low detail.

GDI's stretch rule stays documented outside the repo, in `Documentationattle-3d-view.md`. B1.2 is now Task 3: the in-game check of both detail levels and both layouts, plus the notes.

**Deviations from the spec (decided while planning, with the user; superseded by the outcome above):**
- **Low detail renders sharp** instead of being a separate case. Our render never depended on the detail divisor; only the mask did.
- **Performance line:** the spec lists it under B1.2, but B1.1 already added it (`presenter: 3D view xK, N renders, X ms avg, Y ms max`). B1.2 only adds a smoke-test item for it.

## Global Constraints

- Layering: `wing32 → platform → core`; `core` has no Windows headers and builds on Linux CI.
- Red/green TDD for everything testable outside the game.
- Never commit game binaries, logs, captures, dumps or decompiled code. Decompiler output stays in `Decompilation\`, and game knowledge goes in `Documentation\`, both outside the repo.
- Stage B runs only for the `GAMEF.DLL` SHA-256 `e8afe836e9c99b54a76643b2adb4edb52f56a991d53ab8af60f746129b1132d6` (`core::gamef_layout`).
- Every Reality Lab call runs on the game thread under SEH; any exception turns B1 off for the session, and the game's blit still runs.
- GDI stretch mapping (measured 2026-10-12 against `StretchBlt` between 8-bit DIB sections, default and `COLORONCOLOR` modes): destination pixel `x` takes source pixel `floor((2x+1)·src / (2·dst))`. The same rule applies per axis. Plain `floor(x·src/dst)` misses 30 784 of 123 432 pixels at 208→417.
- Build and test:
  - debug: `. ./tools/vsenv.ps1; cmake --build --preset x86-debug; ctest --preset x86-debug`;
  - release: the same with `x86-release`;
  - format: `clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.h')` (clang-format 19.1.7).
- Install (pre-approved), only after a successful release build: `./tools/install.ps1 -GameDir 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat' -SourceDll build/x86-release/bin/wing32.dll`. Ini changes need the user's OK and must be restored afterwards.
- Git and PRs:
  - branch `stage-b1-2` from `main`;
  - commits end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`;
  - the PR body ends with `🤖 Generated with [Claude Code](https://claude.com/claude-code)`;
  - check the PR is open before pushing; Auto-fix on.

## Review Focus

1. **Layout switch mid-battle (417 ↔ 624).** The device is rebuilt within a frame. The overlay follows the `Map3DClass` window's new rect. `prefer_previous` ignores the old-size previous render (already unit-tested), so there is no stale-size overlay. Checked in game (Task 3).
2. **Low/high detail toggled mid-battle.** The overlay stays sharp across the switch with no blocky frame. The mask keeps working because the published game frame is always view-sized. Checked in game (Task 3).
3. **Odd view width 417 in low detail.** The game stretches 208→417. The mask must match GDI pixel for pixel, or columns of vanilla pixels would show through. Pinned by the GDI-equivalence test (Task 1).
4. **A blit whose source does not fit the game's bitmap, or whose destination size is not the active view size** (an unexpected call): nothing is published and the game's blit runs as before. Guarded in Task 2.
5. **Wide layout fullscreen at 3×** (device 1872×888, the largest render): the render time stays far under the 50 ms fallback. Read from the performance line (Task 3).

---

### Task 1: `core::stretch_indices`, GDI's centre sampling

**Files:**
- Modify: `src/core/overlay.h`, `src/core/overlay.cpp`
- Test: `tests/core/overlay_test.cpp` (rules), new `tests/platform/gdi_stretch_test.cpp` (equals real GDI), `tests/platform/CMakeLists.txt`

**Interfaces:**
- Produces: `std::vector<std::uint8_t> hr::core::stretch_indices(const std::uint8_t* src, int stride, Size src_size, Size dst_size)`. It returns `dst_size.width·dst_size.height` bytes, rows packed. Destination `(x, y)` takes `src[sy·stride + sx]` with `sx = (2x+1)·src_w / (2·dst_w)` and `sy` likewise. If any size is 0 or negative, it returns an empty vector.

- [ ] **Step 1: Write the failing core tests**

Append to `tests/core/overlay_test.cpp`:
```cpp
TEST_CASE("stretch_indices samples the source under each destination pixel's centre",
          "[overlay]") {
    using hr::core::stretch_indices;
    // 2x1 in stride 3 -> 5x2: columns take sx = (2x+1)*2/10 = 0,0,1,1,1; rows sy = 0,0.
    const std::vector<std::uint8_t> src{7, 9, 0};
    CHECK(stretch_indices(src.data(), 3, {2, 1}, {5, 2}) ==
          std::vector<std::uint8_t>{7, 7, 9, 9, 9, 7, 7, 9, 9, 9});
}

TEST_CASE("stretch_indices at the same size copies the pixels", "[overlay]") {
    const std::vector<std::uint8_t> src{1, 2, 0, 3, 4, 0}; // 2x2 in stride 3
    CHECK(hr::core::stretch_indices(src.data(), 3, {2, 2}, {2, 2}) ==
          std::vector<std::uint8_t>{1, 2, 3, 4});
}

TEST_CASE("stretch_indices with an empty size gives nothing", "[overlay]") {
    const std::vector<std::uint8_t> src{1};
    CHECK(hr::core::stretch_indices(src.data(), 1, {0, 1}, {2, 2}).empty());
    CHECK(hr::core::stretch_indices(src.data(), 1, {1, 1}, {2, 0}).empty());
}
```

- [ ] **Step 2: Write the failing GDI-equivalence test**

Create `tests/platform/gdi_stretch_test.cpp`:
```cpp
#include "core/overlay.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <cstdint>
#include <vector>

// The low-detail 3D view is stretched by GDI (WinGStretchBlt -> StretchBlt). The B1 mask needs
// the same pixels, so core::stretch_indices must map exactly like GDI at the game's sizes.
namespace {

struct Dib {
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = nullptr;
    std::uint8_t* bits = nullptr;
    Dib(int width, int height) {
        struct {
            BITMAPINFOHEADER header;
            RGBQUAD colours[256];
        } info{};
        info.header.biSize = sizeof info.header;
        info.header.biWidth = width;
        info.header.biHeight = -height;
        info.header.biPlanes = 1;
        info.header.biBitCount = 8;
        info.header.biClrUsed = 256;
        for (int i = 0; i < 256; ++i) { // distinct colours, so GDI keeps every index
            info.colours[i] = {static_cast<BYTE>(i), static_cast<BYTE>(255 - i),
                               static_cast<BYTE>(i * 7), 0};
        }
        void* raw = nullptr;
        bitmap = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS, &raw,
                                  nullptr, 0);
        bits = static_cast<std::uint8_t*>(raw);
        SelectObject(dc, bitmap);
    }
    ~Dib() {
        DeleteDC(dc);
        DeleteObject(bitmap);
    }
    Dib(const Dib&) = delete;
    Dib& operator=(const Dib&) = delete;
};

void check_like_gdi(int src_w, int src_h, int dst_w, int dst_h) {
    constexpr int stride = 624; // the game's bitmap width; a multiple of 4, as DIB rows are
    Dib src(stride, src_h);
    Dib dst(stride, dst_h);
    for (int y = 0; y < src_h; ++y) {
        for (int x = 0; x < stride; ++x) {
            src.bits[y * stride + x] = static_cast<std::uint8_t>((x + y * 3) % 251);
        }
    }
    REQUIRE(StretchBlt(dst.dc, 0, 0, dst_w, dst_h, src.dc, 0, 0, src_w, src_h, SRCCOPY));
    GdiFlush();
    const auto ours =
        hr::core::stretch_indices(src.bits, stride, {src_w, src_h}, {dst_w, dst_h});
    int differ = 0;
    for (int y = 0; y < dst_h; ++y) {
        for (int x = 0; x < dst_w; ++x) {
            differ += ours[y * dst_w + x] != dst.bits[y * stride + x] ? 1 : 0;
        }
    }
    CHECK(differ == 0);
}

} // namespace

TEST_CASE("stretch_indices matches GDI for the narrow low-detail view (208x148 -> 417x296)",
          "[gdi_stretch]") {
    check_like_gdi(208, 148, 417, 296);
}

TEST_CASE("stretch_indices matches GDI for the wide low-detail view (312x148 -> 624x296)",
          "[gdi_stretch]") {
    check_like_gdi(312, 148, 624, 296);
}
```
Add `gdi_stretch_test.cpp` to `platform_tests` in `tests/platform/CMakeLists.txt`, after `sha256_test.cpp`. If `hr_core` is not reachable from `platform_tests` through `hr_platform`, add `hr_core` to its `target_link_libraries`.

- [ ] **Step 3: Run the tests to verify they fail**

Run: `. ./tools/vsenv.ps1; cmake --build --preset x86-debug`
Expected: a compile error, `stretch_indices` is not a member of `hr::core`.

- [ ] **Step 4: Implement**

In `src/core/overlay.h`, after `to_bgra`:
```cpp
// 8-bit pixels (rows of `stride` bytes, src_size) stretched to dst_size, rows packed, the way GDI's
// StretchBlt maps them: each destination pixel takes the source pixel under its centre,
// sx = floor((2x+1)·src_w / (2·dst_w)), sy likewise. Empty if any size is empty. Stage B1 uses it
// for the game's low-detail 3D view, which the game stretches with WinGStretchBlt.
std::vector<std::uint8_t> stretch_indices(const std::uint8_t* src, int stride, Size src_size,
                                          Size dst_size);
```
In `src/core/overlay.cpp`, after `to_bgra`:
```cpp
std::vector<std::uint8_t> stretch_indices(const std::uint8_t* src, int stride, Size src_size,
                                          Size dst_size) {
    if (src_size.width <= 0 || src_size.height <= 0 || dst_size.width <= 0 ||
        dst_size.height <= 0) {
        return {};
    }
    std::vector<std::uint8_t> out(static_cast<std::size_t>(dst_size.width) * dst_size.height);
    for (int y = 0; y < dst_size.height; ++y) {
        const int sy = (2 * y + 1) * src_size.height / (2 * dst_size.height);
        const std::uint8_t* row = src + static_cast<std::ptrdiff_t>(sy) * stride;
        for (int x = 0; x < dst_size.width; ++x) {
            const int sx = (2 * x + 1) * src_size.width / (2 * dst_size.width);
            out[static_cast<std::size_t>(y) * dst_size.width + x] = row[sx];
        }
    }
    return out;
}
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset x86-debug; ctest --preset x86-debug`
Expected: all pass, including `[overlay]` and `[gdi_stretch]`.

- [ ] **Step 6: Format and commit**

```bash
clang-format -i src/core/overlay.h src/core/overlay.cpp tests/core/overlay_test.cpp tests/platform/gdi_stretch_test.cpp
git add src/core/overlay.h src/core/overlay.cpp tests/core/overlay_test.cpp tests/platform/gdi_stretch_test.cpp tests/platform/CMakeLists.txt
git commit -m "core: stretch_indices maps like GDI's StretchBlt (B1.2)"
```

---

### Task 2: the second render also runs in low detail

**Files:**
- Modify: `src/wing32/rl_shadow.h`, `src/wing32/rl_shadow.cpp`, `src/wing32/exports.cpp`

**Interfaces:**
- Consumes: `core::stretch_indices` (Task 1).
- Produces: `void hr::wing32::rl_shadow::before_blit(HDC dest, HDC src, core::Size src_size, core::Size dest_size)`. `WinGBitBlt` passes `{width, height}` for both; `WinGStretchBlt` passes `{src_width, src_height}` and `{width, height}`.

This code reads game memory and calls Reality Lab, so it cannot be unit-tested outside the game. Its pure part, the stretch, was tested in Task 1. Verify by building here and testing in game in Task 3.

- [ ] **Step 1: Pass the blit sizes**

In `src/wing32/rl_shadow.h`, include `core/geometry.h` and replace the declaration:
```cpp
// Game thread, from WinGBitBlt / WinGStretchBlt before the original, with the blit's source and
// destination sizes (equal for WinGBitBlt; half the destination in the game's low detail).
void before_blit(HDC dest, HDC src, core::Size src_size, core::Size dest_size);
```
In `src/wing32/exports.cpp`:
```cpp
        "rl_shadow", [&] { hr::wing32::rl_shadow::before_blit(dest, src, {width, height}, {width, height}); }, [] {});
```
in `WinGBitBlt`, and
```cpp
        "rl_shadow",
        [&] {
            hr::wing32::rl_shadow::before_blit(dest, src, {src_width, src_height},
                                               {width, height});
        },
        [] {});
```
in `WinGStretchBlt` (clang-format decides the final wrapping).

- [ ] **Step 2: Render in low detail; publish the frame as the screen shows it**

In `src/wing32/rl_shadow.cpp`:
- Remove `bool g_logged_half` and the whole `if (read<int>(w + l.w_divisor) != 1) { … }` block in `render`. The `w_divisor` field stays in `core::GamefLayout` as documentation of the game.
- Change `render(HDC src)` to `render(HDC src, core::Size src_size, core::Size dest_size)`. After `width` and `height` are read, add:
```cpp
    // The game blits the active view size, from its full-size (detail 1) or half-size (detail 2)
    // frame at the top left of its bitmap. Anything else is not the 3D view's frame.
    const int game_stride = static_cast<int>(read<std::uint32_t>(w + l.w_device_desc));
    if (dest_size.width != width || dest_size.height != height || src_size.width <= 0 ||
        src_size.height <= 0 || src_size.width > game_stride || src_size.height > height) {
        return;
    }
```
- Change `publish(std::uintptr_t w, HDC src)` to `publish(std::uintptr_t w, HDC src, core::Size src_size)`, and replace the row-copy loop (and its `game_stride` local) with:
```cpp
    const auto* game_bits =
        reinterpret_cast<const std::uint8_t*>(read<std::uintptr_t>(w + l.w_bits));
    const int game_stride = static_cast<int>(read<std::uint32_t>(w + l.w_device_desc));
    // As the screen shows it: the same pixels at detail 1, GDI's stretch of the half-size frame
    // at detail 2 (the presenter's mask compares the capture with this).
    frame.game = core::stretch_indices(game_bits, game_stride, src_size, {d.width, d.height});
```
- At the end of `render`, call `publish(w, src, src_size);`.
- `before_blit` passes the sizes through: `render(src, src_size, dest_size);`.
- Add `#include "core/overlay.h"` with the other `core/` includes.

- [ ] **Step 3: Build, test, format**

Run: `. ./tools/vsenv.ps1; cmake --build --preset x86-debug; ctest --preset x86-debug; cmake --build --preset x86-release`, then `clang-format --dry-run --Werror $(git ls-files '*.cpp' '*.h')`.
Expected: everything builds, all tests pass, no format errors. `grep -n "half detail" src` finds nothing.

- [ ] **Step 4: Commit**

```bash
git add src/wing32/rl_shadow.h src/wing32/rl_shadow.cpp src/wing32/exports.cpp
git commit -m "wing32: the sharp 3D view also renders in the game's low detail (B1.2)"
```

---

### Task 3: in-game check, smoke test, notes

**Files:**
- Modify: `docs/smoke-test.md`, `docs/superpowers/specs/2026-10-07-stage-b-battle-view-design.md` (§9 B1.2 result line)
- Create: `docs/re-notes/b1-2-exit.md`
- Outside the repo: `Documentation\battle-3d-view.md` (the layout controls and the GDI stretch rule)

- [ ] **Step 1: Install and open the PR**

Install the release build (pre-approved). Push the branch, then open a draft PR against `main` titled "Stage B1.2: sharp 3D view in low detail and the wide layout". Turn Auto-fix on.

- [ ] **Step 2: Runs with the user** (the default ini; no ini change is needed)

1. **Window, 1280×960:**
   - start a battle;
   - switch the 3D detail to low and back to high a few times while the camera moves;
   - switch to the wide 3D view (the battle-UI control that hides the 2D map panel; the game toggles it through controls 0x2e/0x2f) and back;
   - open something over the view in each state.
   Expect: sharp in every state, with no blocky frame, flicker or leftover overlay after a switch.
2. **Fullscreen (Alt+Enter):** wide layout at low and at high detail, briefly.

After each run, read `horned-rat.log`. Expect:
- `b1: device 840x592 (x2)` and `1248x592 (x2)` for the wide layout (2× of 624, already 4-aligned); `1260x888` and `1872x888` at 3×;
- no `b1: off` line;
- the `3D view` performance lines, including the wide 3× render times;
- the overlay line's "from the previous render" and "mostly hidden" counts.

- [ ] **Step 3: Smoke-test items**

Append to the "Stage B1 sharp 3D view" section of `docs/smoke-test.md`:
```markdown
- [ ] Low 3D detail: the view is as sharp as at high detail; switching detail during a battle
      shows no blocky frame.
- [ ] Wide 3D view (2D map panel hidden) and back: sharp in both layouts; the log shows a new
      `b1: device` line per switch (`1248x592` wide at x2); no leftover overlay at the old size.
- [ ] `horned-rat.log`: the `3D view xK` lines stay well under 50 ms avg (also wide at x3), and
      no `b1: second render too slow` line in normal play.
```

- [ ] **Step 4: Notes**

- `docs/re-notes/b1-2-exit.md`: setup, runs and what the user saw, log figures (render ms per factor and layout), problems found and fixed, open items (filed as issues if any).
- Spec §9 B1.2: a result line.
- `Documentation\battle-3d-view.md`: the layout controls (`FUN_100413b0`/`FUN_10041400`, messages 0x2B55/0x2B56, control ids 0x2E/0x2F in the table at `0x100A1A00`, `FUN_10044830` enabling one and disabling the other), the in-game name of the control once the user has found it, and GDI's stretch rule.

- [ ] **Step 5: Commit, push, mark ready**

```bash
git add docs/smoke-test.md docs/re-notes/b1-2-exit.md docs/superpowers/specs/2026-10-07-stage-b-battle-view-design.md
git commit -m "docs: B1.2 results and smoke-test items"
```
Check the PR is open, push, update the PR body with the results, and run `gh pr ready`.
