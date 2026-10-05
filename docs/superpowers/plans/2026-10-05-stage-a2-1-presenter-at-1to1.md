# Stage A2.1: Presenter at 1:1 — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The game is shown in our own in-process presenter window at exactly 640×480, captured from its composed, hidden main window every frame, with mouse, keyboard and cursor working as in vanilla, and every failure ending with an unhidden, playable game.

**Architecture:** Pure decisions (coordinate mapping, message classification, button masking, activation filtering, content-rect choice, brightness) live in `core` and are unit-tested on Windows and Linux. Thin Win32 building blocks (window capture, hiding, subclassing, child input, D3D11 presenter, presenter window) live in `platform` and are tested on Windows CI with windows the tests create themselves. `wing32/a2.cpp` wires them together on a presenter thread and is verified by the manual smoke test.

**Tech Stack:** C++20, CMake/Ninja presets, MSVC x86 (static CRT), Direct3D 11 + DXGI 1.2 (flip model), GDI, Catch2 v3.8.1, GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-10-05-stage-a2-presenter-design.md` (milestone **A2.1**, §9). Background facts: `docs/re-notes/a1-findings.md`.

**Revision (during Task 5):** off-screen parking was found not to work — GDI clips drawing to the on-screen part of a window. Tasks 5, 6, 12 and 13 below are revised accordingly; the `park` config key committed in Task 3 is removed again in Task 6.

## Global Constraints

- Never commit game binaries, assets, game logs, captures or decompiled code.
- Red/green TDD: every behaviour's test is run and **seen to fail** before it is implemented.
- `src/core` must not include `<windows.h>` or any Windows header (Linux CI enforces this). Win32 constants needed by `core` are mirrored in `core/input_map.h` and checked against `<windows.h>` by a platform test.
- Windows binaries: 32-bit x86, MSVC, static CRT, `/W4 /WX /permissive- /utf-8`. Non-MSVC: `-Wall -Wextra -Wpedantic -Werror`.
- clang-format 19.1.7, repo `.clang-format` (LLVM base, 4 spaces, 100 columns, `PointerAlignment: Left`).
- Every commit message ends with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- "Never worse than vanilla": every presenter failure path ends with the game window unhidden at (0,0) and playable (spec §7).
- Presenter size in A2.1 is fixed at 640×480 client, picture drawn 1:1 (scaling is A2.2).
- The game window is **hidden on-screen** (layered alpha 0, click-through, bottom of the z-order, inside its monitor's work area). Off-screen parking does not work (spec §2 correction, found in Task 5); there is no `park` option.

**Deviations from the spec, decided while planning (record them in the spec at Task 13):**
1. The hidden position is enforced by the `Glue` subclass on `WM_WINDOWPOSCHANGING` (catches `SetWindowPos`, `MoveWindow` and everything else) instead of a `SetWindowPos` IAT hook.
2. A2.1 always reports the game window as active (= `background = run`); the `background` key is added in A2.4 as the spec says.
3. At 1:1 the presenter copies the frame with `CopySubresourceRegion` (exact, no shader); the scaling shader arrives with A2.2.

## How to build and test locally (used by every task)

PowerShell 7, repo root:

```powershell
. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^<label>$"
```

**Format** (always this form; covers new files):

```powershell
clang-format -i @(git ls-files --cached --others --exclude-standard '*.cpp' '*.h')
```

CI: the PR's CI is watched by the desktop app (Auto-fix); after pushing, do not poll — fix and push again if a CI event reports a failure.

**Branch:** work on a new branch `stage-a2-1` created from `stage-a0-a1` (PR #1 is not merged yet). Open a draft PR against `stage-a0-a1` after the first push; retarget it to `main` once PR #1 is merged.

## File Structure

```
src/core/
  geometry.h            + Point, Rect
  viewport.{h,cpp}      centered_rect, to_content, fake_cursor_screen
  input_map.{h,cpp}     Win32 constant mirror; classify, is_mouse_button, report_button,
                        filter_activation
  content_rect.{h,cpp}  choose_content_rect
  luma.{h,cpp}          mean_luma
  viewport (extended)   + keep_inside
src/platform/
  window_capture.{h,cpp}     WindowCapture
  hidden_window.{h,cpp}      HiddenState, hide_window, unhide_window
  window_subclass.{h,cpp}    Subclass
  child_input.{h,cpp}        deepest_child_at, post_mouse, post_key
  presenter_d3d11.{h,cpp}    D3DPresenter
  presenter_window.{h,cpp}   PresenterInput, PresenterWindow
src/wing32/
  a2.{h,cpp}            presenter thread + state; hook helpers
  hooks.cpp             GetCursorPos fakes, + GetAsyncKeyState, + SetCursor, CreateWindowExA → a2
tools/luma_probe/       dev tool for the fade check
tests/core/             viewport_test, input_map_test, content_rect_test, luma_test (+ config_test)
tests/platform/         test_window.h; window_capture_test, hidden_window_test, window_subclass_test,
                        child_input_test, presenter_d3d11_test, presenter_window_test
docs/smoke-test.md, docs/re-notes/a2-1-exit.md
```

---

### Task 1: Points, rects and the 1:1 viewport (core)

**Files:**
- Modify: `src/core/geometry.h`, `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`
- Create: `src/core/viewport.h`, `src/core/viewport.cpp`
- Test: `tests/core/viewport_test.cpp`

**Interfaces:**
- Consumes: `hr::core::Size` (existing).
- Produces:
  - `struct Point { int x = 0; int y = 0; };` and `struct Rect { int x = 0; int y = 0; int width = 0; int height = 0; };`, both with defaulted `==`.
  - `Rect hr::core::centered_rect(Size client, Size content);`
  - `Point hr::core::to_content(Point client_point, Rect dest, Size content);` — maps any destination size (A2.2 reuses it), floors, clamps to `0..content-1`.
  - `Point hr::core::fake_cursor_screen(Point glue_client_origin, Rect content_rect, Point content_point);`

- [ ] **Step 1: Write the failing test**

`tests/core/viewport_test.cpp`:

```cpp
#include "core/viewport.h"

#include <catch2/catch_test_macros.hpp>

using namespace hr::core;

TEST_CASE("the picture is centred in the client area at 1:1", "[viewport]") {
    CHECK(centered_rect({640, 480}, {640, 480}) == Rect{0, 0, 640, 480});
    CHECK(centered_rect({800, 600}, {640, 480}) == Rect{80, 60, 640, 480});
    CHECK(centered_rect({600, 400}, {640, 480}) == Rect{-20, -40, 640, 480});
}

TEST_CASE("client points map to content points through the destination rect", "[viewport]") {
    const Rect dest{80, 60, 640, 480};
    CHECK(to_content({80, 60}, dest, {640, 480}) == Point{0, 0});
    CHECK(to_content({85, 70}, dest, {640, 480}) == Point{5, 10});
    CHECK(to_content({719, 539}, dest, {640, 480}) == Point{639, 479});
}

TEST_CASE("points outside the picture clamp to its nearest edge", "[viewport]") {
    const Rect dest{80, 60, 640, 480};
    CHECK(to_content({0, 0}, dest, {640, 480}) == Point{0, 0});
    CHECK(to_content({79, 59}, dest, {640, 480}) == Point{0, 0});
    CHECK(to_content({5000, -5}, dest, {640, 480}) == Point{639, 0});
}

TEST_CASE("mapping also works for scaled destinations", "[viewport]") {
    const Rect dest{0, 0, 1280, 960};
    CHECK(to_content({3, 3}, dest, {640, 480}) == Point{1, 1});
    CHECK(to_content({1279, 959}, dest, {640, 480}) == Point{639, 479});
}

TEST_CASE("an empty destination maps everything to the origin", "[viewport]") {
    CHECK(to_content({10, 10}, Rect{0, 0, 0, 0}, {640, 480}) == Point{0, 0});
}

TEST_CASE("the fake cursor is the parked window's client origin plus content offset and point",
          "[viewport]") {
    CHECK(fake_cursor_screen({-32000, -32000}, Rect{5, 5, 640, 480}, {10, 20}) ==
          Point{-31985, -31975});
}
```

Add `viewport_test.cpp` to `add_executable(core_tests ...)` in `tests/core/CMakeLists.txt`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'core/viewport.h'`.

- [ ] **Step 3: Write the minimal implementation**

Append to `src/core/geometry.h` inside `namespace hr::core`, after `Size`:

```cpp
struct Point {
    int x = 0;
    int y = 0;

    friend bool operator==(const Point&, const Point&) = default;
};

struct Rect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    friend bool operator==(const Rect&, const Rect&) = default;
};
```

`src/core/viewport.h`:

```cpp
#pragma once

#include "core/geometry.h"

namespace hr::core {

// Where the content picture goes inside the presenter client area at 1:1: centred, and allowed to
// extend beyond a client area that is too small.
Rect centered_rect(Size client, Size content);

// Maps a presenter-client point to a content point through `dest` (any size), flooring and
// clamping to the picture so points in the bars land on the nearest edge.
Point to_content(Point client_point, Rect dest, Size content);

// The screen position GetCursorPos must report so that the game's own ScreenToClient on its
// (parked) main window yields `content_point` inside `content_rect`.
Point fake_cursor_screen(Point glue_client_origin, Rect content_rect, Point content_point);

} // namespace hr::core
```

`src/core/viewport.cpp`:

```cpp
#include "core/viewport.h"

#include <algorithm>

namespace hr::core {
namespace {

int map_axis(int p, int start, int length, int content) {
    if (length <= 0 || content <= 0) {
        return 0;
    }
    const long long scaled = static_cast<long long>(p - start) * content;
    long long q = scaled / length;
    if (scaled % length != 0 && scaled < 0) {
        --q; // floor, not truncation
    }
    return static_cast<int>(std::clamp<long long>(q, 0, content - 1));
}

} // namespace

Rect centered_rect(Size client, Size content) {
    return {(client.width - content.width) / 2, (client.height - content.height) / 2,
            content.width, content.height};
}

Point to_content(Point client_point, Rect dest, Size content) {
    return {map_axis(client_point.x, dest.x, dest.width, content.width),
            map_axis(client_point.y, dest.y, dest.height, content.height)};
}

Point fake_cursor_screen(Point glue_client_origin, Rect content_rect, Point content_point) {
    return {glue_client_origin.x + content_rect.x + content_point.x,
            glue_client_origin.y + content_rect.y + content_point.y};
}

} // namespace hr::core
```

Add `viewport.cpp` to `add_library(hr_core STATIC ...)` in `src/core/CMakeLists.txt`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^viewport$"`
Expected: `100% tests passed, 0 tests failed out of 6`.

- [ ] **Step 5: Format and commit**

Run Format, then:

```bash
git add src/core tests/core
git commit -m "core: points, rects and 1:1 viewport mapping" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Input decisions (core)

**Files:**
- Create: `src/core/input_map.h`, `src/core/input_map.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`
- Test: `tests/core/input_map_test.cpp`

**Interfaces:**
- Produces (namespace `hr::core`):
  - `namespace win { ... }` constants: `kWmActivate 0x0006`, `kWmActivateApp 0x001C`, `kWmKeyDown 0x0100`, `kWmKeyUp 0x0101`, `kWmChar 0x0102`, `kWmSysKeyDown 0x0104`, `kWmSysKeyUp 0x0105`, `kWmSysChar 0x0106`, `kWmMouseMove 0x0200`, `kWmLButtonDown 0x0201` … `kWmXButtonDblClk 0x020D` (incl. `kWmMouseWheel 0x020A`), `kVkLButton 0x01`, `kVkRButton 0x02`, `kVkMButton 0x04`, `kVkXButton1 0x05`, `kVkXButton2 0x06`, `kVkReturn 0x0D`, `kWaInactive 0`, `kWaActive 1`.
  - `enum class Forward { None, Mouse, Keyboard };`
  - `Forward classify(unsigned message, std::uintptr_t wparam);` — Alt+Enter (`WM_SYSKEYDOWN`/`WM_SYSKEYUP` with `VK_RETURN`, `WM_SYSCHAR` with `'\r'`) is reserved for us → `None`.
  - `bool is_mouse_button(int vk);`
  - `bool report_button(bool presenter_foreground, bool cursor_in_client);`
  - `std::uintptr_t filter_activation(unsigned message, std::uintptr_t wparam);`

- [ ] **Step 1: Write the failing test**

`tests/core/input_map_test.cpp`:

```cpp
#include "core/input_map.h"

#include <catch2/catch_test_macros.hpp>

using namespace hr::core;
using namespace hr::core::win;

TEST_CASE("mouse messages are forwarded as mouse input", "[input_map]") {
    for (unsigned m = kWmMouseMove; m <= kWmXButtonDblClk; ++m) {
        INFO(m);
        CHECK(classify(m, 0) == Forward::Mouse);
    }
}

TEST_CASE("keyboard messages are forwarded, except our Alt+Enter", "[input_map]") {
    CHECK(classify(kWmKeyDown, 'A') == Forward::Keyboard);
    CHECK(classify(kWmKeyUp, 'A') == Forward::Keyboard);
    CHECK(classify(kWmChar, 'a') == Forward::Keyboard);
    CHECK(classify(kWmSysKeyDown, 'A') == Forward::Keyboard);
    CHECK(classify(kWmSysKeyUp, 'A') == Forward::Keyboard);
    CHECK(classify(kWmSysChar, 'a') == Forward::Keyboard);
    CHECK(classify(kWmSysKeyDown, kVkReturn) == Forward::None);
    CHECK(classify(kWmSysKeyUp, kVkReturn) == Forward::None);
    CHECK(classify(kWmSysChar, '\r') == Forward::None);
}

TEST_CASE("other messages are not forwarded", "[input_map]") {
    CHECK(classify(0x000F /* WM_PAINT */, 0) == Forward::None);
    CHECK(classify(kWmActivate, 0) == Forward::None);
}

TEST_CASE("mouse buttons are recognised by virtual-key code", "[input_map]") {
    for (int vk : {kVkLButton, kVkRButton, kVkMButton, kVkXButton1, kVkXButton2}) {
        CHECK(is_mouse_button(vk));
    }
    CHECK_FALSE(is_mouse_button(0x03)); // VK_CANCEL
    CHECK_FALSE(is_mouse_button('A'));
}

TEST_CASE("physical mouse buttons are reported only over the focused presenter", "[input_map]") {
    CHECK(report_button(true, true));
    CHECK_FALSE(report_button(true, false));
    CHECK_FALSE(report_button(false, true));
    CHECK_FALSE(report_button(false, false));
}

TEST_CASE("deactivation is reported to the game as activation", "[input_map]") {
    CHECK(filter_activation(kWmActivate, kWaInactive) == kWaActive);
    CHECK(filter_activation(kWmActivate, 0x00010000u) == 0x00010001u); // inactive + minimised
    CHECK(filter_activation(kWmActivate, 2) == 2u);                    // WA_CLICKACTIVE
    CHECK(filter_activation(kWmActivateApp, 0) == 1u);
    CHECK(filter_activation(kWmActivateApp, 1) == 1u);
    CHECK(filter_activation(kWmKeyDown, 0) == 0u);
}
```

Add `input_map_test.cpp` to `core_tests`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'core/input_map.h'`.

- [ ] **Step 3: Write the minimal implementation**

`src/core/input_map.h`:

```cpp
#pragma once

#include <cstdint>

namespace hr::core {

// Win32 values mirrored so core stays free of Windows headers. A platform test checks each one
// against <windows.h>.
namespace win {
inline constexpr unsigned kWmActivate = 0x0006;
inline constexpr unsigned kWmActivateApp = 0x001C;
inline constexpr unsigned kWmKeyDown = 0x0100;
inline constexpr unsigned kWmKeyUp = 0x0101;
inline constexpr unsigned kWmChar = 0x0102;
inline constexpr unsigned kWmSysKeyDown = 0x0104;
inline constexpr unsigned kWmSysKeyUp = 0x0105;
inline constexpr unsigned kWmSysChar = 0x0106;
inline constexpr unsigned kWmMouseMove = 0x0200;
inline constexpr unsigned kWmLButtonDown = 0x0201;
inline constexpr unsigned kWmLButtonUp = 0x0202;
inline constexpr unsigned kWmLButtonDblClk = 0x0203;
inline constexpr unsigned kWmRButtonDown = 0x0204;
inline constexpr unsigned kWmRButtonUp = 0x0205;
inline constexpr unsigned kWmRButtonDblClk = 0x0206;
inline constexpr unsigned kWmMButtonDown = 0x0207;
inline constexpr unsigned kWmMButtonUp = 0x0208;
inline constexpr unsigned kWmMButtonDblClk = 0x0209;
inline constexpr unsigned kWmMouseWheel = 0x020A;
inline constexpr unsigned kWmXButtonDown = 0x020B;
inline constexpr unsigned kWmXButtonUp = 0x020C;
inline constexpr unsigned kWmXButtonDblClk = 0x020D;
inline constexpr int kVkLButton = 0x01;
inline constexpr int kVkRButton = 0x02;
inline constexpr int kVkMButton = 0x04;
inline constexpr int kVkXButton1 = 0x05;
inline constexpr int kVkXButton2 = 0x06;
inline constexpr int kVkReturn = 0x0D;
inline constexpr std::uintptr_t kWaInactive = 0;
inline constexpr std::uintptr_t kWaActive = 1;
} // namespace win

enum class Forward { None, Mouse, Keyboard };

// Which presenter-window messages are passed on to the game (spec §5.2).
Forward classify(unsigned message, std::uintptr_t wparam);

bool is_mouse_button(int vk);

// GetAsyncKeyState is global: a physical mouse button is shown to the game only while the
// presenter is in the foreground and the real cursor is over its client area.
bool report_button(bool presenter_foreground, bool cursor_in_client);

// WM_ACTIVATE / WM_ACTIVATEAPP parameters rewritten so the game always believes it is active.
std::uintptr_t filter_activation(unsigned message, std::uintptr_t wparam);

} // namespace hr::core
```

`src/core/input_map.cpp`:

```cpp
#include "core/input_map.h"

namespace hr::core {

using namespace win;

Forward classify(unsigned message, std::uintptr_t wparam) {
    if (message >= kWmMouseMove && message <= kWmXButtonDblClk) {
        return Forward::Mouse;
    }
    switch (message) {
    case kWmSysKeyDown:
    case kWmSysKeyUp:
        return wparam == static_cast<std::uintptr_t>(kVkReturn) ? Forward::None
                                                                : Forward::Keyboard;
    case kWmSysChar:
        return wparam == '\r' ? Forward::None : Forward::Keyboard;
    case kWmKeyDown:
    case kWmKeyUp:
    case kWmChar:
        return Forward::Keyboard;
    default:
        return Forward::None;
    }
}

bool is_mouse_button(int vk) {
    return vk == kVkLButton || vk == kVkRButton || vk == kVkMButton || vk == kVkXButton1 ||
           vk == kVkXButton2;
}

bool report_button(bool presenter_foreground, bool cursor_in_client) {
    return presenter_foreground && cursor_in_client;
}

std::uintptr_t filter_activation(unsigned message, std::uintptr_t wparam) {
    if (message == kWmActivate && (wparam & 0xFFFF) == kWaInactive) {
        return (wparam & ~std::uintptr_t{0xFFFF}) | kWaActive;
    }
    if (message == kWmActivateApp && wparam == 0) {
        return 1;
    }
    return wparam;
}

} // namespace hr::core
```

Add `input_map.cpp` to `hr_core`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^input_map$"`
Expected: `100% tests passed, 0 tests failed out of 6`.

- [ ] **Step 5: Format and commit**

Run Format, then:

```bash
git add src/core tests/core
git commit -m "core: input forwarding, button masking and activation filtering" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Content rect and the `park` config key (core)

**Files:**
- Create: `src/core/content_rect.h`, `src/core/content_rect.cpp`
- Modify: `src/core/config.h`, `src/core/config.cpp`, `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`, `tests/core/config_test.cpp`
- Test: `tests/core/content_rect_test.cpp`, `tests/core/config_test.cpp`

**Interfaces:**
- Consumes: `Rect`, `Size` (Task 1).
- Produces:
  - `Rect hr::core::choose_content_rect(Size client, std::optional<Rect> child);` — the `BitmapScrn` child rect when it is non-empty and inside the client, else the whole client.
  - `enum class hr::core::ParkMode { Offscreen, Underneath };` and `Config::park` (default `Offscreen`), INI key `display.park` (`offscreen` | `underneath`).

- [ ] **Step 1: Write the failing tests**

`tests/core/content_rect_test.cpp`:

```cpp
#include "core/content_rect.h"

#include <catch2/catch_test_macros.hpp>

using namespace hr::core;

TEST_CASE("the BitmapScrn child is the content when it lies inside the client", "[content_rect]") {
    CHECK(choose_content_rect({650, 491}, Rect{5, 5, 640, 480}) == Rect{5, 5, 640, 480});
}

TEST_CASE("without a usable child the whole client is captured", "[content_rect]") {
    CHECK(choose_content_rect({650, 491}, std::nullopt) == Rect{0, 0, 650, 491});
    CHECK(choose_content_rect({650, 491}, Rect{5, 5, 0, 480}) == Rect{0, 0, 650, 491});
    CHECK(choose_content_rect({650, 491}, Rect{20, 20, 640, 480}) == Rect{0, 0, 650, 491});
    CHECK(choose_content_rect({650, 491}, Rect{-1, 0, 640, 480}) == Rect{0, 0, 650, 491});
}
```

Append to `tests/core/config_test.cpp`:

```cpp
TEST_CASE("the park mode is read and defaults to off-screen", "[config]") {
    CHECK(Config{}.park == ParkMode::Offscreen);
    CHECK(load_config("[display]\npark = underneath\n").config.park == ParkMode::Underneath);
    CHECK(load_config("[display]\npark = OffScreen\n").config.park == ParkMode::Offscreen);
    const auto bad = load_config("[display]\npark = sideways\n");
    CHECK(bad.config.park == ParkMode::Offscreen);
    REQUIRE(bad.warnings.size() == 1);
    CHECK_THAT(bad.warnings[0], ContainsSubstring("display.park"));
}
```

Add `content_rect_test.cpp` to `core_tests`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'core/content_rect.h'` (and `park` is not a member of `Config`).

- [ ] **Step 3: Write the minimal implementation**

`src/core/content_rect.h`:

```cpp
#pragma once

#include "core/geometry.h"

#include <optional>

namespace hr::core {

// The region of the game's main-window client area that holds the 640×480 picture: the
// BitmapScrn child when it is non-empty and fully inside the client, else the whole client.
Rect choose_content_rect(Size client, std::optional<Rect> child);

} // namespace hr::core
```

`src/core/content_rect.cpp`:

```cpp
#include "core/content_rect.h"

namespace hr::core {

Rect choose_content_rect(Size client, std::optional<Rect> child) {
    if (child && child->width > 0 && child->height > 0 && child->x >= 0 && child->y >= 0 &&
        child->x + child->width <= client.width && child->y + child->height <= client.height) {
        return *child;
    }
    return {0, 0, client.width, client.height};
}

} // namespace hr::core
```

In `src/core/config.h` add after `enum class AspectMode ...`:

```cpp
enum class ParkMode { Offscreen, Underneath };
```

and in `struct Config` after `bool vsync = true;`:

```cpp
    ParkMode park = ParkMode::Offscreen;
```

In `src/core/config.cpp` add to `kFields` after the `display.vsync` entry:

```cpp
    {"display.park",
     [](Config& c, std::string_view v) {
         return assign(c.park, parse_choice<ParkMode>(v, {{"offscreen", ParkMode::Offscreen},
                                                          {"underneath", ParkMode::Underneath}}));
     }},
```

and in `default_config_ini()` add after the `vsync` line:

```
park         = offscreen   ; offscreen | underneath (fallback: hidden under the presenter)
```

Add `content_rect.cpp` to `hr_core`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^(content_rect|config)$"`
Expected: `100% tests passed, 0 tests failed out of 11` (2 content_rect + 9 config, including the existing default-file round trip).

- [ ] **Step 5: Format and commit**

Run Format, then:

```bash
git add src/core tests/core
git commit -m "core: content rect choice and park mode config" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Picture brightness (core)

**Files:**
- Create: `src/core/luma.h`, `src/core/luma.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`
- Test: `tests/core/luma_test.cpp`

**Interfaces:**
- Produces: `double hr::core::mean_luma(const std::uint32_t* pixels, int width, int height);` — pixels `0x00RRGGBB` (any alpha byte ignored), tightly packed, Rec.601 weights; `0.0` for empty input.

- [ ] **Step 1: Write the failing test**

`tests/core/luma_test.cpp`:

```cpp
#include "core/luma.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using Catch::Approx;
using hr::core::mean_luma;

TEST_CASE("brightness of uniform pictures", "[luma]") {
    const std::vector<std::uint32_t> black(16, 0x000000), white(16, 0xFFFFFF), red(16, 0xFF0000);
    CHECK(mean_luma(black.data(), 4, 4) == Approx(0.0));
    CHECK(mean_luma(white.data(), 4, 4) == Approx(255.0));
    CHECK(mean_luma(red.data(), 4, 4) == Approx(0.299 * 255));
}

TEST_CASE("brightness is the mean over all pixels and ignores alpha", "[luma]") {
    const std::vector<std::uint32_t> half{0xFF000000, 0xFFFFFFFF};
    CHECK(mean_luma(half.data(), 2, 1) == Approx(127.5));
}

TEST_CASE("an empty picture has zero brightness", "[luma]") {
    CHECK(mean_luma(nullptr, 0, 0) == Approx(0.0));
}
```

Add `luma_test.cpp` to `core_tests`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'core/luma.h'`.

- [ ] **Step 3: Write the minimal implementation**

`src/core/luma.h`:

```cpp
#pragma once

#include <cstdint>

namespace hr::core {

// Mean Rec.601 brightness (0..255) of tightly packed 0x??RRGGBB pixels. Used by the fade check.
double mean_luma(const std::uint32_t* pixels, int width, int height);

} // namespace hr::core
```

`src/core/luma.cpp`:

```cpp
#include "core/luma.h"

namespace hr::core {

double mean_luma(const std::uint32_t* pixels, int width, int height) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return 0.0;
    }
    const long long count = static_cast<long long>(width) * height;
    double sum = 0.0;
    for (long long i = 0; i < count; ++i) {
        const std::uint32_t p = pixels[i];
        sum += 0.299 * ((p >> 16) & 0xFF) + 0.587 * ((p >> 8) & 0xFF) + 0.114 * (p & 0xFF);
    }
    return sum / static_cast<double>(count);
}

} // namespace hr::core
```

Add `luma.cpp` to `hr_core`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^luma$"`
Expected: `100% tests passed, 0 tests failed out of 3`.

- [ ] **Step 5: Format, commit, push, open the draft PR**

Run Format, then:

```bash
git add src/core tests/core
git commit -m "core: mean picture brightness" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push -u origin stage-a2-1
gh pr create --draft --base stage-a0-a1 --title "Stage A2.1: presenter at 1:1" --body "Implements milestone A2.1 of docs/superpowers/specs/2026-10-05-stage-a2-presenter-design.md. Plan: docs/superpowers/plans/2026-10-05-stage-a2-1-presenter-at-1to1.md. Draft until all tasks are done.

🤖 Generated with [Claude Code](https://claude.com/claude-code)"
```

Expected: the Linux core jobs build all new `core` code (no Windows headers).

---

### Task 5: Test window helper and window capture (platform)

**Files:**
- Create: `tests/platform/test_window.h`, `src/platform/window_capture.h`, `src/platform/window_capture.cpp`
- Modify: `src/platform/CMakeLists.txt`, `tests/platform/CMakeLists.txt`
- Test: `tests/platform/window_capture_test.cpp`

**Interfaces:**
- Consumes: `core::Rect`, `core::Size`.
- Produces:
  - Test helpers (namespace `hr::test`): `struct ReceivedMessage { UINT msg; WPARAM wparam; LPARAM lparam; };`, `std::map<HWND, std::vector<ReceivedMessage>>& received();`, `HWND make_window(int x, int y, int width, int height, HWND parent = nullptr);` (top-level `WS_POPUP` or `WS_CHILD`, visible, paints black with a red 10×10 square at client (20,30)), `void pump();`.
  - `class hr::platform::WindowCapture { public: bool capture(HWND window, core::Rect source); const std::uint32_t* pixels() const; core::Size size() const; };` — 32-bit top-down DIB, reallocated when the source size changes; pixels `0x??RRGGBB`.

- [ ] **Step 1: Write the helper and the failing test**

`tests/platform/test_window.h`:

```cpp
#pragma once

#include <windows.h>

#include <map>
#include <vector>

namespace hr::test {

struct ReceivedMessage {
    UINT msg;
    WPARAM wparam;
    LPARAM lparam;
};

// Every message received by windows made with make_window (test thread only).
inline std::map<HWND, std::vector<ReceivedMessage>>& received() {
    static std::map<HWND, std::vector<ReceivedMessage>> messages;
    return messages;
}

inline LRESULT CALLBACK recording_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
    received()[window].push_back({msg, wparam, lparam});
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        const HDC dc = BeginPaint(window, &ps);
        RECT client{};
        GetClientRect(window, &client);
        FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const RECT square{20, 30, 30, 40};
        const HBRUSH red = CreateSolidBrush(RGB(255, 0, 0));
        FillRect(dc, &square, red);
        DeleteObject(red);
        EndPaint(window, &ps);
        return 0;
    }
    return DefWindowProcA(window, msg, wparam, lparam);
}

// A visible test window: top-level popup when `parent` is null, else a child. Paints black with
// a red 10×10 square at client (20,30).
inline HWND make_window(int x, int y, int width, int height, HWND parent = nullptr) {
    static const bool registered = [] {
        WNDCLASSA wc{};
        wc.lpfnWndProc = recording_proc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.hCursor = LoadCursorA(nullptr, MAKEINTRESOURCEA(32512)); // IDC_ARROW
        wc.lpszClassName = "HrRecordingWindow";
        return RegisterClassA(&wc) != 0;
    }();
    (void)registered;
    const DWORD style = parent != nullptr ? (WS_CHILD | WS_VISIBLE) : (WS_POPUP | WS_VISIBLE);
    const HWND window = CreateWindowExA(0, "HrRecordingWindow", "", style, x, y, width, height,
                                        parent, nullptr, GetModuleHandleA(nullptr), nullptr);
    UpdateWindow(window);
    return window;
}

inline void pump() {
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

} // namespace hr::test
```

`tests/platform/window_capture_test.cpp`:

```cpp
#include "platform/window_capture.h"
#include "test_window.h"

#include <catch2/catch_test_macros.hpp>

using hr::platform::WindowCapture;

namespace {
std::uint32_t rgb_at(const WindowCapture& capture, int x, int y) {
    return capture.pixels()[y * capture.size().width + x] & 0xFFFFFF;
}
} // namespace

TEST_CASE("window_capture copies a window's client pixels", "[window_capture]") {
    const HWND window = hr::test::make_window(100, 100, 200, 150);
    hr::test::pump();
    WindowCapture capture;
    REQUIRE(capture.capture(window, {0, 0, 200, 150}));
    CHECK(capture.size() == hr::core::Size{200, 150});
    CHECK(rgb_at(capture, 25, 35) == 0xFF0000);
    CHECK(rgb_at(capture, 5, 5) == 0x000000);

    REQUIRE(capture.capture(window, {20, 30, 10, 10})); // a sub-rectangle resizes the buffer
    CHECK(capture.size() == hr::core::Size{10, 10});
    CHECK(rgb_at(capture, 0, 0) == 0xFF0000);
    DestroyWindow(window);
}

TEST_CASE("window_capture rejects an empty source", "[window_capture]") {
    const HWND window = hr::test::make_window(100, 100, 50, 50);
    WindowCapture capture;
    CHECK_FALSE(capture.capture(window, {0, 0, 0, 10}));
    DestroyWindow(window);
}

TEST_CASE("window_capture sees live drawing of a window covered by another window",
          "[window_capture]") {
    const HWND window = hr::test::make_window(100, 100, 200, 150);
    const HWND cover = hr::test::make_window(50, 50, 400, 300);
    SetWindowPos(cover, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    hr::test::pump();

    // Draw like the game does: straight to the window DC, outside WM_PAINT.
    const HDC dc = GetDC(window);
    const RECT square{100, 50, 110, 60};
    const HBRUSH blue = CreateSolidBrush(RGB(0, 0, 255));
    FillRect(dc, &square, blue);
    DeleteObject(blue);
    ReleaseDC(window, dc);
    GdiFlush();

    WindowCapture capture;
    REQUIRE(capture.capture(window, {0, 0, 200, 150}));
    CHECK(rgb_at(capture, 105, 55) == 0x0000FF);
    CHECK(rgb_at(capture, 25, 35) == 0xFF0000);
    DestroyWindow(cover);
    DestroyWindow(window);
}
```

Add `window_capture_test.cpp` to `add_executable(platform_tests ...)` in `tests/platform/CMakeLists.txt`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'platform/window_capture.h'`.

- [ ] **Step 3: Write the minimal implementation**

`src/platform/window_capture.h`:

```cpp
#pragma once

#include "core/geometry.h"

#include <windows.h>

#include <cstdint>

namespace hr::platform {

// Copies a region of a window's composed client area (via its window DC) into a 32-bit
// top-down DIB. Works for windows covered by others or hidden on-screen (A2 spec §2); NOT for
// windows off-screen, whose drawing GDI clips. Pixels are 0x??RRGGBB.
class WindowCapture {
public:
    WindowCapture() = default;
    ~WindowCapture();
    WindowCapture(const WindowCapture&) = delete;
    WindowCapture& operator=(const WindowCapture&) = delete;

    // `source` is in client coordinates of `window`. Returns false for an empty source or when
    // the DC or the copy fails; the previous pixels are then kept.
    bool capture(HWND window, core::Rect source);

    const std::uint32_t* pixels() const { return bits_; }
    core::Size size() const { return size_; }

private:
    bool ensure(core::Size size);
    void release();

    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    std::uint32_t* bits_ = nullptr;
    core::Size size_{};
};

} // namespace hr::platform
```

`src/platform/window_capture.cpp`:

```cpp
#include "platform/window_capture.h"

namespace hr::platform {

WindowCapture::~WindowCapture() { release(); }

void WindowCapture::release() {
    if (dc_ != nullptr) {
        SelectObject(dc_, previous_);
        DeleteDC(dc_);
    }
    if (bitmap_ != nullptr) {
        DeleteObject(bitmap_);
    }
    dc_ = nullptr;
    bitmap_ = nullptr;
    previous_ = nullptr;
    bits_ = nullptr;
    size_ = {};
}

bool WindowCapture::ensure(core::Size size) {
    if (dc_ != nullptr && size == size_) {
        return true;
    }
    release();
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = size.width;
    info.bmiHeader.biHeight = -size.height; // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    dc_ = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dc_ == nullptr || bitmap_ == nullptr) {
        release();
        return false;
    }
    previous_ = SelectObject(dc_, bitmap_);
    bits_ = static_cast<std::uint32_t*>(bits);
    size_ = size;
    return true;
}

bool WindowCapture::capture(HWND window, core::Rect source) {
    if (source.width <= 0 || source.height <= 0 || !ensure({source.width, source.height})) {
        return false;
    }
    const HDC window_dc = GetDC(window);
    if (window_dc == nullptr) {
        return false;
    }
    const BOOL copied = BitBlt(dc_, 0, 0, source.width, source.height, window_dc, source.x,
                               source.y, SRCCOPY);
    ReleaseDC(window, window_dc);
    GdiFlush();
    return copied != FALSE;
}

} // namespace hr::platform
```

In `src/platform/CMakeLists.txt` add `window_capture.cpp` to `hr_platform`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^window_capture$"`
Expected: `100% tests passed, 0 tests failed out of 3`.

- [ ] **Step 5: Format, commit, push**

Run Format, then:

```bash
git add src/platform tests/platform
git commit -m "platform: capture a window's composed client area" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

If CI reports the capture tests failing on the GitHub runner because its session has no composited desktop (all-black captures), add a skip guarded by exactly that observation — not a blanket skip.

---

### Task 6: Hiding the game window on-screen (core + platform), removing `park`

**Files:**
- Modify: `src/core/viewport.h`, `src/core/viewport.cpp`, `tests/core/viewport_test.cpp`
- Modify: `src/core/config.h`, `src/core/config.cpp`, `tests/core/config_test.cpp` (remove `park`)
- Create: `src/platform/hidden_window.h`, `src/platform/hidden_window.cpp`
- Modify: `src/platform/CMakeLists.txt`, `tests/platform/CMakeLists.txt`
- Test: `tests/platform/hidden_window_test.cpp`

**Interfaces:**
- Produces:
  - `Point hr::core::keep_inside(Rect window, Rect area);` — top-left position that puts `window` fully inside `area`, moving it as little as possible; aligned to the area's top-left when it is larger than the area.
  - `struct hr::platform::HiddenState { RECT window_rect{}; LONG_PTR ex_style = 0; bool hidden = false; };`
  - `HiddenState hide_window(HWND window, RECT area);` — adds `WS_EX_LAYERED | WS_EX_TRANSPARENT`, alpha 0, bottom of the z-order, position from `keep_inside`, no activation.
  - `void unhide_window(HWND window, const HiddenState& state);` — restores the extended style, moves to (0,0), shows; no-op if `!state.hidden`.
- Removes: `ParkMode`, `Config::park`, the `display.park` INI key (committed in Task 3).

- [ ] **Step 1: Write the failing tests**

Append to `tests/core/viewport_test.cpp`:

```cpp
TEST_CASE("keep_inside moves a window as little as possible to fit an area", "[viewport]") {
    const Rect area{0, 0, 1920, 1040};
    CHECK(keep_inside({100, 100, 656, 520}, area) == Point{100, 100});
    CHECK(keep_inside({-150, 100, 656, 520}, area) == Point{0, 100});
    CHECK(keep_inside({1800, 900, 656, 520}, area) == Point{1264, 520});
    CHECK(keep_inside({-32000, -32000, 656, 520}, area) == Point{0, 0});
    CHECK(keep_inside({50, 50, 4000, 3000}, area) == Point{0, 0});
    CHECK(keep_inside({1950, 10, 100, 100}, Rect{1920, 0, 1920, 1080}) == Point{1950, 10});
}
```

`tests/platform/hidden_window_test.cpp`:

```cpp
#include "platform/hidden_window.h"
#include "platform/window_capture.h"
#include "test_window.h"

#include <catch2/catch_test_macros.hpp>

using namespace hr::platform;

namespace {
RECT work_area() {
    RECT area{};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0);
    return area;
}
} // namespace

TEST_CASE("hide_window makes a window invisible, click-through and fully on-screen",
          "[hidden_window]") {
    const HWND window = hr::test::make_window(-150, 100, 200, 150);
    hr::test::pump();
    const RECT area = work_area();
    const HiddenState state = hide_window(window, area);
    CHECK(state.hidden);

    RECT r{};
    GetWindowRect(window, &r);
    CHECK(r.left >= area.left);
    CHECK(r.top >= area.top);
    CHECK(r.right <= area.right);
    CHECK(r.bottom <= area.bottom);
    CHECK(r.right - r.left == 200);

    const LONG_PTR ex = GetWindowLongPtrA(window, GWL_EXSTYLE);
    CHECK((ex & WS_EX_LAYERED) != 0);
    CHECK((ex & WS_EX_TRANSPARENT) != 0);
    BYTE alpha = 255;
    DWORD flags = 0;
    REQUIRE(GetLayeredWindowAttributes(window, nullptr, &alpha, &flags));
    CHECK(alpha == 0);

    unhide_window(window, state);
    CHECK((GetWindowLongPtrA(window, GWL_EXSTYLE) & (WS_EX_LAYERED | WS_EX_TRANSPARENT)) == 0);
    GetWindowRect(window, &r);
    CHECK(r.left == 0);
    CHECK(r.top == 0);
    CHECK(IsWindowVisible(window));
    DestroyWindow(window);
}

TEST_CASE("a hidden window under another window still delivers live drawing to the capture",
          "[hidden_window]") {
    const HWND window = hr::test::make_window(100, 100, 200, 150);
    hr::test::pump();
    const HiddenState state = hide_window(window, work_area());
    const HWND cover = hr::test::make_window(0, 0, 800, 600);
    hr::test::pump();

    const HDC dc = GetDC(window);
    const RECT square{100, 50, 110, 60};
    const HBRUSH blue = CreateSolidBrush(RGB(0, 0, 255));
    FillRect(dc, &square, blue);
    DeleteObject(blue);
    ReleaseDC(window, dc);
    GdiFlush();

    WindowCapture capture;
    REQUIRE(capture.capture(window, {0, 0, 200, 150}));
    CHECK((capture.pixels()[55 * 200 + 105] & 0xFFFFFF) == 0x0000FFu);
    unhide_window(window, state);
    DestroyWindow(cover);
    DestroyWindow(window);
}

TEST_CASE("unhide of a window that was never hidden does nothing", "[hidden_window]") {
    const HWND window = hr::test::make_window(100, 100, 50, 50);
    unhide_window(window, HiddenState{});
    RECT r{};
    GetWindowRect(window, &r);
    CHECK(r.left == 100);
    DestroyWindow(window);
}
```

Remove the `TEST_CASE("the park mode is read and defaults to off-screen", ...)` block from `tests/core/config_test.cpp` (the feature is removed; the remaining config tests, including the default-file round trip, guard the removal).

Add `hidden_window_test.cpp` to `platform_tests`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'platform/hidden_window.h'` and `'keep_inside': identifier not found`.

- [ ] **Step 3: Write the implementation and remove `park`**

Add to `src/core/viewport.h`:

```cpp
// Top-left position that puts `window` fully inside `area`, moving it as little as possible; a
// window larger than the area is aligned to the area's top-left.
Point keep_inside(Rect window, Rect area);
```

Add to `src/core/viewport.cpp`:

```cpp
Point keep_inside(Rect window, Rect area) {
    const auto axis = [](int pos, int size, int start, int length) {
        if (size >= length) {
            return start;
        }
        return std::clamp(pos, start, start + length - size);
    };
    return {axis(window.x, window.width, area.x, area.width),
            axis(window.y, window.height, area.y, area.height)};
}
```

`src/platform/hidden_window.h`:

```cpp
#pragma once

#include <windows.h>

namespace hr::platform {

// What hide_window changed, so unhide_window can undo it.
struct HiddenState {
    RECT window_rect{};
    LONG_PTR ex_style = 0;
    bool hidden = false;
};

// Makes `window` invisible (layered, alpha 0) and click-through, puts it at the bottom of the
// z-order and moves it (if needed) fully inside `area` (screen coordinates). It must stay
// on-screen: GDI clips drawing to the on-screen part of a window (A2 spec §2 correction).
HiddenState hide_window(HWND window, RECT area);

// The "never worse than vanilla" exit (spec §7): original extended style, window at (0,0), shown.
void unhide_window(HWND window, const HiddenState& state);

} // namespace hr::platform
```

`src/platform/hidden_window.cpp`:

```cpp
#include "platform/hidden_window.h"

#include "core/viewport.h"

namespace hr::platform {

HiddenState hide_window(HWND window, RECT area) {
    HiddenState state;
    GetWindowRect(window, &state.window_rect);
    state.ex_style = GetWindowLongPtrA(window, GWL_EXSTYLE);
    state.hidden = true;

    SetWindowLongPtrA(window, GWL_EXSTYLE, state.ex_style | WS_EX_LAYERED | WS_EX_TRANSPARENT);
    SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA);
    const RECT& r = state.window_rect;
    const core::Point pos =
        core::keep_inside({r.left, r.top, r.right - r.left, r.bottom - r.top},
                          {area.left, area.top, area.right - area.left, area.bottom - area.top});
    SetWindowPos(window, HWND_BOTTOM, pos.x, pos.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    return state;
}

void unhide_window(HWND window, const HiddenState& state) {
    if (!state.hidden) {
        return;
    }
    SetWindowLongPtrA(window, GWL_EXSTYLE, state.ex_style);
    SetWindowPos(window, nullptr, 0, 0, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

} // namespace hr::platform
```

Add `hidden_window.cpp` to `hr_platform`.

Remove `park`: in `src/core/config.h` delete `enum class ParkMode { Offscreen, Underneath };` and the `ParkMode park = ParkMode::Offscreen;` member; in `src/core/config.cpp` delete the `display.park` entry of `kFields` and the `park = offscreen ...` line of `default_config_ini()`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^(viewport|config|hidden_window)$"`
Expected: `100% tests passed, 0 tests failed out of 18` (7 viewport + 8 config + 3 hidden_window).

- [ ] **Step 5: Format and commit**

Run Format, then:

```bash
git add src/core src/platform tests/core tests/platform
git commit -m "platform: hide the game window on-screen; drop the park option" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Window subclassing (platform)

**Files:**
- Create: `src/platform/window_subclass.h`, `src/platform/window_subclass.cpp`
- Modify: `src/platform/CMakeLists.txt`, `tests/platform/CMakeLists.txt`
- Test: `tests/platform/window_subclass_test.cpp`

**Interfaces:**
- Consumes: `core::filter_activation`, `core::win::*` (Task 2), `platform::guarded` (A1).
- Produces: `class hr::platform::Subclass { public: using Handler = std::function<LRESULT(HWND, UINT, WPARAM, LPARAM, WNDPROC original)>; Subclass(HWND window, Handler handler); ~Subclass(); };` — installs via `SetWindowLongPtrA(GWLP_WNDPROC)` (allowed from any thread of the owning process), finds itself through a window property, guards the handler (exception → original procedure), restores the original procedure in the destructor.

- [ ] **Step 1: Write the failing test**

`tests/platform/window_subclass_test.cpp`:

```cpp
#include "core/input_map.h"
#include "platform/window_subclass.h"
#include "test_window.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

// The core mirror of Win32 constants must match <windows.h>.
static_assert(hr::core::win::kWmActivate == static_cast<decltype(hr::core::win::kWmActivate)>(WM_ACTIVATE));
static_assert(hr::core::win::kWmActivateApp == static_cast<decltype(hr::core::win::kWmActivateApp)>(WM_ACTIVATEAPP));
static_assert(hr::core::win::kWmKeyDown == static_cast<decltype(hr::core::win::kWmKeyDown)>(WM_KEYDOWN));
static_assert(hr::core::win::kWmKeyUp == static_cast<decltype(hr::core::win::kWmKeyUp)>(WM_KEYUP));
static_assert(hr::core::win::kWmChar == static_cast<decltype(hr::core::win::kWmChar)>(WM_CHAR));
static_assert(hr::core::win::kWmSysKeyDown == static_cast<decltype(hr::core::win::kWmSysKeyDown)>(WM_SYSKEYDOWN));
static_assert(hr::core::win::kWmSysKeyUp == static_cast<decltype(hr::core::win::kWmSysKeyUp)>(WM_SYSKEYUP));
static_assert(hr::core::win::kWmSysChar == static_cast<decltype(hr::core::win::kWmSysChar)>(WM_SYSCHAR));
static_assert(hr::core::win::kWmMouseMove == static_cast<decltype(hr::core::win::kWmMouseMove)>(WM_MOUSEMOVE));
static_assert(hr::core::win::kWmLButtonDown == static_cast<decltype(hr::core::win::kWmLButtonDown)>(WM_LBUTTONDOWN));
static_assert(hr::core::win::kWmMouseWheel == static_cast<decltype(hr::core::win::kWmMouseWheel)>(WM_MOUSEWHEEL));
static_assert(hr::core::win::kWmXButtonDblClk == static_cast<decltype(hr::core::win::kWmXButtonDblClk)>(WM_XBUTTONDBLCLK));
static_assert(hr::core::win::kVkLButton == static_cast<decltype(hr::core::win::kVkLButton)>(VK_LBUTTON));
static_assert(hr::core::win::kVkRButton == static_cast<decltype(hr::core::win::kVkRButton)>(VK_RBUTTON));
static_assert(hr::core::win::kVkMButton == static_cast<decltype(hr::core::win::kVkMButton)>(VK_MBUTTON));
static_assert(hr::core::win::kVkXButton1 == static_cast<decltype(hr::core::win::kVkXButton1)>(VK_XBUTTON1));
static_assert(hr::core::win::kVkXButton2 == static_cast<decltype(hr::core::win::kVkXButton2)>(VK_XBUTTON2));
static_assert(hr::core::win::kVkReturn == static_cast<decltype(hr::core::win::kVkReturn)>(VK_RETURN));
static_assert(hr::core::win::kWaInactive == static_cast<decltype(hr::core::win::kWaInactive)>(WA_INACTIVE));
static_assert(hr::core::win::kWaActive == static_cast<decltype(hr::core::win::kWaActive)>(WA_ACTIVE));

namespace {
WPARAM last_activate(HWND window) {
    const auto& messages = hr::test::received()[window];
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->msg == WM_ACTIVATE) {
            return it->wparam;
        }
    }
    return static_cast<WPARAM>(-1);
}
} // namespace

TEST_CASE("a subclass can rewrite messages and is removed again", "[window_subclass]") {
    const HWND window = hr::test::make_window(100, 100, 50, 50);
    {
        hr::platform::Subclass subclass(
            window, [](HWND w, UINT m, WPARAM wp, LPARAM lp, WNDPROC original) {
                return CallWindowProcA(original, w, m, hr::core::filter_activation(m, wp), lp);
            });
        SendMessageA(window, WM_ACTIVATE, WA_INACTIVE, 0);
        CHECK(last_activate(window) == WA_ACTIVE);
    }
    SendMessageA(window, WM_ACTIVATE, WA_INACTIVE, 0);
    CHECK(last_activate(window) == WA_INACTIVE);
    DestroyWindow(window);
}

TEST_CASE("an exception in the handler falls back to the original procedure",
          "[window_subclass]") {
    const HWND window = hr::test::make_window(100, 100, 50, 50);
    hr::platform::Subclass subclass(
        window, [](HWND, UINT, WPARAM, LPARAM, WNDPROC) -> LRESULT {
            throw std::runtime_error("handler failed");
        });
    SendMessageA(window, WM_ACTIVATE, WA_CLICKACTIVE, 0);
    CHECK(last_activate(window) == WA_CLICKACTIVE);
    DestroyWindow(window);
}
```

Add `window_subclass_test.cpp` to `platform_tests`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'platform/window_subclass.h'`.

- [ ] **Step 3: Write the minimal implementation**

`src/platform/window_subclass.h`:

```cpp
#pragma once

#include <windows.h>

#include <functional>

namespace hr::platform {

// Replaces a window's procedure (any thread of the owning process may do this). The handler
// receives the original procedure and decides whether and how to call it. Exceptions from the
// handler are logged and the original procedure handles the message instead.
class Subclass {
public:
    using Handler = std::function<LRESULT(HWND, UINT, WPARAM, LPARAM, WNDPROC original)>;

    Subclass(HWND window, Handler handler);
    ~Subclass();
    Subclass(const Subclass&) = delete;
    Subclass& operator=(const Subclass&) = delete;

private:
    static LRESULT CALLBACK thunk(HWND window, UINT msg, WPARAM wparam, LPARAM lparam);

    HWND window_;
    Handler handler_;
    WNDPROC original_ = nullptr;
};

} // namespace hr::platform
```

`src/platform/window_subclass.cpp`:

```cpp
#include "platform/window_subclass.h"

#include "platform/guard.h"

namespace hr::platform {
namespace {
constexpr char kProperty[] = "hr.subclass";
} // namespace

Subclass::Subclass(HWND window, Handler handler) : window_(window), handler_(std::move(handler)) {
    // Property first, so the thunk can always find us once it is installed.
    SetPropA(window_, kProperty, this);
    original_ = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrA(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Subclass::thunk)));
}

Subclass::~Subclass() {
    if (IsWindow(window_)) {
        SetWindowLongPtrA(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_));
        RemovePropA(window_, kProperty);
    }
}

LRESULT CALLBACK Subclass::thunk(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* const self = static_cast<Subclass*>(GetPropA(window, kProperty));
    if (self == nullptr) {
        return DefWindowProcA(window, msg, wparam, lparam);
    }
    return guarded(
        "subclass",
        [&] { return self->handler_(window, msg, wparam, lparam, self->original_); },
        [&] { return CallWindowProcA(self->original_, window, msg, wparam, lparam); });
}

} // namespace hr::platform
```

Add `window_subclass.cpp` to `hr_platform`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^window_subclass$"`
Expected: `100% tests passed, 0 tests failed out of 2` (and the `static_assert`s compiled).

- [ ] **Step 5: Format and commit**

Run Format, then:

```bash
git add src/platform tests/platform
git commit -m "platform: guarded window subclassing" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 8: Delivering input to the game's child windows (platform)

**Files:**
- Create: `src/platform/child_input.h`, `src/platform/child_input.cpp`
- Modify: `src/platform/CMakeLists.txt`, `tests/platform/CMakeLists.txt`
- Test: `tests/platform/child_input_test.cpp`

**Interfaces:**
- Produces:
  - `struct hr::platform::ChildHit { HWND window = nullptr; POINT point{}; };` (`point` in `window` client coordinates)
  - `ChildHit deepest_child_at(HWND root, POINT root_client);` — skips invisible, disabled and transparent children.
  - `void post_mouse(HWND root, UINT msg, WPARAM wparam, POINT root_client);` — to the capture window of `root`'s thread if one is set (coordinates relative to it), else to the deepest child; `WM_MOUSEWHEEL` carries screen coordinates.
  - `void post_key(HWND root, UINT msg, WPARAM wparam, LPARAM lparam);` — to the focus window of `root`'s thread, else `root`.

- [ ] **Step 1: Write the failing test**

`tests/platform/child_input_test.cpp`:

```cpp
#include "platform/child_input.h"
#include "test_window.h"

#include <catch2/catch_test_macros.hpp>

using namespace hr::platform;

namespace {

struct Tree {
    HWND root = hr::test::make_window(100, 100, 300, 200);
    HWND a = hr::test::make_window(50, 40, 100, 80, root);
    HWND b = hr::test::make_window(10, 10, 30, 30, a);
    Tree() { hr::test::pump(); }
    ~Tree() { DestroyWindow(root); }
};

bool got(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
    for (const auto& m : hr::test::received()[window]) {
        if (m.msg == msg && m.wparam == wparam && m.lparam == lparam) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("deepest_child_at descends to the innermost visible child", "[child_input]") {
    Tree tree;
    auto hit = deepest_child_at(tree.root, {65, 55});
    CHECK(hit.window == tree.b);
    CHECK(hit.point.x == 5);
    CHECK(hit.point.y == 5);

    hit = deepest_child_at(tree.root, {55, 45});
    CHECK(hit.window == tree.a);
    CHECK(hit.point.x == 5);

    hit = deepest_child_at(tree.root, {5, 5});
    CHECK(hit.window == tree.root);

    ShowWindow(tree.b, SW_HIDE);
    CHECK(deepest_child_at(tree.root, {65, 55}).window == tree.a);
}

TEST_CASE("post_mouse delivers child-relative coordinates to the child under the point",
          "[child_input]") {
    Tree tree;
    post_mouse(tree.root, WM_LBUTTONDOWN, MK_LBUTTON, {65, 55});
    hr::test::pump();
    CHECK(got(tree.b, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5)));
}

TEST_CASE("post_mouse goes to the window holding mouse capture", "[child_input]") {
    Tree tree;
    SetCapture(tree.a);
    post_mouse(tree.root, WM_MOUSEMOVE, 0, {65, 55});
    hr::test::pump();
    ReleaseCapture();
    CHECK(got(tree.a, WM_MOUSEMOVE, 0, MAKELPARAM(15, 15)));
}

TEST_CASE("post_key goes to the window with keyboard focus", "[child_input]") {
    Tree tree;
    SetFocus(tree.b);
    post_key(tree.root, WM_CHAR, 'x', 1);
    hr::test::pump();
    CHECK(got(tree.b, WM_CHAR, 'x', 1));
}
```

Add `child_input_test.cpp` to `platform_tests`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'platform/child_input.h'`.

- [ ] **Step 3: Write the minimal implementation**

`src/platform/child_input.h`:

```cpp
#pragma once

#include <windows.h>

namespace hr::platform {

struct ChildHit {
    HWND window = nullptr;
    POINT point{}; // in `window` client coordinates
};

// The innermost visible, enabled, non-transparent window under `root_client` (root client
// coordinates), which may be `root` itself.
ChildHit deepest_child_at(HWND root, POINT root_client);

// Posts a mouse message as if the real cursor were at `root_client`: to the window holding mouse
// capture on root's thread if there is one, else to the deepest child (needed by USER32's
// standard controls, A2 spec §5.2). WM_MOUSEWHEEL carries screen coordinates.
void post_mouse(HWND root, UINT msg, WPARAM wparam, POINT root_client);

// Posts a keyboard message to the focus window of root's thread, else to `root`.
void post_key(HWND root, UINT msg, WPARAM wparam, LPARAM lparam);

} // namespace hr::platform
```

`src/platform/child_input.cpp`:

```cpp
#include "platform/child_input.h"

namespace hr::platform {
namespace {

HWND thread_window(HWND root, bool capture) {
    GUITHREADINFO info{};
    info.cbSize = sizeof info;
    if (!GetGUIThreadInfo(GetWindowThreadProcessId(root, nullptr), &info)) {
        return nullptr;
    }
    return capture ? info.hwndCapture : info.hwndFocus;
}

LPARAM pack(POINT p) { return MAKELPARAM(static_cast<WORD>(p.x), static_cast<WORD>(p.y)); }

} // namespace

ChildHit deepest_child_at(HWND root, POINT root_client) {
    ChildHit hit{root, root_client};
    for (;;) {
        const HWND child = ChildWindowFromPointEx(
            hit.window, hit.point, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
        if (child == nullptr || child == hit.window) {
            return hit;
        }
        MapWindowPoints(hit.window, child, &hit.point, 1);
        hit.window = child;
    }
}

void post_mouse(HWND root, UINT msg, WPARAM wparam, POINT root_client) {
    if (msg == WM_MOUSEWHEEL) {
        POINT screen = root_client;
        ClientToScreen(root, &screen);
        PostMessageA(deepest_child_at(root, root_client).window, msg, wparam, pack(screen));
        return;
    }
    if (const HWND captured = thread_window(root, true)) {
        POINT point = root_client;
        MapWindowPoints(root, captured, &point, 1);
        PostMessageA(captured, msg, wparam, pack(point));
        return;
    }
    const ChildHit hit = deepest_child_at(root, root_client);
    PostMessageA(hit.window, msg, wparam, pack(hit.point));
}

void post_key(HWND root, UINT msg, WPARAM wparam, LPARAM lparam) {
    const HWND focus = thread_window(root, false);
    PostMessageA(focus != nullptr ? focus : root, msg, wparam, lparam);
}

} // namespace hr::platform
```

Add `child_input.cpp` to `hr_platform`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^child_input$"`
Expected: `100% tests passed, 0 tests failed out of 4`.

- [ ] **Step 5: Format, commit, push**

Run Format, then:

```bash
git add src/platform tests/platform
git commit -m "platform: deliver mouse and keyboard input to game child windows" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

---

### Task 9: Direct3D 11 presenter (platform)

**Files:**
- Create: `src/platform/presenter_d3d11.h`, `src/platform/presenter_d3d11.cpp`
- Modify: `src/platform/CMakeLists.txt` (sources + `d3d11 dxgi`), `tests/platform/CMakeLists.txt`
- Test: `tests/platform/presenter_d3d11_test.cpp`

**Interfaces:**
- Consumes: `core::Size`, `core::Rect`.
- Produces: `class hr::platform::D3DPresenter`:
  - `enum class Driver { Hardware, Warp };`
  - `static std::unique_ptr<D3DPresenter> create(HWND window, core::Size size, bool allow_hardware, std::string& error);` — hardware first (if allowed), then WARP; flip-model swap chain `B8G8R8A8_UNORM`, 2 buffers; DXGI's own Alt+Enter disabled.
  - `Driver driver() const;`
  - `void render(const std::uint32_t* bgra, core::Size frame, core::Rect dest);` — clears to black, copies the frame 1:1 into `dest`, clipped to the back buffer.
  - `void present(bool vsync);`
  - `std::vector<std::uint32_t> read_back();` — current back buffer (tests).

- [ ] **Step 1: Write the failing test**

`tests/platform/presenter_d3d11_test.cpp`:

```cpp
#include "platform/presenter_d3d11.h"
#include "test_window.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using hr::platform::D3DPresenter;

namespace {

std::vector<std::uint32_t> pattern(int width, int height) {
    std::vector<std::uint32_t> pixels(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            pixels[static_cast<size_t>(y) * width + x] =
                (static_cast<std::uint32_t>(x * 16) << 16) |
                (static_cast<std::uint32_t>(y * 32) << 8) | 0x40u;
        }
    }
    return pixels;
}

std::unique_ptr<D3DPresenter> make(HWND window) {
    std::string error;
    auto presenter = D3DPresenter::create(window, {64, 48}, /*allow_hardware=*/false, error);
    INFO(error);
    REQUIRE(presenter);
    return presenter;
}

} // namespace

TEST_CASE("the presenter copies a frame 1:1 into the destination on black",
          "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    CHECK(presenter->driver() == D3DPresenter::Driver::Warp);

    const auto frame = pattern(16, 8);
    presenter->render(frame.data(), {16, 8}, {10, 5, 16, 8});
    const auto pixels = presenter->read_back();
    REQUIRE(pixels.size() == 64u * 48u);

    int mismatches = 0;
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 64; ++x) {
            const bool inside = x >= 10 && x < 26 && y >= 5 && y < 13;
            const std::uint32_t expected =
                inside ? frame[static_cast<size_t>(y - 5) * 16 + (x - 10)] : 0u;
            mismatches += (pixels[static_cast<size_t>(y) * 64 + x] & 0xFFFFFF) != expected;
        }
    }
    CHECK(mismatches == 0);
    presenter->present(false);
    DestroyWindow(window);
}

TEST_CASE("frames partly outside the back buffer are clipped", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const auto frame = pattern(16, 8);
    presenter->render(frame.data(), {16, 8}, {-4, -2, 16, 8});
    const auto pixels = presenter->read_back();
    CHECK((pixels[0] & 0xFFFFFF) == frame[2 * 16 + 4]);
    CHECK((pixels[5 * 64 + 11] & 0xFFFFFF) == frame[7 * 16 + 15]);
    CHECK((pixels[0 * 64 + 12] & 0xFFFFFF) == 0u);
    DestroyWindow(window);
}

TEST_CASE("rendering without a frame just clears to black", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    presenter->render(nullptr, {0, 0}, {0, 0, 0, 0});
    const auto pixels = presenter->read_back();
    CHECK((pixels[10 * 64 + 10] & 0xFFFFFF) == 0u);
    DestroyWindow(window);
}
```

Add `presenter_d3d11_test.cpp` to `platform_tests`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'platform/presenter_d3d11.h'`.

- [ ] **Step 3: Write the minimal implementation**

`src/platform/presenter_d3d11.h`:

```cpp
#pragma once

#include "core/geometry.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <windows.h>
#include <wrl/client.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hr::platform {

// Shows captured frames in a window through a flip-model D3D11 swap chain. A2.1 copies 1:1; the
// scaling shader arrives with A2.2.
class D3DPresenter {
public:
    enum class Driver { Hardware, Warp };

    // Hardware device first (when allowed), then WARP. nullptr + `error` if neither works.
    static std::unique_ptr<D3DPresenter> create(HWND window, core::Size size, bool allow_hardware,
                                                std::string& error);

    Driver driver() const { return driver_; }

    // Clears the back buffer to black and copies the BGRA frame 1:1 to `dest`, clipped.
    void render(const std::uint32_t* bgra, core::Size frame, core::Rect dest);
    void present(bool vsync);

    // The current back buffer as 0x??RRGGBB pixels (tests).
    std::vector<std::uint32_t> read_back();

private:
    D3DPresenter() = default;
    bool ensure_frame_texture(core::Size frame);

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11DeviceContext> context_;
    Microsoft::WRL::ComPtr<IDXGISwapChain1> swap_chain_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> back_buffer_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> target_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> frame_;
    core::Size frame_size_{};
    core::Size size_{};
    Driver driver_ = Driver::Warp;
};

} // namespace hr::platform
```

`src/platform/presenter_d3d11.cpp`:

```cpp
#include "platform/presenter_d3d11.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace hr::platform {

using Microsoft::WRL::ComPtr;

namespace {
std::string hresult_text(const char* what, HRESULT hr) {
    char text[96];
    std::snprintf(text, sizeof text, "%s failed (0x%08lX)", what, static_cast<unsigned long>(hr));
    return text;
}
} // namespace

std::unique_ptr<D3DPresenter> D3DPresenter::create(HWND window, core::Size size,
                                                   bool allow_hardware, std::string& error) {
    std::unique_ptr<D3DPresenter> p(new D3DPresenter());
    HRESULT hr = E_FAIL;
    for (const D3D_DRIVER_TYPE type : {D3D_DRIVER_TYPE_HARDWARE, D3D_DRIVER_TYPE_WARP}) {
        if (type == D3D_DRIVER_TYPE_HARDWARE && !allow_hardware) {
            continue;
        }
        hr = D3D11CreateDevice(nullptr, type, nullptr, D3D11_CREATE_DEVICE_BGRA_SUPPORT, nullptr,
                               0, D3D11_SDK_VERSION, &p->device_, nullptr, &p->context_);
        if (SUCCEEDED(hr)) {
            p->driver_ = type == D3D_DRIVER_TYPE_HARDWARE ? Driver::Hardware : Driver::Warp;
            break;
        }
    }
    if (FAILED(hr)) {
        error = hresult_text("D3D11CreateDevice", hr);
        return nullptr;
    }

    ComPtr<IDXGIDevice> dxgi_device;
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory2> factory;
    if (FAILED(hr = p->device_.As(&dxgi_device)) || FAILED(hr = dxgi_device->GetAdapter(&adapter)) ||
        FAILED(hr = adapter->GetParent(IID_PPV_ARGS(&factory)))) {
        error = hresult_text("DXGI factory lookup", hr);
        return nullptr;
    }

    DXGI_SWAP_CHAIN_DESC1 desc{};
    desc.Width = static_cast<UINT>(size.width);
    desc.Height = static_cast<UINT>(size.height);
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    desc.BufferCount = 2;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    if (FAILED(hr = factory->CreateSwapChainForHwnd(p->device_.Get(), window, &desc, nullptr,
                                                    nullptr, &p->swap_chain_))) {
        error = hresult_text("CreateSwapChainForHwnd", hr);
        return nullptr;
    }
    factory->MakeWindowAssociation(window, DXGI_MWA_NO_ALT_ENTER); // Alt+Enter is ours (A2.2)

    if (FAILED(hr = p->swap_chain_->GetBuffer(0, IID_PPV_ARGS(&p->back_buffer_))) ||
        FAILED(hr = p->device_->CreateRenderTargetView(p->back_buffer_.Get(), nullptr,
                                                       &p->target_))) {
        error = hresult_text("back buffer setup", hr);
        return nullptr;
    }
    p->size_ = size;
    return p;
}

bool D3DPresenter::ensure_frame_texture(core::Size frame) {
    if (frame_ && frame == frame_size_) {
        return true;
    }
    frame_.Reset();
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(frame.width);
    desc.Height = static_cast<UINT>(frame.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &frame_))) {
        return false;
    }
    frame_size_ = frame;
    return true;
}

void D3DPresenter::render(const std::uint32_t* bgra, core::Size frame, core::Rect dest) {
    const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    context_->ClearRenderTargetView(target_.Get(), black);
    if (bgra == nullptr || frame.width <= 0 || frame.height <= 0 || !ensure_frame_texture(frame)) {
        return;
    }
    context_->UpdateSubresource(frame_.Get(), 0, nullptr, bgra,
                                static_cast<UINT>(frame.width) * 4, 0);

    const int left = std::max(dest.x, 0);
    const int top = std::max(dest.y, 0);
    const int right = std::min(dest.x + std::min(dest.width, frame.width), size_.width);
    const int bottom = std::min(dest.y + std::min(dest.height, frame.height), size_.height);
    if (right <= left || bottom <= top) {
        return;
    }
    const D3D11_BOX box{static_cast<UINT>(left - dest.x), static_cast<UINT>(top - dest.y), 0,
                        static_cast<UINT>(right - dest.x), static_cast<UINT>(bottom - dest.y), 1};
    context_->CopySubresourceRegion(back_buffer_.Get(), 0, static_cast<UINT>(left),
                                    static_cast<UINT>(top), 0, frame_.Get(), 0, &box);
}

void D3DPresenter::present(bool vsync) { swap_chain_->Present(vsync ? 1 : 0, 0); }

std::vector<std::uint32_t> D3DPresenter::read_back() {
    D3D11_TEXTURE2D_DESC desc{};
    back_buffer_->GetDesc(&desc);
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    desc.MiscFlags = 0;
    ComPtr<ID3D11Texture2D> staging;
    if (FAILED(device_->CreateTexture2D(&desc, nullptr, &staging))) {
        return {};
    }
    context_->CopyResource(staging.Get(), back_buffer_.Get());
    D3D11_MAPPED_SUBRESOURCE mapped{};
    if (FAILED(context_->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapped))) {
        return {};
    }
    std::vector<std::uint32_t> pixels(static_cast<size_t>(desc.Width) * desc.Height);
    for (UINT y = 0; y < desc.Height; ++y) {
        std::memcpy(&pixels[static_cast<size_t>(y) * desc.Width],
                    static_cast<const BYTE*>(mapped.pData) + static_cast<size_t>(y) * mapped.RowPitch,
                    static_cast<size_t>(desc.Width) * 4);
    }
    context_->Unmap(staging.Get(), 0);
    return pixels;
}

} // namespace hr::platform
```

In `src/platform/CMakeLists.txt` add `presenter_d3d11.cpp` to `hr_platform` and change the link line to:

```cmake
target_link_libraries(hr_platform PUBLIC hr_core dwmapi d3d11 dxgi PRIVATE hr_warnings)
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^presenter_d3d11$"`
Expected: `100% tests passed, 0 tests failed out of 3`.

- [ ] **Step 5: Format, commit, push**

Run Format, then:

```bash
git add src/platform tests/platform
git commit -m "platform: Direct3D 11 presenter with 1:1 frame copy" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

---

### Task 10: The presenter window (platform)

**Files:**
- Create: `src/platform/presenter_window.h`, `src/platform/presenter_window.cpp`
- Modify: `src/platform/CMakeLists.txt`, `tests/platform/CMakeLists.txt`
- Test: `tests/platform/presenter_window_test.cpp`

**Interfaces:**
- Consumes: `core::Size`.
- Produces:
  - `class hr::platform::PresenterInput { public: virtual ~PresenterInput() = default; virtual void on_mouse(UINT msg, WPARAM wparam, POINT client) = 0; virtual void on_key(UINT msg, WPARAM wparam, LPARAM lparam) = 0; virtual void on_close() = 0; virtual HCURSOR cursor() = 0; };`
  - `class hr::platform::PresenterWindow { public: PresenterWindow(PresenterInput& input, core::Size client, const char* title); ~PresenterWindow(); HWND hwnd() const; void show(); };` — fixed-size overlapped window (no resize/maximise in A2.1), client exactly `client`; mouse messages → `on_mouse` (wheel converted to client coordinates); key messages → `on_key` (system keys also go to `DefWindowProc`, so Alt+F4 still works); `WM_CLOSE` → `on_close` (window not destroyed); `WM_SETCURSOR` in the client area sets `input.cursor()` (arrow when null).

- [ ] **Step 1: Write the failing test**

`tests/platform/presenter_window_test.cpp`:

```cpp
#include "platform/presenter_window.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace {

struct FakeInput final : hr::platform::PresenterInput {
    struct Mouse {
        UINT msg;
        POINT client;
    };
    std::vector<Mouse> mouse;
    std::vector<UINT> keys;
    bool closed = false;

    void on_mouse(UINT msg, WPARAM, POINT client) override { mouse.push_back({msg, client}); }
    void on_key(UINT msg, WPARAM, LPARAM) override { keys.push_back(msg); }
    void on_close() override { closed = true; }
    HCURSOR cursor() override { return nullptr; }
};

} // namespace

TEST_CASE("the presenter window has exactly the requested client size", "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    REQUIRE(window.hwnd() != nullptr);
    RECT client{};
    GetClientRect(window.hwnd(), &client);
    CHECK(client.right == 640);
    CHECK(client.bottom == 480);
}

TEST_CASE("the presenter window reports mouse, keyboard and close", "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");

    SendMessageA(window.hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(10, 20));
    SendMessageA(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(11, 21));
    REQUIRE(input.mouse.size() == 2);
    CHECK(input.mouse[0].msg == WM_MOUSEMOVE);
    CHECK(input.mouse[0].client.x == 10);
    CHECK(input.mouse[0].client.y == 20);
    CHECK(input.mouse[1].msg == WM_LBUTTONDOWN);

    SendMessageA(window.hwnd(), WM_KEYDOWN, 'A', 0);
    SendMessageA(window.hwnd(), WM_CHAR, 'a', 0);
    CHECK(input.keys == std::vector<UINT>{WM_KEYDOWN, WM_CHAR});

    SendMessageA(window.hwnd(), WM_CLOSE, 0, 0);
    CHECK(input.closed);
    CHECK(IsWindow(window.hwnd()));
}

TEST_CASE("mouse wheel positions arrive in client coordinates", "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    POINT screen{30, 40};
    ClientToScreen(window.hwnd(), &screen);
    SendMessageA(window.hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA),
                 MAKELPARAM(static_cast<WORD>(screen.x), static_cast<WORD>(screen.y)));
    REQUIRE(input.mouse.size() == 1);
    CHECK(input.mouse[0].client.x == 30);
    CHECK(input.mouse[0].client.y == 40);
}
```

Add `presenter_window_test.cpp` to `platform_tests`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug`
Expected: FAIL with `Cannot open include file: 'platform/presenter_window.h'`.

- [ ] **Step 3: Write the minimal implementation**

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
    virtual HCURSOR cursor() = 0; // nullptr = standard arrow
};

// Our visible window. A2.1: fixed client size, no resizing.
class PresenterWindow {
public:
    PresenterWindow(PresenterInput& input, core::Size client, const char* title);
    ~PresenterWindow();
    PresenterWindow(const PresenterWindow&) = delete;
    PresenterWindow& operator=(const PresenterWindow&) = delete;

    HWND hwnd() const { return hwnd_; }
    void show();

private:
    static LRESULT CALLBACK proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam);

    PresenterInput& input_;
    HWND hwnd_ = nullptr;
};

} // namespace hr::platform
```

`src/platform/presenter_window.cpp`:

```cpp
#include "platform/presenter_window.h"

#include <windowsx.h>

namespace hr::platform {
namespace {
constexpr char kClassName[] = "HornedRatPresenter";
constexpr DWORD kStyle = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
} // namespace

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
    RECT frame{0, 0, client.width, client.height};
    AdjustWindowRect(&frame, kStyle, FALSE);
    hwnd_ = CreateWindowExA(0, kClassName, title, kStyle, CW_USEDEFAULT, CW_USEDEFAULT,
                            frame.right - frame.left, frame.bottom - frame.top, nullptr, nullptr,
                            GetModuleHandleA(nullptr), this);
}

PresenterWindow::~PresenterWindow() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
    }
}

void PresenterWindow::show() {
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

LRESULT CALLBACK PresenterWindow::proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTA*>(lparam);
        SetWindowLongPtrA(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* const self = reinterpret_cast<PresenterWindow*>(GetWindowLongPtrA(window, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcA(window, msg, wparam, lparam);
    }
    switch (msg) {
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(window, &point);
        self->input_.on_mouse(msg, wparam, point);
        return 0;
    }
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
        self->input_.on_mouse(msg, wparam, POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        return (msg >= WM_XBUTTONDOWN && msg <= WM_XBUTTONDBLCLK) ? TRUE : 0;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
        self->input_.on_key(msg, wparam, lparam);
        return 0;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_SYSCHAR:
        self->input_.on_key(msg, wparam, lparam);
        break; // DefWindowProc keeps Alt+F4 and the system menu working
    case WM_CLOSE:
        self->input_.on_close();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            const HCURSOR cursor = self->input_.cursor();
            SetCursor(cursor != nullptr ? cursor : LoadCursorA(nullptr, MAKEINTRESOURCEA(32512)));
            return TRUE;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    default:
        break;
    }
    return DefWindowProcA(window, msg, wparam, lparam);
}

} // namespace hr::platform
```

Add `presenter_window.cpp` to `hr_platform`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug -L "^presenter_window$"`
Expected: `100% tests passed, 0 tests failed out of 3`.

- [ ] **Step 5: Format and commit**

Run Format, then:

```bash
git add src/platform tests/platform
git commit -m "platform: presenter window reporting input" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 11: Brightness probe for the fade check (tool)

**Files:**
- Create: `tools/luma_probe/CMakeLists.txt`, `tools/luma_probe/luma_probe.cpp`
- Modify: root `CMakeLists.txt`

**Interfaces:**
- Consumes: `platform::WindowCapture` (Task 5), `core::mean_luma` (Task 4).
- Produces: `bin/luma_probe.exe <seconds> <output.csv>` — records `t,luma` of the game's main window (`Glue`) every ~5 ms; works for vanilla and with A2 (window hidden on-screen).

The probe is glue around two tested units, so its check is a run: before the game is running it must fail cleanly; that is the red step.

- [ ] **Step 1: Write the tool**

`tools/luma_probe/luma_probe.cpp`:

```cpp
// Dev tool for the A2.1 fade check: records the mean brightness of the game's composed picture.
//   luma_probe <seconds> <output.csv>
// Works for vanilla (window on-screen) and with A2 (window hidden on-screen). Never commit its output.
#include "core/luma.h"
#include "platform/window_capture.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fputs("usage: luma_probe <seconds> <output.csv>\n", stderr);
        return 2;
    }
    const HWND game = FindWindowA("Glue", nullptr);
    if (game == nullptr) {
        std::fputs("game window (class 'Glue') not found\n", stderr);
        return 1;
    }
    FILE* out = nullptr;
    if (fopen_s(&out, argv[2], "w") != 0 || out == nullptr) {
        std::fputs("cannot open output file\n", stderr);
        return 1;
    }
    std::fputs("t,luma\n", out);
    hr::platform::WindowCapture capture;
    const auto start = std::chrono::steady_clock::now();
    const double seconds = std::atof(argv[1]);
    int samples = 0;
    for (;;) {
        const double t =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (t >= seconds) {
            break;
        }
        RECT client{};
        GetClientRect(game, &client);
        if (capture.capture(game, {0, 0, client.right, client.bottom})) {
            std::fprintf(out, "%.4f,%.2f\n", t,
                         hr::core::mean_luma(capture.pixels(), capture.size().width,
                                             capture.size().height));
            ++samples;
        }
        Sleep(5);
    }
    std::fclose(out);
    std::printf("%d samples written to %s\n", samples, argv[2]);
    return 0;
}
```

`tools/luma_probe/CMakeLists.txt`:

```cmake
add_executable(luma_probe luma_probe.cpp)
target_link_libraries(luma_probe PRIVATE hr_platform hr_warnings)
```

In the root `CMakeLists.txt`, add after `add_subdirectory(src/wing32)` (inside the `if(NOT HR_CORE_ONLY)` block):

```cmake
  add_subdirectory(tools/luma_probe)
```

- [ ] **Step 2: Red — run it without the game**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ./build/x86-debug/bin/luma_probe.exe 1 $env:TEMP/probe.csv; $LASTEXITCODE`
Expected: `game window (class 'Glue') not found`, exit code `1`.

- [ ] **Step 3: Green — deferred to the real game**

The probe only finds the game's main window class, so its green run is Task 13 Step 4 (it must
write a few thousand samples there). Its two building blocks are already covered by the
`luma` and `window_capture` tests. Note this in the PR description.

- [ ] **Step 4: Format and commit**

Run Format, then:

```bash
git add tools/luma_probe CMakeLists.txt
git commit -m "tools: brightness probe for the fade check" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 12: Wire the presenter into the proxy (wing32)

**Files:**
- Create: `src/wing32/a2.h`, `src/wing32/a2.cpp`
- Modify: `src/wing32/hooks.cpp`, `src/wing32/CMakeLists.txt`

**Interfaces:**
- Consumes: everything from Tasks 1–10 (`hide_window`/`unhide_window` from Task 6); `wing32::settings()` (A1).
- Produces (namespace `hr::wing32::a2`):
  - `void on_window_created(HWND window, HWND parent, DWORD style);`
  - `bool active();`
  - `void fake_cursor_pos(POINT& out);`
  - `bool mask_button(int vk);`
  - `void set_game_cursor(HCURSOR cursor);`

This is glue: its pure parts are already tested (Tasks 1–3), its building blocks too (Tasks 5–10). The automated check here is that the whole suite stays green — in particular `proxy` and `proxy_init`, which load the DLL in a non-game host where nothing of A2 must start. The behaviour itself is verified in Task 13.

- [ ] **Step 1: Write `a2.h`**

`src/wing32/a2.h`:

```cpp
#pragma once

#include <windows.h>

// A2 presenter (spec 2026-10-05-stage-a2-presenter-design.md). Only ever active inside WHSHR.EXE:
// everything is triggered from game-module hooks.
namespace hr::wing32::a2 {

// After every successful CreateWindowExA in a game module. Starts the presenter thread for the
// first top-level window (the game's main window, A1 finding).
void on_window_created(HWND window, HWND parent, DWORD style);

// True while the presenter shows the game and its main window is hidden.
bool active();

// GetCursorPos replacement while active: the virtual cursor in the hidden window's screen space.
void fake_cursor_pos(POINT& out);

// GetAsyncKeyState while active: true if a mouse button must be reported as "up".
bool mask_button(int vk);

// SetCursor: the game's current cursor, shown by the presenter.
void set_game_cursor(HCURSOR cursor);

} // namespace hr::wing32::a2
```

- [ ] **Step 2: Write `a2.cpp`**

`src/wing32/a2.cpp`:

```cpp
#include "wing32/a2.h"

#include "core/content_rect.h"
#include "core/input_map.h"
#include "core/viewport.h"
#include "platform/child_input.h"
#include "platform/log.h"
#include "platform/hidden_window.h"
#include "platform/presenter_d3d11.h"
#include "platform/presenter_window.h"
#include "platform/window_capture.h"
#include "platform/window_subclass.h"
#include "wing32/runtime.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <optional>
#include <string>
#include <thread>

namespace hr::wing32::a2 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr core::Size kPresenterSize{640, 480};

std::atomic<HWND> g_glue{nullptr};
std::atomic<HWND> g_presenter{nullptr};
std::atomic<bool> g_started{false};
std::atomic<bool> g_active{false};
std::atomic<bool> g_stop{false};
std::atomic<HCURSOR> g_cursor{nullptr};
std::atomic<int> g_cursor_x{0};
std::atomic<int> g_cursor_y{0};
std::atomic<int> g_content_x{0};
std::atomic<int> g_content_y{0};
std::atomic<int> g_content_w{640};
std::atomic<int> g_content_h{480};
std::atomic<int> g_hidden_x{0}; // hidden position, enforced while active
std::atomic<int> g_hidden_y{0};

core::Rect content_rect() {
    return {g_content_x.load(), g_content_y.load(), g_content_w.load(), g_content_h.load()};
}

core::Rect find_content_rect(HWND glue) {
    RECT client{};
    GetClientRect(glue, &client);
    std::optional<core::Rect> child;
    if (const HWND screen = FindWindowExA(glue, nullptr, "BitmapScrn", nullptr)) {
        RECT r{};
        GetWindowRect(screen, &r);
        MapWindowPoints(HWND_DESKTOP, glue, reinterpret_cast<POINT*>(&r), 2);
        child = core::Rect{r.left, r.top, r.right - r.left, r.bottom - r.top};
    }
    return core::choose_content_rect({client.right, client.bottom}, child);
}

void store_content_rect(core::Rect r) {
    g_content_x = r.x;
    g_content_y = r.y;
    g_content_w = r.width;
    g_content_h = r.height;
}

// Runs on the game thread for every message of the main window (A2 spec §5.4).
LRESULT glue_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam, WNDPROC original) {
    if (g_active) {
        if (msg == WM_ACTIVATE || msg == WM_ACTIVATEAPP) {
            wparam = core::filter_activation(msg, wparam);
        } else if (msg == WM_WINDOWPOSCHANGING) {
            auto* const pos = reinterpret_cast<WINDOWPOS*>(lparam);
            if ((pos->flags & SWP_NOMOVE) == 0) {
                pos->x = g_hidden_x;
                pos->y = g_hidden_y;
            }
        }
    }
    if (msg == WM_DESTROY) {
        g_active = false;
        g_stop = true;
    }
    return CallWindowProcA(original, window, msg, wparam, lparam);
}

class Input final : public platform::PresenterInput {
public:
    void set_dest(core::Rect dest) { dest_ = dest; }

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
    HCURSOR cursor() override { return g_cursor; }

private:
    core::Rect dest_{0, 0, 640, 480};
};

void stop_with_unhide(HWND glue, const platform::HiddenState& state, const std::string& reason) {
    g_active = false;
    platform::unhide_window(glue, state);
    platform::log("presenter: game window unhidden (" + reason + ")");
}

void presenter_main() {
    const HWND glue = g_glue;
    platform::HiddenState hidden_state;
    try {
        const core::Config& config = settings().config;
        Input input;
        platform::PresenterWindow window(input, kPresenterSize, "Shadow of the Horned Rat");
        g_presenter = window.hwnd();

        std::string error;
        auto d3d = platform::D3DPresenter::create(window.hwnd(), kPresenterSize, true, error);
        if (!d3d) {
            platform::log("presenter: disabled, " + error);
            return;
        }
        platform::log(std::string("presenter: D3D11 ") +
                      (d3d->driver() == platform::D3DPresenter::Driver::Hardware ? "hardware"
                                                                                 : "WARP"));

        platform::WindowCapture capture;
        store_content_rect(find_content_rect(glue));
        if (!capture.capture(glue, content_rect())) {
            platform::log("presenter: disabled, first capture failed");
            return;
        }

        // Never destroyed: the game thread may be inside it at any time until the process ends.
        static platform::Subclass subclass(glue, &glue_proc);

        MONITORINFO monitor{};
        monitor.cbSize = sizeof monitor;
        GetMonitorInfoA(MonitorFromWindow(glue, MONITOR_DEFAULTTOPRIMARY), &monitor);
        hidden_state = platform::hide_window(glue, monitor.rcWork);
        RECT hidden{};
        GetWindowRect(glue, &hidden);
        g_hidden_x = hidden.left;
        g_hidden_y = hidden.top;
        g_active = true;
        window.show();
        platform::log("presenter: running, game window hidden at (" + std::to_string(hidden.left) +
                      "," + std::to_string(hidden.top) + ")");

        auto last_good = Clock::now();
        auto stats_start = Clock::now();
        double capture_ms = 0.0;
        double present_ms = 0.0;
        int frames = 0;
        while (!g_stop) {
            MSG msg;
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            const core::Rect content = find_content_rect(glue);
            store_content_rect(content);
            const core::Rect dest = core::centered_rect(kPresenterSize, {content.width, content.height});
            input.set_dest(dest);

            const auto t0 = Clock::now();
            if (capture.capture(glue, content)) {
                last_good = t0;
            } else if (t0 - last_good > std::chrono::seconds(2)) {
                stop_with_unhide(glue, hidden_state, "capture failed for 2 s");
                return;
            }
            const auto t1 = Clock::now();
            d3d->render(capture.pixels(), capture.size(), dest);
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
            if (!config.vsync) {
                Sleep(1);
            }
        }
        platform::log("presenter: game window destroyed, stopping");
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

} // namespace

void on_window_created(HWND window, HWND parent, DWORD style) {
    if (parent != nullptr || (style & WS_CHILD) != 0) {
        return;
    }
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true)) {
        return;
    }
    g_glue = window;
    platform::log("presenter: starting for the main window");
    std::thread(presenter_main).detach();
}

bool active() { return g_active; }

void fake_cursor_pos(POINT& out) {
    POINT origin{0, 0};
    ClientToScreen(g_glue, &origin);
    const core::Point p = core::fake_cursor_screen({origin.x, origin.y}, content_rect(),
                                                   {g_cursor_x.load(), g_cursor_y.load()});
    out = POINT{p.x, p.y};
}

bool mask_button(int vk) {
    const HWND presenter = g_presenter;
    if (!g_active || presenter == nullptr || !core::is_mouse_button(vk)) {
        return false;
    }
    POINT cursor{};
    GetCursorPos(&cursor); // our own import: the real position
    bool in_client = WindowFromPoint(cursor) == presenter;
    if (in_client) {
        POINT client = cursor;
        ScreenToClient(presenter, &client);
        RECT r{};
        GetClientRect(presenter, &r);
        in_client = PtInRect(&r, client) != FALSE;
    }
    return !core::report_button(GetForegroundWindow() == presenter, in_client);
}

void set_game_cursor(HCURSOR cursor) { g_cursor = cursor; }

} // namespace hr::wing32::a2
```

- [ ] **Step 3: Change the hooks**

In `src/wing32/hooks.cpp`:

1. Add `#include "wing32/a2.h"` to the includes.
2. Add to `struct Originals` and to `originals()`:

```cpp
    decltype(&::GetAsyncKeyState) GetAsyncKeyState;
    decltype(&::SetCursor) SetCursor;
```

```cpp
        get(user32, "GetAsyncKeyState", r.GetAsyncKeyState);
        get(user32, "SetCursor", r.SetCursor);
```

3. Replace `hook_GetCursorPos` with:

```cpp
BOOL WINAPI hook_GetCursorPos(LPPOINT point) {
    const void* caller = _ReturnAddress();
    BOOL result = TRUE;
    if (point != nullptr && a2::active()) {
        platform::guarded(
            "GetCursorPos", [&] { a2::fake_cursor_pos(*point); },
            [&] { result = originals().GetCursorPos(point); });
    } else {
        result = originals().GetCursorPos(point);
    }
    platform::guarded(
        "GetCursorPos log",
        [&] {
            if (const auto n = sample(g_counters.get_cursor_pos); n && point) {
                trace("GetCursorPos", n, caller,
                      "-> (" + std::to_string(point->x) + "," + std::to_string(point->y) + ")" +
                          (a2::active() ? " virtual" : ""));
            }
        },
        [] {});
    return result;
}
```

4. Add two hooks after `hook_GetCursorPos`:

```cpp
SHORT WINAPI hook_GetAsyncKeyState(int vk) {
    const SHORT state = originals().GetAsyncKeyState(vk);
    return platform::guarded(
        "GetAsyncKeyState", [&]() -> SHORT { return a2::mask_button(vk) ? SHORT{0} : state; },
        [&] { return state; });
}

HCURSOR WINAPI hook_SetCursor(HCURSOR cursor) {
    platform::guarded("SetCursor", [&] { a2::set_game_cursor(cursor); }, [] {});
    return originals().SetCursor(cursor);
}
```

5. In `hook_CreateWindowExA`, inside the guarded logging body after `platform::log(...)`, add:

```cpp
            if (result != nullptr) {
                a2::on_window_created(result, parent, style);
            }
```

6. Add to `kHooks`:

```cpp
    {"USER32.dll", "GetAsyncKeyState", reinterpret_cast<void*>(&hook_GetAsyncKeyState)},
    {"USER32.dll", "SetCursor", reinterpret_cast<void*>(&hook_SetCursor)},
```

In `src/wing32/CMakeLists.txt` add `a2.cpp` to the `wing32` sources.

- [ ] **Step 4: Run the whole suite (Debug and Release)**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-debug; cmake --build --preset x86-debug; ctest --preset x86-debug; cmake --preset x86-release; cmake --build --preset x86-release; ctest --preset x86-release`
Expected: `100% tests passed` in both; no compiler warnings. `proxy_init` still logs `hooks disabled` (no A2 start in the test host).

- [ ] **Step 5: Format, commit, push**

Run Format, then:

```bash
git add src/wing32
git commit -m "wing32: A2 presenter thread, hiding and input path at 1:1" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

---

### Task 13: A2.1 exit — smoke test, A vs C, fade check

**Files:**
- Modify: `docs/smoke-test.md`, `docs/superpowers/specs/2026-10-05-stage-a2-presenter-design.md` (record the three planning deviations; update §2/§10 with the exit results)
- Create: `docs/re-notes/a2-1-exit.md`

This task needs the user; ask before installing into the game folder. Logs and probe CSVs are never committed.

- [ ] **Step 1: Extend the smoke test**

Append to the checklist in `docs/smoke-test.md`:

```markdown
### A2 presenter (from A2.1)
- [ ] The game appears in a "Shadow of the Horned Rat" window; the original game window is not visible.
- [ ] Menu hover and clicks work; the "select mission" text and book buttons react.
- [ ] Typing a save-game name works (Edit control receives keys and shows the caret).
- [ ] Battle: selecting, moving and attacking units with the mouse works; the map scrolls.
- [ ] The intro/briefing videos play inside the presenter.
- [ ] Clicking on the desktop or another window does not click inside the game.
- [ ] Closing the presenter quits the game normally.
- [ ] Dragging the presenter partly off-screen: the picture keeps updating everywhere.
- [ ] Other windows stay clickable where the hidden game window lies (it is click-through).
- [ ] Panic switch `enabled = false`: the game runs as vanilla (no presenter).
- [ ] `horned-rat.log` shows `presenter: running`, 10-second performance lines, and no `exception`.
```

- [ ] **Step 2: Install the release build (ask the user first)**

Run: `. ./tools/vsenv.ps1; cmake --preset x86-release; cmake --build --preset x86-release; ./tools/install.ps1 -GameDir 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat' -SourceDll build/x86-release/bin/wing32.dll`
Expected: `Installed horned-rat into '...\WARFB'.`

- [ ] **Step 3: The user runs the smoke test**

Record pass/fail per item, the presenter log lines (device, performance) and anything that differs from vanilla.

- [ ] **Step 4: Fade check**

Vanilla: set `enabled = false`, start the game, then run `build/x86-release/bin/luma_probe.exe 30 $env:TEMP/fade_vanilla.csv` while the user goes main menu → mission select → back (screen transitions). A2: set `enabled = true`, repeat with `fade_a2.csv`. Expected for each: a few thousand samples (this is also Task 11's green step). Compare the transitions: a fade shows as a ramp of intermediate brightness values over ~0.3–1 s; a hard cut jumps between two levels within one or two samples.

- [ ] **Step 5: Write the exit notes and update the spec**

`docs/re-notes/a2-1-exit.md`: date, build hash, smoke-test results, the presenter's performance lines, the fade-check result (ramps present in vanilla? in A2?), and whether "restore palette effects" becomes a separate item. In the spec, add the three planning deviations under §5 and the results to §2/§10.

- [ ] **Step 6: Commit, push, mark the PR ready**

```bash
git add docs
git commit -m "docs: A2.1 exit results" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
gh pr ready
```

**Milestone A2.1 is complete.** Next: plan A2.2 (scaling) with superpowers:writing-plans.
