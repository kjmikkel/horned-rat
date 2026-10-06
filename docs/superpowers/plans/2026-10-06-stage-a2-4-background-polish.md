# Stage A2.4: Background and Polish Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add `background = run | pause`, so the user chooses whether the game keeps playing when the presenter loses focus. Give the performance line a "slowest frame" figure, look into the start-up brightness dip noted in A2.1, and close Stage A2 with a full smoke test.

**Architecture:**
- The game believes it is active from the `Glue` subclass, which rewrites `WM_ACTIVATE` / `WM_ACTIVATEAPP`. A new `core::game_activation` decides the rewrite for both modes.
- `Glue` is hidden and never in the foreground, so it hears nothing when the user switches to another application. For `pause`, the presenter therefore reports its own application activation (`WM_ACTIVATEAPP`, ignoring switches to a window of our own process such as a game dialog) and posts the matching activation messages to `Glue`.
- The plan gathers evidence before any fix. Diagnostics show how the game reacts to deactivation (minimise, hide, display mode) and when the start-up dip happens. Task 5's fixes are chosen from that evidence.

**Tech Stack:** C++20, MSVC x86 (`/W4 /WX`, static CRT), CMake/Ninja presets, Catch2 v3.8.1, Win32 (window messages, `OpenThread` / `GetProcessIdOfThread`), Direct3D 11 (unchanged).

**Spec:** `docs/superpowers/specs/2026-10-05-stage-a2-presenter-design.md` (milestone A2.4; §5.4 Window state, §6 `background = run | pause`, §7 logging, §8 smoke test). Open items: `docs/re-notes/a2-1-exit.md` §4 (brightness dip), `docs/re-notes/a2-3-exit.md` §5.

## Global Constraints

- **Layering:** `core` → `platform` → `wing32`. `core` includes no Windows headers. Win32 values used in `core` are mirrored in `core::win` and `static_assert`ed in a platform test.
- **TDD:** red/green TDD for every behaviour. Write the test, see it fail for the right reason, then implement. `wing32` glue is thin and is verified by the manual smoke test.
- **Language and format:** C++20, `/W4 /WX`. clang-format 19.1.7 (LLVM base, 4 spaces, 100 columns, `PointerAlignment: Left`). CI runs a format check, so run `clang-format -i` on touched files before committing.
- **Build and test** (PowerShell, repo root): `. ./tools/vsenv.ps1; cmake --build --preset x86-debug; ctest --preset x86-debug`.
  - To run one test binary with a tag: `./build/x86-debug/bin/core_tests.exe "[frame_stats]"`.
  - In PowerShell, give paths without quotes.
- **Game files:** never commit game files, logs, captures, probe CSVs or decompiled code. Ask the user before installing into the GOG game folder or changing `horned-rat.ini`, and restore ini changes at the end.
  - Game folder: `D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat`; the ini and log are in its `WARFB\`.
- **"Never worse than vanilla"** (spec §7): every presenter failure unhides the game window and leaves a playable game.
- **Threads:** the presenter thread never waits for the game thread. It posts to game windows (or uses `SendMessageCallbackA`), never `SendMessage`. The game may be inside a modal dialog.
- **Config** (spec §6): `[general] background = run | pause`, default `run`. Invalid values warn and use the default.
- **Spec §5.4:** "With `background = run`, `WM_ACTIVATE(WA_INACTIVE)` and `WM_ACTIVATEAPP(FALSE)` are passed on as active; with `background = pause` they pass unchanged." Decision 2 below refines "unchanged" for the hidden window.
- **Git and PR:**
  - Work on branch `stage-a2-4`.
  - Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
  - PR bodies end with `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.
  - Turn Auto-fix on for the PR.
  - Before every push after Task 1, check the PR is still open (`gh pr view --json state`). If it was merged, continue on a new branch from `origin/main` with a follow-up PR.

## Decisions taken in this plan (record in the spec at Task 6)

1. **"The application is active"** means one of our process's windows is in the foreground: the presenter, or a game dialog (`MessageBoxA` on the game thread). The presenter thread tracks it from its own `WM_ACTIVATEAPP`.
   - `WM_ACTIVATEAPP(TRUE)` → active.
   - `WM_ACTIVATEAPP(FALSE, thread)` → still active if `thread` belongs to our process (focus moved to a game dialog), else inactive.
2. **`pause` with a hidden window.** "Pass unchanged" does not work as written. `Glue` is never the foreground window, so Windows tells it about deactivation only by accident (when a game dialog closes), and never about the user coming back. Instead:
   - The `Glue` subclass rewrites `WM_ACTIVATE` / `WM_ACTIVATEAPP` to the application's state (`core::game_activation`). `run` keeps today's behaviour: always active.
   - On a change of the application's state, `pause` posts the messages vanilla would send, in vanilla's order. Losing focus posts `WM_ACTIVATE(WA_INACTIVE)` then `WM_ACTIVATEAPP(FALSE)`; regaining it posts `WM_ACTIVATEAPP(TRUE)` then `WM_ACTIVATE(WA_ACTIVE)`. The A2.3 log shows that order.
   - `WM_NCACTIVATE` is not rewritten. Task 5 changes that only if the evidence asks for it.
3. **Performance line:** the 10 s line gains the slowest frame (capture + present). Means hide single stutters; the slowest frame shows them. The stats move from `a2.cpp` to `core::FrameStats`.
4. **Start-up brightness trace:** for the first 5 s after `presenter: running`, the presenter logs the captured picture's mean brightness whenever it changes by at least 4 (of 255). Log timestamps then put the dip next to the other log lines: activation, `ShowWindow`, display-mode changes. This replaces the out-of-process `luma_probe`, whose clock is not the log's. The trace is a permanent diagnostic and costs nothing after 5 s.
5. **Deactivation diagnostics stay:** `Glue`'s `WM_SIZE` and `WM_SHOWWINDOW` join the logged activation messages, and a `ShowWindow` hook logs calls on top-level windows.

## File Structure

| File | Responsibility |
|---|---|
| `src/core/config.{h,cpp}` | + `general.background` (`Background`) |
| `src/core/input_map.{h,cpp}` | + `game_activation` |
| `src/core/frame_stats.{h,cpp}` (new) | `FrameStats`: the 10 s performance line |
| `src/core/luma.{h,cpp}` | + `BrightnessTrace` |
| `src/platform/presenter_window.{h,cpp}` | `PresenterInput::on_app_activation`, `thread_in_this_process` |
| `src/wing32/hooks.cpp` | diagnostic `ShowWindow` hook |
| `src/wing32/a2.cpp` | `background` wiring, `WM_SIZE`/`WM_SHOWWINDOW` diagnostics, `FrameStats`, brightness trace |
| `tests/core/config_test.cpp`, `input_map_test.cpp`, `frame_stats_test.cpp` (new), `luma_test.cpp` | core tests |
| `tests/platform/presenter_window_test.cpp` | platform tests |
| `docs/re-notes/a2-4-exit.md` (new), `docs/smoke-test.md`, the spec | results |

---

### Task 1: `background` key and the activation rule (core)

**Files:**
- Modify: `src/core/config.h`, `src/core/config.cpp`, `src/core/input_map.h`, `src/core/input_map.cpp`
- Test: `tests/core/config_test.cpp`, `tests/core/input_map_test.cpp`

**Interfaces:**
- Produces:
  - `enum class hr::core::Background { Run, Pause };` and `Background Config::background = Background::Run;` (key `general.background`)
  - `std::uintptr_t hr::core::game_activation(Background background, unsigned message, std::uintptr_t wparam, bool app_active);`

- [ ] **Step 1: Write the failing tests**

In `tests/core/config_test.cpp`, extend `"every key is read"`. The ini text gets `background = pause` after `enabled = false`:

```cpp
    const auto r = load_config("[general]\nenabled = false\nbackground = pause\n"
                               "[display]\nmode = borderless\nwindow_size = 1920x1440\n"
                               "scale = integer\naspect = stretch\nvsync = no\ncursor = native\n"
                               "[debug]\nlog = off\ngame_debug = 0\nmessage_census = on\n");
```

and add `CHECK(r.config.background == Background::Pause);` after `CHECK_FALSE(r.config.enabled);`. Then add after `"an invalid cursor mode warns and keeps scaled"`:

```cpp
TEST_CASE("an invalid background mode warns and keeps run", "[config]") {
    const auto r = load_config("[general]\nbackground = sleep\n");
    CHECK(r.config.background == Background::Run);
    REQUIRE(r.warnings.size() == 1);
    CHECK_THAT(r.warnings[0], ContainsSubstring("general.background") && ContainsSubstring("sleep"));
}
```

In `tests/core/input_map_test.cpp`, append:

```cpp
TEST_CASE("background = run: the game is always told it is active", "[input_map]") {
    for (bool app_active : {true, false}) {
        INFO(app_active);
        CHECK(game_activation(Background::Run, kWmActivate, kWaInactive, app_active) == kWaActive);
        CHECK(game_activation(Background::Run, kWmActivateApp, 0, app_active) == 1u);
        CHECK(game_activation(Background::Run, kWmActivate, 2, app_active) == 2u);
    }
}

TEST_CASE("background = pause: the game follows the application's activation", "[input_map]") {
    // Active: deactivations caused inside our process (a game dialog) are hidden from the game.
    CHECK(game_activation(Background::Pause, kWmActivate, kWaInactive, true) == kWaActive);
    CHECK(game_activation(Background::Pause, kWmActivateApp, 0, true) == 1u);
    CHECK(game_activation(Background::Pause, kWmActivate, 2, true) == 2u); // WA_CLICKACTIVE
    // Inactive: every activation message says inactive; the minimised flag (high word) stays.
    CHECK(game_activation(Background::Pause, kWmActivate, kWaActive, false) == kWaInactive);
    CHECK(game_activation(Background::Pause, kWmActivate, 0x00010002u, false) == 0x00010000u);
    CHECK(game_activation(Background::Pause, kWmActivateApp, 1, false) == 0u);
}

TEST_CASE("other messages pass through game_activation unchanged", "[input_map]") {
    for (auto background : {Background::Run, Background::Pause}) {
        CHECK(game_activation(background, kWmKeyDown, 7, false) == 7u);
        CHECK(game_activation(background, kWmKeyDown, 7, true) == 7u);
    }
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `. ./tools/vsenv.ps1; cmake --build --preset x86-debug`
Expected: compile errors: `background` is not a member of `Config`, and `Background` / `game_activation` are undeclared.

- [ ] **Step 3: Implement**

`src/core/config.h`, after `enum class CursorMode`:

```cpp
enum class Background { Run, Pause };
```

and in `Config`, after `bool enabled = true;`:

```cpp
    Background background = Background::Run; // when the presenter is not focused (spec §5.4)
```

`src/core/config.cpp`, in `kFields` after `general.enabled`:

```cpp
    {"general.background",
     [](Config& c, std::string_view v) {
         return assign(c.background, parse_choice<Background>(v, {{"run", Background::Run},
                                                                  {"pause", Background::Pause}}));
     }},
```

and in `default_config_ini()`, after the `enabled` line:

```
background   = run         ; run (keeps playing when not focused) | pause
```

`src/core/input_map.h`: add `#include "core/config.h"` after `#include <cstdint>`, and after `filter_activation`:

```cpp
// WM_ACTIVATE / WM_ACTIVATEAPP parameters as the game must see them (spec §5.4). `app_active`:
// one of our process's windows (the presenter or a game dialog) is in the foreground.
// Run: always active. Pause: active exactly while app_active.
std::uintptr_t game_activation(Background background, unsigned message, std::uintptr_t wparam,
                               bool app_active);
```

`src/core/input_map.cpp`, after `filter_activation`:

```cpp
std::uintptr_t game_activation(Background background, unsigned message, std::uintptr_t wparam,
                               bool app_active) {
    if (background == Background::Run || app_active) {
        return filter_activation(message, wparam);
    }
    if (message == kWmActivate) {
        return (wparam & ~std::uintptr_t{0xFFFF}) | kWaInactive;
    }
    if (message == kWmActivateApp) {
        return 0;
    }
    return wparam;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/core_tests.exe "[config],[input_map]"`
Expected: all pass, including `"the default file parses back to the defaults"`. Then `ctest --preset x86-debug`: all pass.

- [ ] **Step 5: Commit, push and open the PR**

```bash
clang-format -i src/core/config.h src/core/config.cpp src/core/input_map.h src/core/input_map.cpp tests/core/config_test.cpp tests/core/input_map_test.cpp
git add src/core tests/core docs/superpowers/plans/2026-10-06-stage-a2-4-background-polish.md
git commit -m "core: background = run | pause and the game's activation rule" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push -u origin stage-a2-4
gh pr create --draft --base main --title "Stage A2.4: background and polish" --body "Implements milestone **A2.4** of \`docs/superpowers/specs/2026-10-05-stage-a2-presenter-design.md\`: \`background = run | pause\`, the performance line, the start-up brightness dip, and the closing smoke test of Stage A2. Plan: \`docs/superpowers/plans/2026-10-06-stage-a2-4-background-polish.md\`. Draft until Task 6.

🤖 Generated with [Claude Code](https://claude.com/claude-code)"
```

Then bind the PR in the app and turn Auto-fix on.

---

### Task 2: The presenter reports the application's activation (platform)

**Files:**
- Modify: `src/platform/presenter_window.h`, `src/platform/presenter_window.cpp`
- Test: `tests/platform/presenter_window_test.cpp`

**Interfaces:**
- Consumes: nothing from Task 1.
- Produces:
  - `virtual void PresenterInput::on_app_activation(bool active) = 0;`, called on the window's thread for every `WM_ACTIVATEAPP` the presenter receives.
  - `bool hr::platform::thread_in_this_process(DWORD thread_id);`

- [ ] **Step 1: Write the failing tests**

In `tests/platform/presenter_window_test.cpp`, add to `FakeInput` (after `cursor()`):

```cpp
    std::vector<bool> activations;
    void on_app_activation(bool active) override { activations.push_back(active); }
```

Append:

```cpp
TEST_CASE("thread_in_this_process tells our threads from others", "[presenter_window]") {
    CHECK(hr::platform::thread_in_this_process(GetCurrentThreadId()));
    CHECK_FALSE(hr::platform::thread_in_this_process(0));

    STARTUPINFOA startup{};
    startup.cb = sizeof startup;
    PROCESS_INFORMATION child{};
    char command[] = "cmd.exe /c exit";
    REQUIRE(CreateProcessA(nullptr, command, nullptr, nullptr, FALSE,
                           CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                           &child));
    CHECK_FALSE(hr::platform::thread_in_this_process(child.dwThreadId));
    TerminateProcess(child.hProcess, 0);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
}

TEST_CASE("the presenter window reports the application's activation", "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    SendMessageA(window.hwnd(), WM_ACTIVATEAPP, TRUE, 0);
    // Focus moved to a window of our own process (a game dialog): still active.
    SendMessageA(window.hwnd(), WM_ACTIVATEAPP, FALSE, static_cast<LPARAM>(GetCurrentThreadId()));
    // Focus moved to another application (thread id 0 = none of ours).
    SendMessageA(window.hwnd(), WM_ACTIVATEAPP, FALSE, 0);
    CHECK(input.activations == std::vector<bool>{true, true, false});
}
```

The child process is created suspended and killed at once, so it never runs; its main thread is a real thread of another process.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset x86-debug`
Expected: compile errors: `on_app_activation` marked `override` does not override, and `thread_in_this_process` is not a member of `hr::platform`.

- [ ] **Step 3: Implement**

`src/platform/presenter_window.h`, in `PresenterInput` after `cursor()`:

```cpp
    // On every WM_ACTIVATEAPP: true while one of our process's windows is in the foreground
    // (the presenter or a game dialog), false once another application has it.
    virtual void on_app_activation(bool active) = 0;
```

and after `cursor_in_client`:

```cpp
// True if `thread_id` is a running thread of this process.
bool thread_in_this_process(DWORD thread_id);
```

`src/platform/presenter_window.cpp`, in `proc`'s switch before `case WM_ERASEBKGND:`:

```cpp
    case WM_ACTIVATEAPP:
        // lparam is the thread that gains (or loses) activation. Moving to a game dialog on the
        // game thread is no loss for the application.
        self->input_.on_app_activation(wparam != FALSE ||
                                       thread_in_this_process(static_cast<DWORD>(lparam)));
        break;
```

and at the end of the namespace:

```cpp
bool thread_in_this_process(DWORD thread_id) {
    const HANDLE thread = OpenThread(THREAD_QUERY_LIMITED_INFORMATION, FALSE, thread_id);
    if (thread == nullptr) {
        return false;
    }
    const bool ours = GetProcessIdOfThread(thread) == GetCurrentProcessId();
    CloseHandle(thread);
    return ours;
}
```

In `src/wing32/a2.cpp`, give `Input` a temporary body so the DLL still builds (Task 4 replaces it):

```cpp
    void on_app_activation(bool) override {}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/platform_tests.exe "[presenter_window]"`
Expected: all pass. Then `ctest --preset x86-debug`: all pass.

- [ ] **Step 5: Commit and push** (check the PR is still open first)

```bash
clang-format -i src/platform/presenter_window.h src/platform/presenter_window.cpp src/wing32/a2.cpp tests/platform/presenter_window_test.cpp
git add src/platform src/wing32 tests/platform
git commit -m "platform: the presenter window reports the application's activation" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

---

### Task 3: Frame statistics and the start-up brightness trace (core)

**Files:**
- Create: `src/core/frame_stats.h`, `src/core/frame_stats.cpp`, `tests/core/frame_stats_test.cpp`
- Modify: `src/core/luma.h`, `src/core/luma.cpp`, `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`
- Test: `tests/core/frame_stats_test.cpp`, `tests/core/luma_test.cpp`

**Interfaces:**
- Produces:
  - `class hr::core::FrameStats { void add(double capture_ms, double present_ms); int frames() const; std::string take_line(int seconds); };`
  - `class hr::core::BrightnessTrace { BrightnessTrace(double duration_s, double min_step); bool running(double t) const; bool should_log(double t, double luma); };`

- [ ] **Step 1: Write the failing tests**

`tests/core/frame_stats_test.cpp`:

```cpp
#include "core/frame_stats.h"

#include <catch2/catch_test_macros.hpp>

using hr::core::FrameStats;

TEST_CASE("the performance line has means and the slowest frame", "[frame_stats]") {
    FrameStats stats;
    stats.add(2.0, 14.0);
    stats.add(4.0, 16.0);
    CHECK(stats.frames() == 2);
    CHECK(stats.take_line(10) ==
          "2 frames in 10 s, capture 3.00 ms, present 15.00 ms, slowest 20.00 ms");
}

TEST_CASE("taking the line starts a new period", "[frame_stats]") {
    FrameStats stats;
    stats.add(30.0, 5.0);
    (void)stats.take_line(10);
    CHECK(stats.frames() == 0);
    stats.add(1.0, 1.0);
    CHECK(stats.take_line(10) ==
          "1 frames in 10 s, capture 1.00 ms, present 1.00 ms, slowest 2.00 ms");
}

TEST_CASE("a period without frames says so", "[frame_stats]") {
    FrameStats stats;
    CHECK(stats.take_line(10) == "0 frames in 10 s");
}
```

Append to `tests/core/luma_test.cpp` (add `using hr::core::BrightnessTrace;` after the other `using` lines):

```cpp
TEST_CASE("the brightness trace logs the first frame and changes of at least the step",
          "[luma]") {
    BrightnessTrace trace(5.0, 4.0);
    CHECK(trace.should_log(0.0, 46.0));
    CHECK_FALSE(trace.should_log(0.1, 48.0)); // +2: below the step
    CHECK(trace.should_log(0.2, 15.0));       // the dip
    CHECK_FALSE(trace.should_log(0.3, 18.9)); // +3.9 since the last logged value
    CHECK(trace.should_log(0.4, 53.6));
}

TEST_CASE("the brightness trace stops after its duration", "[luma]") {
    BrightnessTrace trace(5.0, 4.0);
    CHECK(trace.running(4.99));
    CHECK_FALSE(trace.running(5.0));
    CHECK_FALSE(trace.should_log(5.0, 100.0));
}
```

Add `frame_stats_test.cpp` to `core_tests` in `tests/core/CMakeLists.txt` (after `cursor_bitmap_test.cpp`).

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset x86-debug`
Expected: compile errors: `core/frame_stats.h` not found; `BrightnessTrace` is not a member of `hr::core`.

- [ ] **Step 3: Implement**

`src/core/frame_stats.h`:

```cpp
#pragma once

#include <string>

namespace hr::core {

// The presenter's performance line (spec §7), one per reporting period: frame count, mean
// capture and present times, and the slowest frame, which shows stutter the means hide.
class FrameStats {
  public:
    void add(double capture_ms, double present_ms);
    int frames() const { return frames_; }
    // "601 frames in 10 s, capture 2.13 ms, present 14.25 ms, slowest 18.40 ms"; starts a new
    // period.
    std::string take_line(int seconds);

  private:
    int frames_ = 0;
    double capture_ms_ = 0.0;
    double present_ms_ = 0.0;
    double slowest_ms_ = 0.0;
};

} // namespace hr::core
```

`src/core/frame_stats.cpp`:

```cpp
#include "core/frame_stats.h"

#include <algorithm>
#include <cstdio>

namespace hr::core {

void FrameStats::add(double capture_ms, double present_ms) {
    ++frames_;
    capture_ms_ += capture_ms;
    present_ms_ += present_ms;
    slowest_ms_ = std::max(slowest_ms_, capture_ms + present_ms);
}

std::string FrameStats::take_line(int seconds) {
    char line[160];
    if (frames_ == 0) {
        std::snprintf(line, sizeof line, "0 frames in %d s", seconds);
    } else {
        std::snprintf(line, sizeof line,
                      "%d frames in %d s, capture %.2f ms, present %.2f ms, slowest %.2f ms",
                      frames_, seconds, capture_ms_ / frames_, present_ms_ / frames_, slowest_ms_);
    }
    *this = FrameStats{};
    return line;
}

} // namespace hr::core
```

`src/core/luma.h`, after `mean_luma`:

```cpp
// Which captured frames' brightness is logged at start-up (A2.4 diagnostics for the brightness
// dip): within `duration_s` of the start, the first frame and every change of at least
// `min_step` since the last logged value.
class BrightnessTrace {
  public:
    BrightnessTrace(double duration_s, double min_step)
        : duration_s_(duration_s), min_step_(min_step) {}
    bool running(double t) const { return t < duration_s_; }
    bool should_log(double t, double luma);

  private:
    double duration_s_;
    double min_step_;
    bool logged_any_ = false;
    double last_ = 0.0;
};
```

`src/core/luma.cpp`: add `#include <cmath>` if missing, and:

```cpp
bool BrightnessTrace::should_log(double t, double luma) {
    if (!running(t) || (logged_any_ && std::abs(luma - last_) < min_step_)) {
        return false;
    }
    logged_any_ = true;
    last_ = luma;
    return true;
}
```

Add `frame_stats.cpp` to `hr_core` in `src/core/CMakeLists.txt`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/core_tests.exe "[frame_stats],[luma]"`
Expected: all pass. Then `ctest --preset x86-debug`: all pass.

- [ ] **Step 5: Commit and push** (check the PR is still open first)

```bash
clang-format -i src/core/frame_stats.h src/core/frame_stats.cpp src/core/luma.h src/core/luma.cpp tests/core/frame_stats_test.cpp tests/core/luma_test.cpp
git add src/core tests/core
git commit -m "core: frame statistics with the slowest frame, start-up brightness trace" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

---

### Task 4: Wiring, diagnostics and the evidence run

**Files:**
- Modify: `src/wing32/a2.cpp`, `src/wing32/hooks.cpp`
- Create: `docs/re-notes/a2-4-exit.md`

**Interfaces:**
- Consumes: `core::Background`, `core::game_activation` (Task 1); `PresenterInput::on_app_activation` (Task 2); `core::FrameStats`, `core::BrightnessTrace`, `core::mean_luma` (Task 3).

`wing32` glue is verified by the game run, not by unit tests (Global Constraints).

- [ ] **Step 1: `background` in `src/wing32/a2.cpp`**

After `std::atomic<bool> g_stop{false};`:

```cpp
// One of our process's windows is in the foreground (presenter thread's WM_ACTIVATEAPP).
std::atomic<bool> g_app_active{true};
```

In `glue_proc`, replace both uses of `core::filter_activation(msg, wparam)` with:

```cpp
core::game_activation(settings().config.background, msg, wparam, g_app_active)
```

Replace the temporary `on_app_activation` in `Input` with:

```cpp
    void on_app_activation(bool active) override {
        if (g_app_active.exchange(active) == active) {
            return;
        }
        const bool pause = settings().config.background == core::Background::Pause;
        platform::log(std::string("presenter: ") +
                      (active ? (pause ? "focused, game resumed" : "focused")
                              : (pause ? "lost focus, game paused" : "lost focus, game runs on")));
        if (!pause || !g_active) {
            return;
        }
        // Glue is never the foreground window, so Windows does not tell it; send what vanilla
        // gets, in vanilla's order. Posted: the presenter never waits for the game thread. The
        // Glue subclass gives both messages the new state (core::game_activation).
        const HWND glue = g_glue;
        if (active) {
            PostMessageA(glue, WM_ACTIVATEAPP, TRUE, 0);
            PostMessageA(glue, WM_ACTIVATE, WA_ACTIVE, 0);
        } else {
            PostMessageA(glue, WM_ACTIVATE, WA_INACTIVE, 0);
            PostMessageA(glue, WM_ACTIVATEAPP, FALSE, 0);
        }
    }
```

In `presenter_body`, after the `presenter: cursor ...` log line, add:

```cpp
        platform::log(std::string("presenter: background ") +
                      (config.background == core::Background::Pause ? "pause" : "run"));
```

- [ ] **Step 2: Deactivation diagnostics in `a2.cpp`**

Rename `activation_name` to `watched_name` (both the definition and the call in `glue_proc`), and add two cases before `default:`:

```cpp
    case WM_SIZE:
        return "WM_SIZE";
    case WM_SHOWWINDOW:
        return "WM_SHOWWINDOW";
```

Update the comment of the logging block in `glue_proc` if it names activation only. The log line already prints `wparam`. For `WM_SIZE`, `1` = `SIZE_MINIMIZED`; for `WM_SHOWWINDOW`, `0` = hidden.

- [ ] **Step 3: Frame statistics and brightness trace in `a2.cpp`**

Add `#include "core/frame_stats.h"` and `#include "core/luma.h"`. In `presenter_body`, replace the four variables `stats_start`, `capture_ms`, `present_ms`, `frames` with:

```cpp
        auto stats_start = Clock::now();
        core::FrameStats stats;
        const auto running_since = Clock::now();
        core::BrightnessTrace brightness(5.0, 4.0); // diagnostics: the start-up dip
```

In `frame`, after `const auto t1 = Clock::now();`:

```cpp
            const double since = std::chrono::duration<double>(t1 - running_since).count();
            if (brightness.running(since)) {
                const core::Size size = capture.size();
                const double luma = core::mean_luma(capture.pixels(), size.width, size.height);
                if (brightness.should_log(since, luma)) {
                    char line[64];
                    std::snprintf(line, sizeof line, "diag: brightness %.1f", luma);
                    platform::log(line);
                }
            }
```

and replace the statistics block after `const auto t2 = Clock::now();` with:

```cpp
            stats.add(std::chrono::duration<double, std::milli>(t1 - t0).count(),
                      std::chrono::duration<double, std::milli>(t2 - t1).count());
            if (t2 - stats_start >= std::chrono::seconds(10)) {
                platform::log("presenter: " + stats.take_line(10));
                stats_start = t2;
            }
```

- [ ] **Step 4: Diagnostic `ShowWindow` hook in `src/wing32/hooks.cpp`**

In `Originals`, after `SetWindowPos`: `decltype(&::ShowWindow) ShowWindow;`. In the resolver, after `get(user32, "SetWindowPos", r.SetWindowPos);`: `get(user32, "ShowWindow", r.ShowWindow);`. After `hook_SetWindowPos`:

```cpp
// Diagnostics (A2.4): how the game hides, minimises or restores its top-level windows, for
// example when it loses activation. Child windows are shown constantly and are not logged.
BOOL WINAPI hook_ShowWindow(HWND window, int command) {
    const void* caller = _ReturnAddress();
    const BOOL result = originals().ShowWindow(window, command);
    const DWORD error = GetLastError();
    platform::guarded(
        "ShowWindow",
        [&] {
            if ((GetWindowLongPtrA(window, GWL_STYLE) & WS_CHILD) == 0) {
                platform::log("diag: ShowWindow(class '" + window_class(window) + "', " +
                              std::to_string(command) + ") -> " + std::to_string(result) +
                              " from " + platform::module_file_name_from_address(caller));
            }
        },
        [] {});
    SetLastError(error);
    return result;
}
```

In `kHooks`, after the `SetWindowPos` entry: `{"USER32.dll", "ShowWindow", reinterpret_cast<void*>(&hook_ShowWindow)},`. If `window_class` is defined after `hook_SetWindowPos`, place the new hook after `window_class` instead.

- [ ] **Step 5: Build and run all tests**

Run: `. ./tools/vsenv.ps1; cmake --build --preset x86-debug; ctest --preset x86-debug; cmake --build --preset x86-release; ctest --preset x86-release`
Expected: builds without warnings; all tests pass.

- [ ] **Step 6: Commit and push** (check the PR is still open first)

```bash
clang-format -i src/wing32/a2.cpp src/wing32/hooks.cpp
git add src/wing32
git commit -m "wing32: background = pause, deactivation and start-up brightness diagnostics" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

- [ ] **Step 7: Install and the evidence run (ask the user first)**

Ask before installing and before each ini change. Install:
`./tools/install.ps1 -GameDir 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat' -SourceDll build/x86-release/bin/wing32.dll`

Two runs:
1. **Run A, `background = run`** (default, no ini change):
   - Watch the presenter appear: is there a visible flicker or darkening at start-up?
   - In the main menu and in a battle, alt-tab to another application for ~10 s, then back. Does the game keep playing (music, units moving), and does the picture stay right?
2. **Run B, `background = pause`** (add `background = pause` under `[general]`):
   - The same alt-tab in the menu and in a battle. Does the game stop while away, and resume on return? Does sound stop and come back?
   - Click on another window while a game dialog (the exit question) is open, then return.

After each run, read `WARFB\horned-rat.log`. Look at:
- `presenter: lost focus …` / `focused …`;
- `diag: Glue WM_…` lines, especially `WM_SIZE` 1 (minimised) and `WM_SHOWWINDOW` 0;
- `diag: ShowWindow(class 'Glue', …)` (6 = `SW_MINIMIZE`, 0 = `SW_HIDE`, 9 = `SW_RESTORE`);
- `ChangeDisplaySettingsA` lines;
- `presenter: game window unhidden` (the presenter gave up);
- the `diag: brightness` lines next to the start-up lines.

- [ ] **Step 8: Write `docs/re-notes/a2-4-exit.md` §1 Investigation**

```markdown
# A2.4 exit: background and polish

## 1. Investigation

### Run A (`background = run`)
- **Run:** date, build commit, hardware/WARP.
- **What the user saw:** …
- **Log:** …

### Run B (`background = pause`)
- …

### Start-up brightness
- `diag: brightness` lines with their timestamps, next to the log lines around them.
- **Conclusion:** …
```

Fill it in from the user's report and the log; do not commit the log itself. Commit (`docs: A2.4 investigation`) and push.

- [ ] **Step 9: Gate**

Decide Task 5's fixes from the evidence, using the table in Task 5. If the evidence matches none of its rows, stop and discuss with the user before writing code.

---

### Task 5: Fixes from the evidence

**Files (only those the chosen rows need):**
- Modify: `src/core/input_map.{h,cpp}`, `src/wing32/a2.cpp`, `src/wing32/hooks.cpp`
- Test: `tests/core/input_map_test.cpp`, `tests/platform/window_subclass_test.cpp`

Apply each row whose evidence was seen. Each row is its own red/green cycle and its own commit. Each ends with a re-run of the affected item with the user (ask before installing).

| Evidence | Cause | Fix |
|---|---|---|
| Run A and B behave as the user expects; no dip, or a dip the user does not notice | — | None. Record "no fix needed" in §4 of the exit notes. |
| `WM_SIZE 1` / `ShowWindow(…, 6 or 0)` on `Glue` on deactivation in run B, followed by capture trouble or `game window unhidden` | The game minimises or hides its window when paused. A minimised window has no client area to capture. | **5a** below |
| In run B the game never pauses (no reaction to the posted messages) | The game decides activity some other way (e.g. polling `GetActiveWindow` / `GetForegroundWindow`) | Stop and discuss with the user. Hooking those functions changes the game's view of every window, which is a design decision. |
| The brightness dip coincides with `diag: Glue WM_NCACTIVATE wparam=0` at start-up | The game reacts to the non-client deactivation the subclass lets through | **5b** below |
| The dip coincides with something else (display mode, first `ShowWindow`) | — | Record the finding. If the user does not notice the dip, close the item as cosmetic. Otherwise stop and discuss. |

#### 5a: keep the hidden window restored while the presenter runs

The presenter keeps showing the last good frame while the game is paused. The game's own minimise / hide of `Glue` is skipped while A2 is active. `Glue` is already invisible, so nothing on screen changes, and capture keeps working.

- [ ] **Step 1: Write the failing test** — append to `tests/core/input_map_test.cpp`:

```cpp
TEST_CASE("while the presenter runs, the game cannot minimise or hide its window",
          "[input_map]") {
    for (int command : {kSwHide, kSwMinimize, kSwShowMinimized, kSwShowMinNoActive, kSwForceMinimize}) {
        INFO(command);
        CHECK(skip_show_window(true, command));
        CHECK_FALSE(skip_show_window(false, command));
    }
    for (int command : {kSwShow, kSwRestore, kSwShowNormal, kSwShowNa}) {
        INFO(command);
        CHECK_FALSE(skip_show_window(true, command));
    }
}
```

And in `tests/platform/window_subclass_test.cpp`, next to the other `static_assert`s:

```cpp
static_assert(hr::core::win::kSwHide == SW_HIDE);
static_assert(hr::core::win::kSwShowNormal == SW_SHOWNORMAL);
static_assert(hr::core::win::kSwShowMinimized == SW_SHOWMINIMIZED);
static_assert(hr::core::win::kSwMinimize == SW_MINIMIZE);
static_assert(hr::core::win::kSwShowMinNoActive == SW_SHOWMINNOACTIVE);
static_assert(hr::core::win::kSwShowNa == SW_SHOWNA);
static_assert(hr::core::win::kSwRestore == SW_RESTORE);
static_assert(hr::core::win::kSwShow == SW_SHOW);
static_assert(hr::core::win::kSwForceMinimize == SW_FORCEMINIMIZE);
```

- [ ] **Step 2: Run it to verify it fails** — `cmake --build --preset x86-debug`. Expected: `kSwHide` and `skip_show_window` are undeclared.

- [ ] **Step 3: Implement** — in `core::win` (`input_map.h`):

```cpp
inline constexpr int kSwHide = 0;
inline constexpr int kSwShowNormal = 1;
inline constexpr int kSwShowMinimized = 2;
inline constexpr int kSwMinimize = 6;
inline constexpr int kSwShowMinNoActive = 7;
inline constexpr int kSwShowNa = 8;
inline constexpr int kSwRestore = 9;
inline constexpr int kSwShow = 5;
inline constexpr int kSwForceMinimize = 11;
```

and after `game_activation`:

```cpp
// True if the game's ShowWindow on its main window must be skipped: while the presenter runs
// the window must stay restored (hidden by us, never minimised), or there is nothing to capture.
bool skip_show_window(bool presenter_active, int command);
```

`input_map.cpp`:

```cpp
bool skip_show_window(bool presenter_active, int command) {
    if (!presenter_active) {
        return false;
    }
    switch (command) {
    case kSwHide:
    case kSwShowMinimized:
    case kSwMinimize:
    case kSwShowMinNoActive:
    case kSwForceMinimize:
        return true;
    default:
        return false;
    }
}
```

In `hook_ShowWindow` (`hooks.cpp`), before calling the original:

```cpp
    if (window == a2::main_window() && core::skip_show_window(a2::active(), command)) {
        platform::log("diag: ShowWindow(Glue, " + std::to_string(command) +
                      ") skipped while the presenter runs");
        return IsWindowVisible(window); // "was visible before", as ShowWindow reports
    }
```

Add `HWND main_window();` to `src/wing32/a2.h` ("The game's main window (Glue), or nullptr before it exists."), implemented in `a2.cpp` as `HWND main_window() { return g_glue; }`.

- [ ] **Step 4: Run the tests** — `cmake --build --preset x86-debug; ctest --preset x86-debug`: all pass.
- [ ] **Step 5: Commit** — `git commit -m "Keep the game's hidden window restored while the presenter runs"` with the Co-Authored-By line; push after checking the PR is open. Re-run run B with the user.

#### 5b: the game is not told about the non-client deactivation at start-up

- [ ] **Step 1: Write the failing test** — append to `tests/core/input_map_test.cpp`:

```cpp
TEST_CASE("the non-client deactivation follows the same rule", "[input_map]") {
    CHECK(game_activation(Background::Run, kWmNcActivate, 0, false) == 1u);
    CHECK(game_activation(Background::Pause, kWmNcActivate, 0, true) == 1u);
    CHECK(game_activation(Background::Pause, kWmNcActivate, 1, false) == 0u);
}
```

and in `tests/platform/window_subclass_test.cpp`: `static_assert(hr::core::win::kWmNcActivate == WM_NCACTIVATE);`

- [ ] **Step 2: Run it to verify it fails** — `kWmNcActivate` is undeclared.
- [ ] **Step 3: Implement** — `inline constexpr unsigned kWmNcActivate = 0x0086;` in `core::win`. In `filter_activation`, treat `kWmNcActivate` like `kWmActivateApp` (`wparam == 0` → `1`). In `game_activation`'s inactive branch, return `0` for `kWmNcActivate` as for `kWmActivateApp`. In `glue_proc`, rewrite `WM_NCACTIVATE` as well:

```cpp
        if (msg == WM_ACTIVATE || msg == WM_ACTIVATEAPP || msg == WM_NCACTIVATE) {
```

- [ ] **Step 4: Run the tests** — `ctest --preset x86-debug`: all pass, including the existing `filter_activation` cases.
- [ ] **Step 5: Commit** — `git commit -m "Hide the start-up non-client deactivation from the game"` with the Co-Authored-By line; push after checking the PR is open. Re-run the start-up check with the user and compare the `diag: brightness` lines.

---

#### Added after the evidence run (agreed with the user, see `a2-4-exit.md` §1)

The evidence matched none of the rows for 5a/5b (no minimise, no dip). Three other fixes were agreed instead, each its own red/green cycle and commit:

- **5c:** `Glue` is kept out of the taskbar and alt-tab (`WS_EX_TOOLWINDOW` while hidden, in `platform::hide_window` / `unhide_window`). If it becomes the foreground window anyway, the presenter takes the foreground back.
- **5d:** `background = pause` freezes the game:
  - A `core::PauseClock` gives the game a clock that stands still while it is held; it is served through `GetTickCount` / `timeGetTime` hooks.
  - `platform::wait_handling_sent_messages` holds a thread while handling sent messages.
  - The `PeekMessageA` / `GetMessageA` hooks hold the game thread once the game has handled the inactive messages. Focus, close or shutdown releases it.
- **5e:** `core::FrameStats` names the slow part of the slowest frame (`slowest 64.7 ms (capture 60.1)`).

---

### Task 6: Closing smoke test, exit notes, spec

**Files:**
- Modify: `docs/smoke-test.md`, `docs/re-notes/a2-4-exit.md`, `docs/superpowers/specs/2026-10-05-stage-a2-presenter-design.md`

This task needs the user. Ask before installing into the game folder or changing `horned-rat.ini`.

- [ ] **Step 1: Add the A2.4 items to `docs/smoke-test.md`** (after "A2 cursor (from A2.3)"):

```markdown
### A2 background (from A2.4)
- [ ] `background = run`: alt-tab away in a battle; the game keeps playing (music, units move) and
      the picture is right on return.
- [ ] `background = pause`: alt-tab away; the game stops while away and resumes on return.
- [ ] With either mode, a game dialog (the exit question) does not count as leaving the game.
- [ ] Second monitor (if available): the presenter on the second monitor, the focus on the first;
      the game behaves as its `background` mode says.
- [ ] `horned-rat.log`: the 10 s lines show `slowest` below ~35 ms in normal play; at start-up the
      `diag: brightness` lines show no dip the user can see.
```

- [ ] **Step 2: Install the release build (ask the user first)**

Run: `. ./tools/vsenv.ps1; cmake --build --preset x86-release; ./tools/install.ps1 -GameDir 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat' -SourceDll build/x86-release/bin/wing32.dll`

- [ ] **Step 3: The user runs the closing smoke test of Stage A2**

The full checklist of `docs/smoke-test.md`: the base items, A2 presenter, scaling, cursor and background. Include the items still untested from A2.3: `aspect = stretch`, and Windows display scaling at 125 % or 150 % if the user can switch it. Use one run per `background` mode, with permission to edit the ini between runs. Record pass / fail / not tested per item.

- [ ] **Step 4: Restore the ini (ask first)** — remove every line added for the runs, so the user's ini is as it was before A2.4.

- [ ] **Step 5: Complete `docs/re-notes/a2-4-exit.md`** — add after §1:
- §2 smoke-test table (the whole checklist: this is Stage A2's exit);
- §3 performance (the 10 s lines with `slowest`, menu and battle);
- §4 bugs found and fixed (Task 5 rows applied, and anything the smoke test found);
- §5 open items carried out of Stage A2.

- [ ] **Step 6: Update the spec**
- §5.4: replace "with `background = pause` they pass unchanged" with decisions 1–2 (the application's state, the posted messages). Add 5a / 5b if applied.
- Add "§5.8 Decisions made in A2.4" with decisions 3–5 and the Task 5 outcome.
- §7: the performance line now includes the slowest frame.
- §10: add the A2.4 results and a one-line "Stage A2 complete" status with what remains open.
- The header's **Status:** add "A2.1–A2.4 complete (2026-10-…)".

- [ ] **Step 7: Commit, push, mark the PR ready**

Check the PR is still open first (`gh pr view --json state`). If it was merged, put the docs commit on a new branch from `origin/main` and open a follow-up PR.

```bash
git add docs
git commit -m "docs: A2.4 exit results, Stage A2 complete" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
gh pr ready
```

**Milestone A2.4 and Stage A2 are complete.** Next: decide with the user what follows Stage A2 (spec §1 non-goals: Stage B, other platforms).
