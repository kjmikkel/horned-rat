# Stage A2.3: Cursor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show the game's own cursor over the presenter, either drawn by the presenter and enlarged with the picture (`cursor = scaled`) or set as the Windows cursor (`cursor = native`), and fix the A2.2 known issue: in the books menu the game's cursor and the item hover subtitles are missing.

**Architecture:** The plan starts with evidence. Two diagnostic hooks and an opt-in message census show which cursors the game loads and how it sets them. They also show which messages its windows receive under the presenter. The expected cause is that Windows sends `WM_SETCURSOR` only to the window under the *real* cursor, which is always the presenter. If the evidence confirms this, the presenter forwards `WM_SETCURSOR` asynchronously to the game child under the virtual cursor before each forwarded mouse message, just as Windows would. It then works out the cursor the game wants: the one the game set with `SetCursor` meanwhile, else the hit window's class cursor. On top of that, `core` gets the sprite placement and pixel maths, and `platform` reads cursor bitmaps and draws them as alpha-blended D3D11 sprites. `wing32/a2.cpp` wires both cursor modes into the per-frame function.

**Tech Stack:** C++20, MSVC x86 (`/W4 /WX`, static CRT), CMake/Ninja presets, Catch2 v3.8.1, Direct3D 11 + DXGI flip model, HLSL compiled at runtime, WARP for tests, Win32 thread hooks (`WH_CALLWNDPROC`, `WH_GETMESSAGE`) for diagnostics.

**Spec:** `docs/superpowers/specs/2026-10-05-stage-a2-presenter-design.md` (milestone A2.3; §5.3 Cursor, §6 `cursor = scaled | native`, §8). Known issue: `docs/re-notes/a2-2-exit.md` §4.

## Global Constraints

- **Layering:** `core` → `platform` → `wing32`. `core` includes no Windows headers. Win32 values used in `core` are mirrored in `core::win` and `static_assert`ed in a platform test.
- **TDD:** red/green TDD for every behaviour. Write the test, see it fail for the right reason, then implement. `wing32` glue is thin and is verified by the manual smoke test.
- **Language and format:** C++20, `/W4 /WX`. clang-format 19.1.7 (LLVM base, 4 spaces, 100 columns, `PointerAlignment: Left`). CI runs a format check, so run `clang-format -i` on touched files before committing.
- **Build and test** (PowerShell, repo root): `. ./tools/vsenv.ps1; cmake --build --preset x86-debug; ctest --preset x86-debug`.
  - To run one test binary with a tag: `./build/x86-debug/bin/core_tests.exe "[cursor_math]"`.
  - In PowerShell, give paths without quotes.
- **Game files:** never commit game files, logs, captures, probe CSVs or decompiled code. Ask the user before installing into the GOG game folder or changing `horned-rat.ini`, and restore ini changes at the end.
  - Game folder: `D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat`; the ini and log are in its `WARFB\`.
- **"Never worse than vanilla"** (spec §7): every presenter failure unhides the game window and leaves a playable game. A cursor that cannot be drawn falls back to the native cursor; it never disables the presenter.
- **Config** (spec §6): `[display] cursor = scaled | native`, default `scaled`. Invalid values warn and use the default.
- **Spec §5.3:**
  - `scaled`: "over its client area the presenter hides the system cursor and draws the recorded cursor (bitmap from `GetIconInfo`, converted to a texture, cached per handle) at g, scaled by the same integer factor as the picture, hotspot-corrected".
  - `native`: "on `WM_SETCURSOR` in the client area the presenter sets the recorded handle".
- **CI desktop:** tests must not assume a big screen; GitHub's Windows runners have a 1024×768 desktop. A test that moves the real cursor restores it, and `SKIP`s if the cursor cannot be moved.
- **Git and PR:**
  - Work on branch `stage-a2-3`.
  - Commit messages end with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
  - PR bodies end with `🤖 Generated with [Claude Code](https://claude.com/claude-code)`.
  - Turn Auto-fix on for the PR.
  - Before every push after Task 1, check the PR is still open (`gh pr view --json state`). If it was merged, continue on a new branch from `origin/main` with a follow-up PR.

## Decisions taken in this plan (record in the spec at Task 9)

1. **Evidence before the fix.** Task 2 adds diagnostics and a game run; Task 3's fix is built only if that run confirms the hypothesis (gate at the end of Task 2). The diagnostics stay in the code:
   - `LoadCursorA`, `SetClassLongA` and `RegisterClassA/ExA` are logged.
   - `SetCursor` changes are logged.
   - A message census is switched on by a new `[debug] message_census = false` key.
2. **`WM_SETCURSOR` forwarding.** Before each forwarded mouse message except the wheel, the presenter sends `WM_SETCURSOR(child, MAKELPARAM(HTCLIENT, msg))` to the deepest game child under g. It does this as Windows does before the mouse message.
   - The send uses `SendMessageCallbackA`, which is asynchronous, so the presenter thread never blocks on the game thread. The game might be inside a modal dialog that the presenter owns.
   - Nothing is sent while a window of the game thread has mouse capture; Windows sends none then either.
3. **Choosing the cursor after the round trip.** The completion callback runs on the presenter thread.
   - If the game called `SetCursor` while handling the message (a counter in the hook changed), that cursor wins.
   - Otherwise `DefWindowProc` applied the hit window's class cursor, which our IAT hook cannot see. The presenter reads that cursor with `GetClassLongPtrA(GCLP_HCURSOR)`.
   - If the class has no cursor, nothing changes.
4. **`PresenterInput::cursor()` returns what is shown over the client area,** and `nullptr` means *no* cursor (the presenter draws its own). `PresenterWindow::refresh_cursor()` applies a changed cursor at once. Without it, Windows would only ask again on the next mouse move.
5. **Scaled sprite placement and size:**
   - It is scaled by the picture's whole factor per axis, at least 1 (`cursor_scale`). This matches `integer` + `stretch` (3×2 on 1920×1080).
   - Its hotspot sits on the top-left corner of game pixel g.
   - It is drawn only while the real cursor is over the client area.
   - If a cursor cannot be converted, the presenter shows it natively instead.
6. **Cursor pixels:**
   - Monochrome "invert screen" pixels are drawn opaque white, which is what inversion looks like on the game's mostly dark screens.
   - A colour cursor without alpha takes its alpha from the AND mask. A colour pixel under a set AND bit is transparent if black, else opaque (XOR over a dark background).
7. **The sprite cache is never evicted.** The game has few cursors, and `LoadCursorA` handles are shared and live as long as the process.

## File Structure

| File | Responsibility |
|---|---|
| `src/core/message_census.{h,cpp}` (new) | `message_name`, `MessageCensus` (diagnostics) |
| `src/core/config.{h,cpp}` | + `debug.message_census`, + `display.cursor` (`CursorMode`) |
| `src/core/cursor_math.{h,cpp}` (new) | `cursor_after_set_cursor`, `cursor_scale`, `cursor_rect` |
| `src/core/cursor_bitmap.{h,cpp}` (new) | `cursor_pixels` (AND/XOR/colour → straight-alpha BGRA) |
| `src/platform/message_spy.{h,cpp}` (new) | `MessageSpy`: thread hooks feeding a `MessageCensus` |
| `src/platform/child_input.{h,cpp}` | + `send_set_cursor` |
| `src/platform/presenter_window.{h,cpp}` | `cursor()` semantics, `refresh_cursor`, `cursor_in_client` |
| `src/platform/cursor_image.{h,cpp}` (new) | `cursor_image(HCURSOR)` via `GetIconInfo` / `GetDIBits` |
| `src/platform/presenter_d3d11.{h,cpp}` | `add_sprite`, `has_sprite`, `draw_sprite`, alpha blend state |
| `src/wing32/hooks.cpp` | diagnostic hooks, `SetCursor` counter |
| `src/wing32/a2.{h,cpp}` | census, `WM_SETCURSOR` forwarding, shown cursor, sprites |
| `tests/core/message_census_test.cpp`, `cursor_math_test.cpp`, `cursor_bitmap_test.cpp` (new), `config_test.cpp` | core tests |
| `tests/platform/message_spy_test.cpp`, `cursor_image_test.cpp` (new), `child_input_test.cpp`, `presenter_window_test.cpp`, `presenter_d3d11_test.cpp` | platform tests |
| `docs/smoke-test.md`, `docs/re-notes/a2-3-exit.md` (new), A2 spec | investigation notes and exit |

---

### Task 1: Message census and its config key (core)

**Files:**
- Create: `src/core/message_census.h`, `src/core/message_census.cpp`, `tests/core/message_census_test.cpp`
- Modify: `src/core/config.h`, `src/core/config.cpp`, `tests/core/config_test.cpp`, `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`

**Interfaces:**
- Consumes: `core::load_config`, `core::default_config_ini` (`core/config.h`).
- Produces:
  - `std::string core::message_name(unsigned message)`
  - `class core::MessageCensus { void add(std::string_view window_class, unsigned message); std::vector<std::string> take(); bool empty() const; }`
  - `bool core::Config::message_census` (default `false`, key `debug.message_census`)

- [ ] **Step 1: Create the branch**

```bash
git switch main
git pull
git switch -c stage-a2-3
```

- [ ] **Step 2: Write the failing tests**

`tests/core/message_census_test.cpp`:

```cpp
#include "core/message_census.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace hr::core;

TEST_CASE("message names: known names, WM_USER/WM_APP offsets, else hex", "[message_census]") {
    CHECK(message_name(0x0020) == "WM_SETCURSOR");
    CHECK(message_name(0x0200) == "WM_MOUSEMOVE");
    CHECK(message_name(0x0084) == "WM_NCHITTEST");
    CHECK(message_name(0x0405) == "WM_USER+5");
    CHECK(message_name(0x8001) == "WM_APP+1");
    CHECK(message_name(0x0123) == "0x0123");
    CHECK(message_name(0xC0A1) == "0xC0A1");
}

TEST_CASE("the census counts per window class and message, sorted, and clears on take",
          "[message_census]") {
    MessageCensus census;
    CHECK(census.empty());
    census.add("Glue", 0x0200);
    census.add("BitmapScrn", 0x0020);
    census.add("Glue", 0x0200);
    census.add("BitmapScrn", 0x0200);
    census.add("Glue", 0x0020);
    CHECK_FALSE(census.empty());
    CHECK(census.take() == std::vector<std::string>{
                               "BitmapScrn WM_SETCURSOR x1", "BitmapScrn WM_MOUSEMOVE x1",
                               "Glue WM_SETCURSOR x1", "Glue WM_MOUSEMOVE x2"});
    CHECK(census.empty());
    CHECK(census.take().empty());
}
```

In `tests/core/config_test.cpp`, extend `"every key is read"`. Change its `[debug]` line to `"[debug]\nlog = off\ngame_debug = 0\nmessage_census = on\n"` and add at the end of the test case:

```cpp
    CHECK(r.config.message_census);
```

Add `message_census_test.cpp` to `add_executable(core_tests ...)` in `tests/core/CMakeLists.txt`.

- [ ] **Step 3: Run the tests to verify they fail**

Run: `cmake --build --preset x86-debug`
Expected: compile errors: `core/message_census.h` not found and `message_census` is not a member of `Config`.

- [ ] **Step 4: Implement**

`src/core/message_census.h`:

```cpp
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hr::core {

// "WM_SETCURSOR" for the messages the cursor investigation cares about, "WM_USER+5" and
// "WM_APP+1" in those ranges, else the number as "0x0123".
std::string message_name(unsigned message);

// Counts messages per (window class, message) for the diagnostic message census (A2.3).
class MessageCensus {
  public:
    void add(std::string_view window_class, unsigned message);
    // One line per pair, sorted by class, then message number: "BitmapScrn WM_SETCURSOR x12".
    // Clears the counts.
    std::vector<std::string> take();
    bool empty() const { return counts_.empty(); }

  private:
    std::map<std::pair<std::string, unsigned>, std::uint64_t> counts_;
};

} // namespace hr::core
```

`src/core/message_census.cpp`:

```cpp
#include "core/message_census.h"

#include <cstdio>

namespace hr::core {
namespace {

struct Known {
    unsigned message;
    const char* name;
};

// Win32 message numbers (winuser.h).
constexpr Known kKnown[] = {
    {0x0006, "WM_ACTIVATE"},       {0x0007, "WM_SETFOCUS"},
    {0x0008, "WM_KILLFOCUS"},      {0x000C, "WM_SETTEXT"},
    {0x000F, "WM_PAINT"},          {0x0014, "WM_ERASEBKGND"},
    {0x0018, "WM_SHOWWINDOW"},     {0x001C, "WM_ACTIVATEAPP"},
    {0x0020, "WM_SETCURSOR"},      {0x0021, "WM_MOUSEACTIVATE"},
    {0x0046, "WM_WINDOWPOSCHANGING"}, {0x0047, "WM_WINDOWPOSCHANGED"},
    {0x0084, "WM_NCHITTEST"},      {0x0100, "WM_KEYDOWN"},
    {0x0101, "WM_KEYUP"},          {0x0102, "WM_CHAR"},
    {0x0111, "WM_COMMAND"},        {0x0113, "WM_TIMER"},
    {0x0200, "WM_MOUSEMOVE"},      {0x0201, "WM_LBUTTONDOWN"},
    {0x0202, "WM_LBUTTONUP"},      {0x0203, "WM_LBUTTONDBLCLK"},
    {0x0204, "WM_RBUTTONDOWN"},    {0x0205, "WM_RBUTTONUP"},
    {0x020A, "WM_MOUSEWHEEL"},     {0x0215, "WM_CAPTURECHANGED"},
    {0x02A1, "WM_MOUSEHOVER"},     {0x02A3, "WM_MOUSELEAVE"},
};
constexpr unsigned kWmUser = 0x0400;
constexpr unsigned kWmApp = 0x8000;
constexpr unsigned kFirstRegistered = 0xC000;

} // namespace

std::string message_name(unsigned message) {
    for (const auto& known : kKnown) {
        if (known.message == message) {
            return known.name;
        }
    }
    if (message >= kWmUser && message < kWmApp) {
        return "WM_USER+" + std::to_string(message - kWmUser);
    }
    if (message >= kWmApp && message < kFirstRegistered) {
        return "WM_APP+" + std::to_string(message - kWmApp);
    }
    char text[16];
    std::snprintf(text, sizeof text, "0x%04X", message);
    return text;
}

void MessageCensus::add(std::string_view window_class, unsigned message) {
    ++counts_[{std::string(window_class), message}];
}

std::vector<std::string> MessageCensus::take() {
    std::vector<std::string> lines;
    lines.reserve(counts_.size());
    for (const auto& [key, count] : counts_) {
        lines.push_back(key.first + " " + message_name(key.second) + " x" + std::to_string(count));
    }
    counts_.clear();
    return lines;
}

} // namespace hr::core
```

Add `message_census.cpp` to `add_library(hr_core STATIC ...)` in `src/core/CMakeLists.txt`.

In `src/core/config.h`, after `bool game_debug = true;`:

```cpp
    bool message_census = false; // diagnostics: log the messages the game's windows receive
```

In `src/core/config.cpp`, add to `kFields` after `debug.game_debug`:

```cpp
    {"debug.message_census",
     [](Config& c, std::string_view v) { return assign(c.message_census, parse_bool(v)); }},
```

In `default_config_ini()`, add the line after `game_debug`:

```
message_census = false     ; log which messages the game's windows receive (diagnostics)
```

- [ ] **Step 5: Run the tests to verify they pass**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/core_tests.exe "[message_census],[config]"`
Expected: all pass, including `"the default file parses back to the defaults"`. Then `ctest --preset x86-debug`: all pass.

- [ ] **Step 6: Commit, push and open the PR**

```bash
clang-format -i src/core/message_census.h src/core/message_census.cpp src/core/config.h src/core/config.cpp tests/core/message_census_test.cpp tests/core/config_test.cpp
git add src/core tests/core
git commit -m "core: message census and the debug.message_census key" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push -u origin stage-a2-3
gh pr create --draft --base main --title "Stage A2.3: cursor" --body "Implements milestone **A2.3** of \`docs/superpowers/specs/2026-10-05-stage-a2-presenter-design.md\` and investigates the books-menu cursor / hover-subtitle issue from \`docs/re-notes/a2-2-exit.md\` §4. Plan: \`docs/superpowers/plans/2026-10-06-stage-a2-3-cursor.md\`. Draft until Task 9.

🤖 Generated with [Claude Code](https://claude.com/claude-code)"
```

Then bind the PR in the app and turn Auto-fix on.

---

### Task 2: Diagnostics in the game, and the investigation run

**Files:**
- Create: `src/platform/message_spy.h`, `src/platform/message_spy.cpp`, `tests/platform/message_spy_test.cpp`, `docs/re-notes/a2-3-exit.md`
- Modify: `src/platform/CMakeLists.txt`, `tests/platform/CMakeLists.txt`, `src/wing32/hooks.cpp`, `src/wing32/a2.cpp`

**Interfaces:**
- Consumes: `core::MessageCensus`, `core::Config::message_census` (Task 1); `hr::test::make_window`, `hr::test::pump` (`tests/platform/test_window.h`; the test window class is `"HrRecordingWindow"`).
- Produces:
  - `class platform::MessageSpy { using Sink = std::function<void(const std::vector<std::string>&)>; MessageSpy(std::chrono::milliseconds period, Sink sink); bool installed() const; void count(HWND, UINT); }`
  - Log lines `diag: LoadCursorA(...)`, `diag: SetClassLongA(...)`, `diag: RegisterClassA(...)`, `SetCursor #n ...`, `diag: census <class> <message> x<n>`.

- [ ] **Step 1: Write the failing test**

`tests/platform/message_spy_test.cpp`:

```cpp
#include "platform/message_spy.h"
#include "test_window.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using hr::platform::MessageSpy;

namespace {
bool contains(const std::vector<std::string>& lines, const std::string& line) {
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}
} // namespace

TEST_CASE("the message spy counts sent and posted messages per window class", "[message_spy]") {
    std::vector<std::string> lines;
    {
        MessageSpy spy(std::chrono::milliseconds(0), [&](const std::vector<std::string>& census) {
            lines.insert(lines.end(), census.begin(), census.end());
        });
        REQUIRE(spy.installed());
        const HWND window = hr::test::make_window(0, 0, 50, 50);
        hr::test::pump();
        lines.clear();

        SendMessageA(window, WM_SETCURSOR, reinterpret_cast<WPARAM>(window),
                     MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        PostMessageA(window, WM_USER + 5, 0, 0);
        hr::test::pump();
        DestroyWindow(window);
    }
    CHECK(contains(lines, "HrRecordingWindow WM_SETCURSOR x1"));
    CHECK(contains(lines, "HrRecordingWindow WM_USER+5 x1"));
}

TEST_CASE("the census is handed over only once its period has passed", "[message_spy]") {
    int calls = 0;
    MessageSpy spy(std::chrono::hours(1), [&](const std::vector<std::string>&) { ++calls; });
    REQUIRE(spy.installed());
    const HWND window = hr::test::make_window(0, 0, 50, 50);
    SendMessageA(window, WM_USER + 1, 0, 0);
    hr::test::pump();
    DestroyWindow(window);
    CHECK(calls == 0);
}
```

Add `message_spy_test.cpp` to `add_executable(platform_tests ...)` in `tests/platform/CMakeLists.txt`.

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset x86-debug`
Expected: compile error, `platform/message_spy.h` not found.

- [ ] **Step 3: Implement the spy**

`src/platform/message_spy.h`:

```cpp
#pragma once

#include "core/message_census.h"

#include <windows.h>

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace hr::platform {

// Diagnostics (A2.3). Counts every message the windows of the constructing thread receive: sent
// ones (WH_CALLWNDPROC) and posted ones (WH_GETMESSAGE). When a message arrives and `period` has
// passed since the last hand-over, it passes the census to `sink` on that thread. Only one spy
// may exist at a time.
class MessageSpy {
  public:
    using Sink = std::function<void(const std::vector<std::string>& lines)>;

    MessageSpy(std::chrono::milliseconds period, Sink sink);
    ~MessageSpy();
    MessageSpy(const MessageSpy&) = delete;
    MessageSpy& operator=(const MessageSpy&) = delete;

    bool installed() const { return call_hook_ != nullptr && get_hook_ != nullptr; }
    void count(HWND window, UINT msg); // called by the hook procedures

  private:
    std::chrono::milliseconds period_;
    Sink sink_;
    core::MessageCensus census_;
    std::chrono::steady_clock::time_point last_;
    HHOOK call_hook_ = nullptr;
    HHOOK get_hook_ = nullptr;
};

} // namespace hr::platform
```

`src/platform/message_spy.cpp`:

```cpp
#include "platform/message_spy.h"

#include "platform/guard.h"

#include <cstdio>
#include <utility>

namespace hr::platform {
namespace {

MessageSpy* g_spy = nullptr; // only touched on the spied thread

LRESULT CALLBACK call_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && g_spy != nullptr) {
        const auto* sent = reinterpret_cast<const CWPSTRUCT*>(lparam);
        g_spy->count(sent->hwnd, sent->message);
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

// Also called for PeekMessage(PM_NOREMOVE); only removed messages are counted.
LRESULT CALLBACK get_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && wparam == PM_REMOVE && g_spy != nullptr) {
        const auto* posted = reinterpret_cast<const MSG*>(lparam);
        if (posted->hwnd != nullptr) {
            g_spy->count(posted->hwnd, posted->message);
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

} // namespace

MessageSpy::MessageSpy(std::chrono::milliseconds period, Sink sink)
    : period_(period), sink_(std::move(sink)), last_(std::chrono::steady_clock::now()) {
    g_spy = this;
    const DWORD thread = GetCurrentThreadId();
    call_hook_ = SetWindowsHookExA(WH_CALLWNDPROC, &call_proc, nullptr, thread);
    get_hook_ = SetWindowsHookExA(WH_GETMESSAGE, &get_proc, nullptr, thread);
}

MessageSpy::~MessageSpy() {
    if (call_hook_ != nullptr) {
        UnhookWindowsHookEx(call_hook_);
    }
    if (get_hook_ != nullptr) {
        UnhookWindowsHookEx(get_hook_);
    }
    g_spy = nullptr;
}

void MessageSpy::count(HWND window, UINT msg) {
    guarded(
        "message spy",
        [&] {
            char name[64] = {};
            if (GetClassNameA(window, name, sizeof name) == 0) {
                std::snprintf(name, sizeof name, "?");
            }
            census_.add(name, msg);
            const auto now = std::chrono::steady_clock::now();
            if (now - last_ >= period_) {
                last_ = now;
                sink_(census_.take());
            }
        },
        [] {});
}

} // namespace hr::platform
```

Add `message_spy.cpp` to `add_library(hr_platform STATIC ...)` in `src/platform/CMakeLists.txt`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/platform_tests.exe "[message_spy]"`
Expected: 2 test cases pass. Then `ctest --preset x86-debug`: all pass.

- [ ] **Step 5: Diagnostic hooks in `src/wing32/hooks.cpp`** (glue, verified by the run below)

Add `#include <set>` to the standard includes.

Add these members to `Originals` after `SetCursor`:

```cpp
    decltype(&::LoadCursorA) LoadCursorA;
    decltype(&::SetClassLongA) SetClassLongA;
    decltype(&::RegisterClassA) RegisterClassA;
    decltype(&::RegisterClassExA) RegisterClassExA;
```

Add the matching lookups after `get(user32, "SetCursor", r.SetCursor);`:

```cpp
        get(user32, "LoadCursorA", r.LoadCursorA);
        get(user32, "SetClassLongA", r.SetClassLongA);
        get(user32, "RegisterClassA", r.RegisterClassA);
        get(user32, "RegisterClassExA", r.RegisterClassExA);
```

Add `set_cursor{0}` to the `std::atomic<std::uint64_t>` list in `Counters` (after `begin_paint{0}`).

Replace `hook_SetCursor` and add the new hooks after it:

```cpp
HCURSOR WINAPI hook_SetCursor(HCURSOR cursor) {
    const void* caller = _ReturnAddress();
    platform::guarded(
        "SetCursor",
        [&] {
            a2::set_game_cursor(cursor);
            static std::atomic<HCURSOR> last{nullptr};
            if (last.exchange(cursor) != cursor) {
                if (const auto n = sample(g_counters.set_cursor)) {
                    trace("SetCursor", n, caller, "cursor=" + hex(cursor));
                }
            }
        },
        [] {});
    return originals().SetCursor(cursor);
}

// ---- Cursor sources (diagnostics for the A2.3 investigation) ----

std::string resource_name(LPCSTR name) {
    if (IS_INTRESOURCE(name)) {
        return "#" + std::to_string(reinterpret_cast<std::uintptr_t>(name));
    }
    return std::string("\"") + name + "\"";
}

std::string instance_name(HINSTANCE instance) {
    if (instance == nullptr) {
        return "system";
    }
    const std::string path = platform::module_path(instance);
    const auto slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

HCURSOR WINAPI hook_LoadCursorA(HINSTANCE instance, LPCSTR name) {
    const void* caller = _ReturnAddress();
    const HCURSOR result = originals().LoadCursorA(instance, name);
    const DWORD error = GetLastError();
    platform::guarded(
        "LoadCursorA",
        [&] {
            // Once per handle: some games load their cursor on every WM_SETCURSOR.
            static std::mutex mutex;
            static std::set<HCURSOR> seen;
            const std::lock_guard lock(mutex);
            if (seen.insert(result).second) {
                platform::log("diag: LoadCursorA(" + instance_name(instance) + ", " +
                              resource_name(name) + ") from " +
                              platform::module_file_name_from_address(caller) + " -> " +
                              hex(result));
            }
        },
        [] {});
    SetLastError(error);
    return result;
}

DWORD WINAPI hook_SetClassLongA(HWND window, int index, LONG value) {
    const void* caller = _ReturnAddress();
    const DWORD result = originals().SetClassLongA(window, index, value);
    const DWORD error = GetLastError();
    platform::guarded(
        "SetClassLongA",
        [&] {
            platform::log("diag: SetClassLongA(class '" + window_class(window) + "', " +
                          std::to_string(index) + (index == GCL_HCURSOR ? " GCL_HCURSOR" : "") +
                          ", " +
                          hex(reinterpret_cast<const void*>(
                              static_cast<std::uintptr_t>(static_cast<DWORD>(value)))) +
                          ") from " + platform::module_file_name_from_address(caller));
        },
        [] {});
    SetLastError(error);
    return result;
}

void log_class(const char* function, LPCSTR name, HCURSOR cursor, const void* caller) {
    platform::log(std::string("diag: ") + function + "(" + resource_name(name) + ", cursor " +
                  hex(cursor) + ") from " + platform::module_file_name_from_address(caller));
}

ATOM WINAPI hook_RegisterClassA(const WNDCLASSA* wc) {
    const void* caller = _ReturnAddress();
    const ATOM result = originals().RegisterClassA(wc);
    const DWORD error = GetLastError();
    platform::guarded(
        "RegisterClassA",
        [&] {
            if (wc != nullptr) {
                log_class("RegisterClassA", wc->lpszClassName, wc->hCursor, caller);
            }
        },
        [] {});
    SetLastError(error);
    return result;
}

ATOM WINAPI hook_RegisterClassExA(const WNDCLASSEXA* wc) {
    const void* caller = _ReturnAddress();
    const ATOM result = originals().RegisterClassExA(wc);
    const DWORD error = GetLastError();
    platform::guarded(
        "RegisterClassExA",
        [&] {
            if (wc != nullptr) {
                log_class("RegisterClassExA", wc->lpszClassName, wc->hCursor, caller);
            }
        },
        [] {});
    SetLastError(error);
    return result;
}
```

Add to `kHooks` after the `SetCursor` entry:

```cpp
    {"USER32.dll", "LoadCursorA", reinterpret_cast<void*>(&hook_LoadCursorA)},
    {"USER32.dll", "SetClassLongA", reinterpret_cast<void*>(&hook_SetClassLongA)},
    {"USER32.dll", "RegisterClassA", reinterpret_cast<void*>(&hook_RegisterClassA)},
    {"USER32.dll", "RegisterClassExA", reinterpret_cast<void*>(&hook_RegisterClassExA)},
```

`hook_import` stays silent for functions a module does not import, so listing all four is harmless.

- [ ] **Step 6: Start the census in `src/wing32/a2.cpp`**

Add `#include "platform/message_spy.h"` and `#include <vector>`. In `on_window_created`, after `platform::log("presenter: starting for the main window (hidden at creation)");` and before the thread starts:

```cpp
    if (settings().config.message_census) {
        // On the game thread, so the spy sees that thread's windows. Never destroyed: the game's
        // windows live until the process ends.
        static platform::MessageSpy spy(std::chrono::seconds(5),
                                        [](const std::vector<std::string>& lines) {
                                            for (const auto& line : lines) {
                                                platform::log("diag: census " + line);
                                            }
                                        });
        platform::log(std::string("diag: message census ") +
                      (spy.installed() ? "on" : "could not be installed"));
    }
```

- [ ] **Step 7: Build, test and commit**

Run: `cmake --build --preset x86-debug; ctest --preset x86-debug; cmake --build --preset x86-release; ctest --preset x86-release`
Expected: no warnings; all tests pass.

```bash
clang-format -i src/platform/message_spy.h src/platform/message_spy.cpp tests/platform/message_spy_test.cpp src/wing32/hooks.cpp src/wing32/a2.cpp
git add src tests
git commit -m "Cursor diagnostics: cursor-source hooks and an opt-in message census" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

- [ ] **Step 8: Investigation run A (ask the user first)**

Ask the user for permission to:
- install the release build: `./tools/install.ps1 -GameDir 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat' -SourceDll build/x86-release/bin/wing32.dll`;
- add `message_census = true` under `[debug]` in `WARFB\horned-rat.ini` (it is restored in Task 9).

Then the user:
1. Starts the game and goes to the books menu.
2. Moves slowly over a few special items for about 20 s, watching the cursor and the subtitles.
3. Waits through one loading screen.
4. Quits.

- [ ] **Step 9: Read the evidence**

Run: `Select-String -Path 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat\WARFB\horned-rat.log' -Pattern 'diag: (LoadCursorA|SetClassLongA|RegisterClass|census|message census)|SetCursor #'`

Answer these questions:
1. **Which cursors exist?** Look at the `LoadCursorA` resource names (from `gmcur.dll`? system `#32514` = `IDC_WAIT`?).
2. **How are cursors set?**
   - Does a window class get a cursor: `RegisterClass` with a non-null cursor, or `SetClassLongA ... GCL_HCURSOR`?
   - Which handles reach `SetCursor`, and from which module?
3. **What reaches the game's windows?** Does any game window class get `WM_SETCURSOR` in the census? It is expected to get none while the presenter runs. Which classes get `WM_MOUSEMOVE` in the books menu?

Create `docs/re-notes/a2-3-exit.md`:

```markdown
# A2.3 exit: cursor

## 1. Investigation (books-menu cursor and hover subtitles)

- **Run A:** <date>, build <commit>, `message_census = true`.
- **Cursors loaded:** <LoadCursorA lines, summarised: module, resource, handle>
- **Class cursors:** <RegisterClass / SetClassLongA findings>
- **SetCursor:** <handles, modules, when>
- **Messages under the presenter:** <census: WM_SETCURSOR yes/no per class; WM_MOUSEMOVE per class>
- **Conclusion:** <hypothesis confirmed / refuted, with the evidence>
```

Fill in the placeholders in angle brackets with what the log showed. Do not commit the log itself.

- [ ] **Step 10: Gate**

- **Continue with Task 3** if the census shows that game windows receive no `WM_SETCURSOR` while the presenter runs.
- **Otherwise stop.** Commit the notes, tell the user what the evidence shows, and revise this plan with them before writing any fix.

```bash
git add docs/re-notes/a2-3-exit.md
git commit -m "docs: A2.3 cursor investigation, run A" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

---

### Task 3: Forward `WM_SETCURSOR` to the game

**Files:**
- Create: `src/core/cursor_math.h`, `src/core/cursor_math.cpp`, `tests/core/cursor_math_test.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`, `src/platform/child_input.h`, `src/platform/child_input.cpp`, `tests/platform/child_input_test.cpp`, `src/wing32/a2.h`, `src/wing32/a2.cpp`

**Interfaces:**
- Consumes: `platform::deepest_child_at(HWND root, POINT root_client) -> ChildHit` and the file-local `thread_window(HWND root, bool capture)` (`child_input.cpp`); the `Tree` fixture and `got(...)` in `child_input_test.cpp` (root at (100,100) 300×200; `a` at (50,40) 100×80 in root; `b` at (10,10) 30×30 in `a`).
- Produces:
  - `std::uintptr_t core::cursor_after_set_cursor(bool game_called_set_cursor, std::uintptr_t current, std::uintptr_t class_cursor)`
  - `HWND platform::send_set_cursor(HWND root, POINT root_client, UINT mouse_msg, SENDASYNCPROC done, ULONG_PTR data)`

- [ ] **Step 1: Write the failing tests**

`tests/core/cursor_math_test.cpp`:

```cpp
#include "core/cursor_math.h"

#include <catch2/catch_test_macros.hpp>

using namespace hr::core;

TEST_CASE("after a forwarded WM_SETCURSOR the game's own SetCursor wins", "[cursor_math]") {
    CHECK(cursor_after_set_cursor(true, 0x10, 0x20) == 0x10);
}

TEST_CASE("without a SetCursor call the hit window's class cursor applies", "[cursor_math]") {
    CHECK(cursor_after_set_cursor(false, 0x10, 0x20) == 0x20);
}

TEST_CASE("without either the cursor stays as it was", "[cursor_math]") {
    CHECK(cursor_after_set_cursor(false, 0x10, 0) == 0x10);
}
```

Add `cursor_math_test.cpp` to `core_tests` in `tests/core/CMakeLists.txt`.

Append to `tests/platform/child_input_test.cpp`:

```cpp
namespace {
int g_done_calls = 0;
HWND g_done_window = nullptr;
ULONG_PTR g_done_data = 0;

void CALLBACK record_done(HWND window, UINT msg, ULONG_PTR data, LRESULT) {
    CHECK(msg == WM_SETCURSOR);
    ++g_done_calls;
    g_done_window = window;
    g_done_data = data;
}
} // namespace

TEST_CASE("send_set_cursor asks the child under the point, as Windows does before a click",
          "[child_input]") {
    Tree tree;
    g_done_calls = 0;
    // (65,55) in root is inside b: a starts at (50,40), b at (10,10) in a.
    const HWND target = send_set_cursor(tree.root, POINT{65, 55}, WM_LBUTTONDOWN, &record_done, 42);
    CHECK(target == tree.b);
    CHECK(got(tree.b, WM_SETCURSOR, reinterpret_cast<WPARAM>(tree.b),
              MAKELPARAM(HTCLIENT, WM_LBUTTONDOWN)));
    hr::test::pump();
    CHECK(g_done_calls == 1);
    CHECK(g_done_window == tree.b);
    CHECK(g_done_data == 42);
}

TEST_CASE("send_set_cursor sends nothing while a window has mouse capture", "[child_input]") {
    Tree tree;
    g_done_calls = 0;
    SetCapture(tree.a);
    CHECK(send_set_cursor(tree.root, POINT{65, 55}, WM_MOUSEMOVE, &record_done, 0) == nullptr);
    ReleaseCapture();
    hr::test::pump();
    CHECK(g_done_calls == 0);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset x86-debug`
Expected: compile errors: `core/cursor_math.h` not found and `send_set_cursor` undeclared.

- [ ] **Step 3: Implement**

`src/core/cursor_math.h`:

```cpp
#pragma once

#include <cstdint>

namespace hr::core {

// The cursor the game wants after it has handled a forwarded WM_SETCURSOR (A2.3), with handles as
// integers. If the game called SetCursor meanwhile, that cursor is already recorded as `current`.
// Otherwise DefWindowProc applied the hit window's class cursor, if it has one. Otherwise nothing
// changed.
std::uintptr_t cursor_after_set_cursor(bool game_called_set_cursor, std::uintptr_t current,
                                       std::uintptr_t class_cursor);

} // namespace hr::core
```

`src/core/cursor_math.cpp`:

```cpp
#include "core/cursor_math.h"

namespace hr::core {

std::uintptr_t cursor_after_set_cursor(bool game_called_set_cursor, std::uintptr_t current,
                                       std::uintptr_t class_cursor) {
    if (game_called_set_cursor || class_cursor == 0) {
        return current;
    }
    return class_cursor;
}

} // namespace hr::core
```

Add `cursor_math.cpp` to `hr_core` in `src/core/CMakeLists.txt`.

In `src/platform/child_input.h`, after `post_mouse`:

```cpp
// Windows sends WM_SETCURSOR to the window under the real cursor before each mouse message; under
// the presenter that is never a game window. This sends it asynchronously
// (SendMessageCallbackA) to the deepest child under `root_client`, with HTCLIENT and
// `mouse_msg`. Once root's thread has handled it, `done(window, WM_SETCURSOR, data, result)` runs
// on the calling thread while it pumps messages. Like Windows, it sends nothing while a window of
// root's thread has mouse capture. Returns the window it was sent to, else nullptr.
HWND send_set_cursor(HWND root, POINT root_client, UINT mouse_msg, SENDASYNCPROC done,
                     ULONG_PTR data);
```

In `src/platform/child_input.cpp`, after `post_mouse`:

```cpp
HWND send_set_cursor(HWND root, POINT root_client, UINT mouse_msg, SENDASYNCPROC done,
                     ULONG_PTR data) {
    if (thread_window(root, true) != nullptr) {
        return nullptr;
    }
    const HWND target = deepest_child_at(root, root_client).window;
    if (!SendMessageCallbackA(target, WM_SETCURSOR, reinterpret_cast<WPARAM>(target),
                              MAKELPARAM(HTCLIENT, mouse_msg), done, data)) {
        return nullptr;
    }
    return target;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/core_tests.exe "[cursor_math]"; ./build/x86-debug/bin/platform_tests.exe "[child_input]"`
Expected: all pass. Then `ctest --preset x86-debug`: all pass.

- [ ] **Step 5: Wire it into `src/wing32/a2.cpp`** (glue)

Add `#include "core/cursor_math.h"` and `#include <cstdint>`. After `std::atomic<HCURSOR> g_cursor{nullptr};` add:

```cpp
std::atomic<std::uint32_t> g_cursor_sets{0}; // SetCursor calls by the game so far
```

Add this before `class Input`:

```cpp
// Runs on the presenter thread once the game thread has handled a forwarded WM_SETCURSOR
// (platform::send_set_cursor; `sets_before` = g_cursor_sets when it was sent).
void CALLBACK on_set_cursor_done(HWND window, UINT, ULONG_PTR sets_before, LRESULT) {
    const bool game_set = g_cursor_sets.load() != static_cast<std::uint32_t>(sets_before);
    const auto class_cursor = static_cast<std::uintptr_t>(GetClassLongPtrA(window, GCLP_HCURSOR));
    const auto chosen = core::cursor_after_set_cursor(
        game_set, reinterpret_cast<std::uintptr_t>(g_cursor.load()), class_cursor);
    const auto next = reinterpret_cast<HCURSOR>(chosen);
    if (g_cursor.exchange(next) != next) {
        char line[160];
        std::snprintf(line, sizeof line, "diag: cursor 0x%p (%s '%s')", static_cast<void*>(next),
                      game_set ? "set by the game in" : "class cursor of",
                      class_of(window).c_str());
        platform::log(line);
    }
}
```

In `Input::on_mouse`, replace the forwarding `if` block with:

```cpp
        if (g_active && core::classify(msg, wparam) == core::Forward::Mouse) {
            const POINT root{content.x + g.x, content.y + g.y};
            if (msg != WM_MOUSEWHEEL) {
                platform::send_set_cursor(g_glue, root, msg, &on_set_cursor_done,
                                          g_cursor_sets.load());
            }
            platform::post_mouse(g_glue, msg, wparam, root);
        }
```

Replace `set_game_cursor`:

```cpp
void set_game_cursor(HCURSOR cursor) {
    g_cursor = cursor;
    ++g_cursor_sets;
}
```

In `src/wing32/a2.h`, change the comment above `set_game_cursor` to:

```cpp
// SetCursor: records the game's cursor and counts the call (a forwarded WM_SETCURSOR uses the
// count to tell whether the game chose a cursor itself).
```

- [ ] **Step 6: Build, test and commit**

Run: `cmake --build --preset x86-debug; ctest --preset x86-debug; cmake --build --preset x86-release; ctest --preset x86-release`
Expected: no warnings; all tests pass.

```bash
clang-format -i src/core/cursor_math.h src/core/cursor_math.cpp tests/core/cursor_math_test.cpp src/platform/child_input.h src/platform/child_input.cpp tests/platform/child_input_test.cpp src/wing32/a2.h src/wing32/a2.cpp
git add src tests
git commit -m "Forward WM_SETCURSOR to the game child under the virtual cursor" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

- [ ] **Step 7: Verification run B (ask the user first)**

Ask for permission to install the new release build (the same `install.ps1` command as Task 2; the ini keeps `message_census = true`). The user repeats run A. They report whether:
- the game's cursor shows in the books menu;
- the hover subtitles appear;
- the loading cursor goes away after loading.

Read the log: `diag: cursor` lines (which windows, class cursor or set by the game), and census lines showing `WM_SETCURSOR` now reaching game windows. Add a "Run B" paragraph to §1 of `docs/re-notes/a2-3-exit.md` with the result and the evidence.

- [ ] **Step 8: Gate**

- **Continue with Task 4** if the game's cursor now shows correctly.
- **Cursor fixed, subtitles still missing:** commit the notes and stop. Tell the user what the census shows and plan the subtitle investigation with them before going on.
- **Cursor still wrong:** stop and revise the plan with the user.

```bash
git add docs/re-notes/a2-3-exit.md
git commit -m "docs: A2.3 cursor investigation, run B" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

---

### Task 4: `cursor` config key, native mode and immediate cursor updates

**Files:**
- Modify: `src/core/config.h`, `src/core/config.cpp`, `tests/core/config_test.cpp`, `src/platform/presenter_window.h`, `src/platform/presenter_window.cpp`, `tests/platform/presenter_window_test.cpp`, `src/wing32/a2.cpp`

**Interfaces:**
- Consumes: `PresenterInput`, `PresenterWindow` (`platform/presenter_window.h`); `FakeInput` in `presenter_window_test.cpp`.
- Produces:
  - `enum class core::CursorMode { Scaled, Native }`, `core::Config::cursor` (default `Scaled`, key `display.cursor`)
  - `PresenterInput::cursor()`: the cursor shown over the client area, `nullptr` = none
  - `void platform::PresenterWindow::refresh_cursor()`
  - `bool platform::cursor_in_client(HWND window)`

- [ ] **Step 1: Write the failing tests**

In `tests/core/config_test.cpp`, extend `"every key is read"`. Change its `[display]` line to `"[display]\nmode = borderless\nwindow_size = 1920x1440\n"` `"scale = integer\naspect = stretch\nvsync = no\ncursor = native\n"` and add:

```cpp
    CHECK(r.config.cursor == CursorMode::Native);
```

Add:

```cpp
TEST_CASE("an invalid cursor mode warns and keeps scaled", "[config]") {
    const auto r = load_config("[display]\ncursor = huge\n");
    CHECK(r.config.cursor == CursorMode::Scaled);
    REQUIRE(r.warnings.size() == 1);
    CHECK_THAT(r.warnings[0], ContainsSubstring("display.cursor") && ContainsSubstring("huge"));
}
```

In `tests/platform/presenter_window_test.cpp`, change `FakeInput`'s cursor to:

```cpp
    HCURSOR shown = nullptr;
    HCURSOR cursor() override { return shown; }
```

and add:

```cpp
namespace {
HCURSOR system_cursor(int id) { return LoadCursorA(nullptr, MAKEINTRESOURCEA(id)); }
constexpr int kArrow = 32512; // IDC_ARROW
constexpr int kCross = 32515; // IDC_CROSS
} // namespace

TEST_CASE("over the client area the window shows the input's cursor; nullptr shows none",
          "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    const auto ask = [&] {
        SendMessageA(window.hwnd(), WM_SETCURSOR, reinterpret_cast<WPARAM>(window.hwnd()),
                     MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    };
    input.shown = system_cursor(kCross);
    ask();
    CHECK(GetCursor() == system_cursor(kCross));
    input.shown = nullptr;
    ask();
    CHECK(GetCursor() == nullptr);
    SetCursor(system_cursor(kArrow));
}

TEST_CASE("cursor_in_client and refresh_cursor follow the real cursor", "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {320, 240}, "test");
    window.show();
    SetWindowPos(window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    POINT saved{};
    GetCursorPos(&saved);
    POINT inside{160, 120};
    ClientToScreen(window.hwnd(), &inside);
    RECT frame{};
    GetWindowRect(window.hwnd(), &frame);
    const POINT outside{frame.left - 10, frame.top - 10};

    POINT now{};
    if (!SetCursorPos(inside.x, inside.y) || !GetCursorPos(&now) || now.x != inside.x ||
        now.y != inside.y) {
        SetCursorPos(saved.x, saved.y);
        SKIP("the real cursor cannot be moved on this desktop");
    }
    CHECK(hr::platform::cursor_in_client(window.hwnd()));
    input.shown = system_cursor(kCross);
    SetCursor(system_cursor(kArrow));
    window.refresh_cursor();
    CHECK(GetCursor() == system_cursor(kCross));

    SetCursorPos(outside.x, outside.y);
    CHECK_FALSE(hr::platform::cursor_in_client(window.hwnd()));
    SetCursor(system_cursor(kArrow));
    window.refresh_cursor();
    CHECK(GetCursor() == system_cursor(kArrow));
    SetCursorPos(saved.x, saved.y);
}
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset x86-debug`
Expected: compile errors (`CursorMode`, `cursor_in_client`, `refresh_cursor` unknown). Once those are declared, the `nullptr shows none` case still fails on `GetCursor() == nullptr`, because today's window shows the arrow for `nullptr`.

- [ ] **Step 3: Implement**

`src/core/config.h`: add `enum class CursorMode { Scaled, Native };` after `AspectMode`. In `Config`, after `bool vsync = true;`, add:

```cpp
    CursorMode cursor = CursorMode::Scaled;
```

`src/core/config.cpp`: add to `kFields` after `display.vsync`:

```cpp
    {"display.cursor",
     [](Config& c, std::string_view v) {
         return assign(c.cursor, parse_choice<CursorMode>(v, {{"scaled", CursorMode::Scaled},
                                                              {"native", CursorMode::Native}}));
     }},
```

and the default-file line after `vsync`:

```
cursor       = scaled      ; scaled (drawn with the picture) | native (Windows cursor)
```

`src/platform/presenter_window.h`: change the `cursor()` declaration to:

```cpp
    // The cursor shown over the client area; nullptr = none (the owner draws its own).
    virtual HCURSOR cursor() = 0;
```

Add to `PresenterWindow`'s public part:

```cpp
    // Shows input.cursor() at once if the real cursor is over the client area. Windows asks with
    // WM_SETCURSOR only when the mouse moves, so a changed cursor would otherwise wait for that.
    void refresh_cursor();
```

Add after the class:

```cpp
// True if the real cursor is over `window`'s client area and no other window covers that point.
// Any thread; coordinates in the calling thread's DPI context.
bool cursor_in_client(HWND window);
```

`src/platform/presenter_window.cpp`: replace the `WM_SETCURSOR` case:

```cpp
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            SetCursor(self->input_.cursor());
            return TRUE;
        }
        break;
```

Add after the window procedure:

```cpp
void PresenterWindow::refresh_cursor() {
    if (cursor_in_client(hwnd_)) {
        SetCursor(input_.cursor());
    }
}

bool cursor_in_client(HWND window) {
    POINT cursor{};
    if (window == nullptr || !GetCursorPos(&cursor) || WindowFromPoint(cursor) != window) {
        return false;
    }
    ScreenToClient(window, &cursor);
    RECT client{};
    GetClientRect(window, &client);
    return PtInRect(&client, cursor) != FALSE;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/core_tests.exe "[config]"; ./build/x86-debug/bin/platform_tests.exe "[presenter_window]"`
Expected: all pass (the real-cursor case may report as skipped on a desktop where the cursor cannot move). Then `ctest --preset x86-debug`: all pass.

- [ ] **Step 5: Wire the shown cursor into `src/wing32/a2.cpp`** (glue)

`Input` replaces `HCURSOR cursor() override { return g_cursor; }` with:

```cpp
    HCURSOR cursor() override { return shown_; }
    HCURSOR shown_cursor() const { return shown_; }
    void set_shown_cursor(HCURSOR cursor) { shown_ = cursor; }
```

and gains the member `HCURSOR shown_ = LoadCursorA(nullptr, IDC_ARROW);`.

In `presenter_body`, before the `frame` lambda:

```cpp
        const HCURSOR arrow = LoadCursorA(nullptr, IDC_ARROW);
        platform::log(std::string("presenter: cursor ") +
                      (config.cursor == core::CursorMode::Scaled ? "scaled" : "native"));
```

Inside the `frame` lambda, right after `input.set_dest(dest);`:

```cpp
            // Native for now; Task 8 adds the scaled sprite.
            const HCURSOR game_cursor = g_cursor;
            const HCURSOR shown = game_cursor != nullptr ? game_cursor : arrow;
            if (shown != input.shown_cursor()) {
                input.set_shown_cursor(shown);
                window.refresh_cursor();
            }
```

In `mask_button`, replace the block from `POINT cursor{};` to the end of `if (in_client) { ... }` with:

```cpp
    const bool in_client = platform::cursor_in_client(presenter);
```

- [ ] **Step 6: Build, test and commit**

Run: `cmake --build --preset x86-debug; ctest --preset x86-debug; cmake --build --preset x86-release; ctest --preset x86-release`
Expected: no warnings; all tests pass.

```bash
clang-format -i src/core/config.h src/core/config.cpp tests/core/config_test.cpp src/platform/presenter_window.h src/platform/presenter_window.cpp tests/platform/presenter_window_test.cpp src/wing32/a2.cpp
git add src tests
git commit -m "Cursor config key, native cursor shown at once when the game changes it" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

---

### Task 5: Scaled cursor placement (core)

**Files:**
- Modify: `src/core/cursor_math.h`, `src/core/cursor_math.cpp`, `tests/core/cursor_math_test.cpp`

**Interfaces:**
- Consumes: `core::Size`, `core::Point`, `core::Rect` (`core/geometry.h`).
- Produces:
  - `core::Size core::cursor_scale(core::Size content, core::Rect dest)`
  - `core::Rect core::cursor_rect(core::Point g, core::Size content, core::Rect dest, core::Size cursor, core::Point hotspot)`

- [ ] **Step 1: Write the failing tests** — append to `tests/core/cursor_math_test.cpp`:

```cpp
namespace {
constexpr Size kGame{640, 480};
}

TEST_CASE("the cursor scale is the picture's whole factor per axis, at least 1",
          "[cursor_math]") {
    CHECK(cursor_scale(kGame, {0, 0, 640, 480}) == Size{1, 1});
    CHECK(cursor_scale(kGame, {320, 60, 1280, 960}) == Size{2, 2});
    CHECK(cursor_scale(kGame, {240, 0, 1440, 1080}) == Size{2, 2});
    CHECK(cursor_scale(kGame, {0, 60, 1920, 960}) == Size{3, 2});
    CHECK(cursor_scale(kGame, {0, 0, 320, 240}) == Size{1, 1});
    CHECK(cursor_scale({0, 0}, {0, 0, 640, 480}) == Size{1, 1});
}

TEST_CASE("the cursor's hotspot sits on the top-left corner of game pixel g", "[cursor_math]") {
    // 1:1
    CHECK(cursor_rect({100, 50}, kGame, {0, 0, 640, 480}, {32, 32}, {5, 3}) ==
          Rect{95, 47, 32, 32});
    // integer 2x with bars
    CHECK(cursor_rect({100, 50}, kGame, {320, 60, 1280, 960}, {32, 32}, {5, 3}) ==
          Rect{510, 154, 64, 64});
    // sharp 2.25x: pixel edges round down
    CHECK(cursor_rect({639, 479}, kGame, {240, 0, 1440, 1080}, {32, 32}, {0, 0}) ==
          Rect{1677, 1077, 64, 64});
    // stretch 3x2
    CHECK(cursor_rect({10, 10}, kGame, {0, 60, 1920, 960}, {16, 16}, {1, 1}) ==
          Rect{27, 78, 48, 32});
}

TEST_CASE("a cursor near the top-left corner may start outside the client", "[cursor_math]") {
    CHECK(cursor_rect({0, 0}, kGame, {0, 0, 640, 480}, {32, 32}, {5, 3}) == Rect{-5, -3, 32, 32});
}
```

Check the expected values:
- **Integer 2×:** x = 320 + 100·1280/640 − 5·2 = 320 + 200 − 10 = 510; y = 60 + 50·960/480 − 3·2 = 60 + 100 − 6 = 154.
- **Sharp:** 639·1440/640 = 1437.75 → 1437, plus 240 = 1677; 479·1080/480 = 1077.75 → 1077.
- **Stretch:** x = 10·1920/640 − 1·3 = 27; y = 60 + 10·960/480 − 1·2 = 78; size 16·3 × 16·2 = 48×32.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset x86-debug`
Expected: compile errors, `cursor_scale` / `cursor_rect` undeclared.

- [ ] **Step 3: Implement** — add to `src/core/cursor_math.h` (with `#include "core/geometry.h"`):

```cpp
// The whole factor per axis by which the picture is enlarged: dest / content, at least 1.
Size cursor_scale(Size content, Rect dest);

// Where the scaled cursor goes in the presenter's client area (spec §5.3): `cursor` enlarged by
// cursor_scale, with its hotspot on the top-left corner of game pixel `g`. May extend past the
// client area.
Rect cursor_rect(Point g, Size content, Rect dest, Size cursor, Point hotspot);
```

and to `src/core/cursor_math.cpp` (with `#include <algorithm>`):

```cpp
Size cursor_scale(Size content, Rect dest) {
    if (content.width <= 0 || content.height <= 0) {
        return {1, 1};
    }
    return {std::max(1, dest.width / content.width), std::max(1, dest.height / content.height)};
}

Rect cursor_rect(Point g, Size content, Rect dest, Size cursor, Point hotspot) {
    const Size factor = cursor_scale(content, dest);
    const auto edge = [](int game, int dest_size, int content_size) {
        return content_size > 0
                   ? static_cast<int>(static_cast<long long>(game) * dest_size / content_size)
                   : 0;
    };
    return {dest.x + edge(g.x, dest.width, content.width) - hotspot.x * factor.width,
            dest.y + edge(g.y, dest.height, content.height) - hotspot.y * factor.height,
            cursor.width * factor.width, cursor.height * factor.height};
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/core_tests.exe "[cursor_math]"`
Expected: 6 test cases pass.

- [ ] **Step 5: Commit**

```bash
clang-format -i src/core/cursor_math.h src/core/cursor_math.cpp tests/core/cursor_math_test.cpp
git add src/core tests/core
git commit -m "core: scaled cursor size and hotspot position" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

---

### Task 6: Cursor bitmaps to pixels

**Files:**
- Create: `src/core/cursor_bitmap.h`, `src/core/cursor_bitmap.cpp`, `tests/core/cursor_bitmap_test.cpp`, `src/platform/cursor_image.h`, `src/platform/cursor_image.cpp`, `tests/platform/cursor_image_test.cpp`
- Modify: `src/core/CMakeLists.txt`, `tests/core/CMakeLists.txt`, `src/platform/CMakeLists.txt`, `tests/platform/CMakeLists.txt`

**Interfaces:**
- Produces:
  - `std::vector<std::uint32_t> core::cursor_pixels(core::Size size, const std::uint32_t* color, const std::uint8_t* mask)`
  - `struct platform::CursorImage { core::Size size; core::Point hotspot; std::vector<std::uint32_t> pixels; }`
  - `std::optional<platform::CursorImage> platform::cursor_image(HCURSOR cursor)`

- [ ] **Step 1: Write the failing tests**

`tests/core/cursor_bitmap_test.cpp`:

```cpp
#include "core/cursor_bitmap.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace hr::core;

TEST_CASE("monochrome cursors: black, white, transparent, and inverted drawn white",
          "[cursor_bitmap]") {
    // 4x1: AND row 0 0 1 1, then XOR row 0 1 0 1.
    const std::uint8_t mask[] = {0, 0, 1, 1, 0, 1, 0, 1};
    CHECK(cursor_pixels({4, 1}, nullptr, mask) ==
          std::vector<std::uint32_t>{0xFF000000u, 0xFFFFFFFFu, 0u, 0xFFFFFFFFu});
}

TEST_CASE("colour cursors with alpha keep their pixels", "[cursor_bitmap]") {
    const std::uint32_t color[] = {0x80FF0000u, 0x00000000u, 0xFF00FF00u};
    const std::uint8_t mask[] = {1, 1, 1};
    CHECK(cursor_pixels({3, 1}, color, mask) ==
          std::vector<std::uint32_t>{0x80FF0000u, 0u, 0xFF00FF00u});
}

TEST_CASE("colour cursors without alpha take it from the AND mask", "[cursor_bitmap]") {
    // clear: opaque colour (also black); set: transparent if black, else opaque (XOR on dark)
    const std::uint32_t color[] = {0x00FF0000u, 0x00000000u, 0x00000000u, 0x0000FF00u};
    const std::uint8_t mask[] = {0, 0, 1, 1};
    CHECK(cursor_pixels({2, 2}, color, mask) ==
          std::vector<std::uint32_t>{0xFFFF0000u, 0xFF000000u, 0u, 0xFF00FF00u});
}

TEST_CASE("an empty cursor has no pixels", "[cursor_bitmap]") {
    const std::uint8_t mask[] = {0};
    CHECK(cursor_pixels({0, 0}, nullptr, mask).empty());
}
```

`tests/platform/cursor_image_test.cpp`:

```cpp
#include "platform/cursor_image.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

using hr::platform::cursor_image;

TEST_CASE("a monochrome cursor is read with its size, hotspot and pixels", "[cursor_image]") {
    // 8x2 cursor: AND rows 0x00, 0xFF; XOR rows 0xF0, 0xF0 (1-bit rows padded to 16 bits).
    const BYTE bits[] = {0x00, 0, 0xFF, 0, 0xF0, 0, 0xF0, 0};
    const HBITMAP mask = CreateBitmap(8, 4, 1, 1, bits);
    ICONINFO info{FALSE, 3, 1, mask, nullptr};
    const HCURSOR cursor = CreateIconIndirect(&info);
    DeleteObject(mask);
    REQUIRE(cursor != nullptr);

    const auto image = cursor_image(cursor);
    DestroyCursor(cursor);
    REQUIRE(image);
    CHECK(image->size == hr::core::Size{8, 2});
    CHECK(image->hotspot == hr::core::Point{3, 1});
    constexpr std::uint32_t W = 0xFFFFFFFFu, B = 0xFF000000u, T = 0u;
    CHECK(image->pixels ==
          std::vector<std::uint32_t>{W, W, W, W, B, B, B, B, W, W, W, W, T, T, T, T});
}

TEST_CASE("a 32-bit cursor keeps its alpha channel", "[cursor_image]") {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 2;
    bi.bmiHeader.biHeight = -2; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    const HBITMAP color = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    REQUIRE(color != nullptr);
    // Only fully opaque or fully transparent (black) pixels: the result does not depend on
    // whether Windows stores the colours premultiplied.
    const std::uint32_t pixels[] = {0xFFFF0000u, 0x00000000u, 0xFF00FF00u, 0xFF0000FFu};
    std::memcpy(bits, pixels, sizeof pixels);
    const BYTE mask_bits[] = {0, 0, 0, 0};
    const HBITMAP mask = CreateBitmap(2, 2, 1, 1, mask_bits);
    ICONINFO info{FALSE, 0, 0, mask, color};
    const HCURSOR cursor = CreateIconIndirect(&info);
    DeleteObject(mask);
    DeleteObject(color);
    REQUIRE(cursor != nullptr);

    const auto image = cursor_image(cursor);
    DestroyCursor(cursor);
    REQUIRE(image);
    CHECK(image->size == hr::core::Size{2, 2});
    CHECK(image->pixels == std::vector<std::uint32_t>(std::begin(pixels), std::end(pixels)));
}

TEST_CASE("the system arrow can be read", "[cursor_image]") {
    const auto image = cursor_image(LoadCursorA(nullptr, MAKEINTRESOURCEA(32512)));
    REQUIRE(image);
    REQUIRE(image->size.width > 0);
    REQUIRE(image->size.height > 0);
    CHECK(image->pixels.size() ==
          static_cast<size_t>(image->size.width) * static_cast<size_t>(image->size.height));
    const auto opaque = [](std::uint32_t p) { return (p >> 24) == 0xFF; };
    const auto clear = [](std::uint32_t p) { return (p >> 24) == 0; };
    CHECK(std::any_of(image->pixels.begin(), image->pixels.end(), opaque));
    CHECK(std::any_of(image->pixels.begin(), image->pixels.end(), clear));
}

TEST_CASE("no cursor gives no image", "[cursor_image]") { CHECK_FALSE(cursor_image(nullptr)); }
```

Add `cursor_bitmap_test.cpp` to `core_tests` and `cursor_image_test.cpp` to `platform_tests`.

- [ ] **Step 2: Run the tests to verify they fail**

Run: `cmake --build --preset x86-debug`
Expected: compile errors, the headers are missing.

- [ ] **Step 3: Implement**

`src/core/cursor_bitmap.h`:

```cpp
#pragma once

#include "core/geometry.h"

#include <cstdint>
#include <vector>

namespace hr::core {

// Straight-alpha 0xAARRGGBB pixels of a Windows cursor (A2 spec §5.3), from its bitmaps as
// GetIconInfo gives them.
// - `mask`: one byte per pixel, nonzero = bit set; never null. A monochrome cursor
//   (`color` == nullptr) has 2 × size.height rows: the AND mask, then the XOR mask.
// - `color`: size.width × size.height pixels 0xAARRGGBB.
// Monochrome: AND clear → black (XOR clear) or white (XOR set); AND set → transparent (XOR clear),
// or screen inversion (XOR set), drawn opaque white: what inversion looks like on the game's dark
// screens.
// Colour: if any pixel has alpha, the pixels are used as they are. Otherwise the AND mask decides:
// clear → opaque colour; set → transparent where the colour is black, else opaque colour (XOR over
// a dark background).
std::vector<std::uint32_t> cursor_pixels(Size size, const std::uint32_t* color,
                                         const std::uint8_t* mask);

} // namespace hr::core
```

`src/core/cursor_bitmap.cpp`:

```cpp
#include "core/cursor_bitmap.h"

#include <algorithm>
#include <cstddef>

namespace hr::core {

std::vector<std::uint32_t> cursor_pixels(Size size, const std::uint32_t* color,
                                         const std::uint8_t* mask) {
    const std::size_t count =
        static_cast<std::size_t>(std::max(size.width, 0)) * std::max(size.height, 0);
    std::vector<std::uint32_t> out(count, 0u);
    constexpr std::uint32_t kOpaque = 0xFF000000u;
    constexpr std::uint32_t kWhite = 0xFFFFFFFFu;
    if (color == nullptr) {
        const std::uint8_t* const xor_mask = mask + count;
        for (std::size_t i = 0; i < count; ++i) {
            const bool xor_bit = xor_mask[i] != 0;
            if (mask[i] == 0) {
                out[i] = xor_bit ? kWhite : kOpaque;
            } else if (xor_bit) {
                out[i] = kWhite; // screen inversion
            }
        }
        return out;
    }
    const bool has_alpha =
        std::any_of(color, color + count, [](std::uint32_t p) { return (p >> 24) != 0; });
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t rgb = color[i] & 0x00FFFFFFu;
        if (has_alpha) {
            out[i] = color[i];
        } else if (mask[i] == 0 || rgb != 0) {
            out[i] = kOpaque | rgb;
        }
    }
    return out;
}

} // namespace hr::core
```

`src/platform/cursor_image.h`:

```cpp
#pragma once

#include "core/geometry.h"

#include <windows.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace hr::platform {

struct CursorImage {
    core::Size size;
    core::Point hotspot;
    std::vector<std::uint32_t> pixels; // straight-alpha 0xAARRGGBB (core::cursor_pixels)
};

// The picture of `cursor` (GetIconInfo), or nullopt if it cannot be read.
std::optional<CursorImage> cursor_image(HCURSOR cursor);

} // namespace hr::platform
```

`src/platform/cursor_image.cpp`:

```cpp
#include "platform/cursor_image.h"

#include "core/cursor_bitmap.h"

#include <cstddef>

namespace hr::platform {
namespace {

// GetIconInfo hands out copies of the cursor's bitmaps; they must be deleted.
struct IconBitmaps {
    ICONINFO info{};
    ~IconBitmaps() {
        if (info.hbmMask != nullptr) {
            DeleteObject(info.hbmMask);
        }
        if (info.hbmColor != nullptr) {
            DeleteObject(info.hbmColor);
        }
    }
};

BITMAPINFOHEADER top_down(int width, int height, WORD bits) {
    BITMAPINFOHEADER header{};
    header.biSize = sizeof header;
    header.biWidth = width;
    header.biHeight = -height;
    header.biPlanes = 1;
    header.biBitCount = bits;
    header.biCompression = BI_RGB;
    return header;
}

// A 1-bit bitmap as one byte per pixel, 1 = bit set (white in its colour table).
std::vector<std::uint8_t> read_mask(HDC dc, HBITMAP bitmap, int width, int height) {
    struct {
        BITMAPINFOHEADER header;
        RGBQUAD colors[2];
    } info{top_down(width, height, 1), {}};
    const int stride = (width + 31) / 32 * 4;
    std::vector<std::uint8_t> bits(static_cast<std::size_t>(stride) * height);
    if (GetDIBits(dc, bitmap, 0, static_cast<UINT>(height), bits.data(),
                  reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS) != height) {
        return {};
    }
    const bool inverted = info.colors[0].rgbRed != 0; // index 0 is white
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool bit = ((bits[static_cast<std::size_t>(y) * stride + x / 8] >>
                               (7 - x % 8)) & 1) != 0;
            mask[static_cast<std::size_t>(y) * width + x] = bit != inverted ? 1 : 0;
        }
    }
    return mask;
}

std::vector<std::uint32_t> read_color(HDC dc, HBITMAP bitmap, int width, int height) {
    BITMAPINFO info{};
    info.bmiHeader = top_down(width, height, 32);
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height);
    if (GetDIBits(dc, bitmap, 0, static_cast<UINT>(height), pixels.data(), &info,
                  DIB_RGB_COLORS) != height) {
        return {};
    }
    return pixels;
}

} // namespace

std::optional<CursorImage> cursor_image(HCURSOR cursor) {
    IconBitmaps bitmaps;
    if (cursor == nullptr || !GetIconInfo(cursor, &bitmaps.info) ||
        bitmaps.info.hbmMask == nullptr) {
        return std::nullopt;
    }
    BITMAP mask{};
    if (GetObjectA(bitmaps.info.hbmMask, sizeof mask, &mask) == 0) {
        return std::nullopt;
    }
    const bool mono = bitmaps.info.hbmColor == nullptr;
    const int width = mask.bmWidth;
    const int height = mono ? mask.bmHeight / 2 : mask.bmHeight;
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    const HDC dc = GetDC(nullptr);
    const auto mask_bits = read_mask(dc, bitmaps.info.hbmMask, width, mask.bmHeight);
    const auto color = mono ? std::vector<std::uint32_t>()
                            : read_color(dc, bitmaps.info.hbmColor, width, height);
    ReleaseDC(nullptr, dc);
    if (mask_bits.empty() || (!mono && color.empty())) {
        return std::nullopt;
    }
    CursorImage image;
    image.size = {width, height};
    image.hotspot = {static_cast<int>(bitmaps.info.xHotspot),
                     static_cast<int>(bitmaps.info.yHotspot)};
    image.pixels = core::cursor_pixels(image.size, mono ? nullptr : color.data(), mask_bits.data());
    return image;
}

} // namespace hr::platform
```

Add `cursor_bitmap.cpp` to `hr_core` and `cursor_image.cpp` to `hr_platform`.

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/core_tests.exe "[cursor_bitmap]"; ./build/x86-debug/bin/platform_tests.exe "[cursor_image]"`
Expected: 4 + 4 test cases pass. If the 32-bit case fails on its pixel values, print them (`INFO`) and find out how Windows hands the colour bitmap back before changing any expectation. Then `ctest --preset x86-debug`: all pass.

- [ ] **Step 5: Commit**

```bash
clang-format -i src/core/cursor_bitmap.h src/core/cursor_bitmap.cpp tests/core/cursor_bitmap_test.cpp src/platform/cursor_image.h src/platform/cursor_image.cpp tests/platform/cursor_image_test.cpp
git add src tests
git commit -m "Read cursor bitmaps into straight-alpha pixels" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

---

### Task 7: Cursor sprites in the D3D11 presenter

**Files:**
- Modify: `src/platform/presenter_d3d11.h`, `src/platform/presenter_d3d11.cpp`, `tests/platform/presenter_d3d11_test.cpp`

**Interfaces:**
- Consumes: `D3DPresenter::render`, `read_back`, and `make(HWND)` in the test file (WARP, 64×48 back buffer).
- Produces:
  - `bool D3DPresenter::add_sprite(std::uintptr_t key, const std::uint32_t* bgra, core::Size size)`
  - `bool D3DPresenter::has_sprite(std::uintptr_t key) const`
  - `bool D3DPresenter::draw_sprite(std::uintptr_t key, core::Rect where)`

- [ ] **Step 1: Write the failing test** — append to `tests/platform/presenter_d3d11_test.cpp`:

```cpp
namespace {
int channel(std::uint32_t pixel, int shift) { return static_cast<int>((pixel >> shift) & 0xFF); }
} // namespace

TEST_CASE("cursor sprites are alpha-blended over the frame, scaled and clipped",
          "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const std::vector<std::uint32_t> frame(64 * 48, 0x00102030u);
    // 2x2: opaque red, transparent, half green, opaque blue
    const std::uint32_t sprite[] = {0xFFFF0000u, 0x00000000u, 0x8000FF00u, 0xFF0000FFu};
    CHECK_FALSE(presenter->has_sprite(7));
    REQUIRE(presenter->add_sprite(7, sprite, {2, 2}));
    CHECK(presenter->has_sprite(7));

    presenter->render(frame.data(), {64, 48}, {0, 0, 64, 48}, D3DPresenter::Filter::Point);
    CHECK(presenter->draw_sprite(7, {10, 10, 4, 4}));
    CHECK(presenter->draw_sprite(7, {-2, -2, 4, 4})); // only its bottom-right texel is visible
    CHECK_FALSE(presenter->draw_sprite(8, {0, 0, 4, 4}));
    const auto pixels = presenter->read_back();
    REQUIRE(pixels.size() == 64u * 48u);
    const auto at = [&](int x, int y) {
        return pixels[static_cast<size_t>(y) * 64 + static_cast<size_t>(x)] & 0xFFFFFFu;
    };

    CHECK(at(10, 10) == 0xFF0000u);
    CHECK(at(11, 11) == 0xFF0000u);
    CHECK(at(12, 10) == 0x102030u); // transparent texel
    CHECK(at(13, 13) == 0x0000FFu);
    const std::uint32_t half = at(10, 12); // 0x00FF00 at 128/255 over 0x102030
    CHECK(std::abs(channel(half, 16) - 0x08) <= 2);
    CHECK(std::abs(channel(half, 8) - 0x90) <= 2);
    CHECK(std::abs(channel(half, 0) - 0x18) <= 2);
    CHECK(at(0, 0) == 0x0000FFu);
    CHECK(at(2, 2) == 0x102030u);
    CHECK(at(14, 14) == 0x102030u);
    presenter->present(false);
    DestroyWindow(window);
}

TEST_CASE("the next frame is drawn opaque again after a sprite", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const std::uint32_t sprite[] = {0x80FFFFFFu};
    REQUIRE(presenter->add_sprite(1, sprite, {1, 1}));
    const std::vector<std::uint32_t> frame(64 * 48, 0x00405060u);
    presenter->render(frame.data(), {64, 48}, {0, 0, 64, 48}, D3DPresenter::Filter::Point);
    presenter->draw_sprite(1, {0, 0, 8, 8});
    presenter->render(frame.data(), {64, 48}, {0, 0, 64, 48}, D3DPresenter::Filter::Point);
    const auto pixels = presenter->read_back();
    CHECK((pixels[0] & 0xFFFFFFu) == 0x405060u);
    presenter->present(false);
    DestroyWindow(window);
}
```

Check the expected values:
- **Half-green texel:** R = 0x10·0.498 ≈ 8; G = 255·0.502 + 0x20·0.498 ≈ 144 = 0x90; B = 0x30·0.498 ≈ 24 = 0x18.
- **Sprite at {−2,−2,4,4}:** screen pixels 0–1 are viewport pixels 2–3, which map to texel 1 in both axes (blue).

- [ ] **Step 2: Run the test to verify it fails**

Run: `cmake --build --preset x86-debug`
Expected: compile errors, `add_sprite` / `has_sprite` / `draw_sprite` are not members.

- [ ] **Step 3: Implement**

`src/platform/presenter_d3d11.h`:
- add `#include <map>`;
- add these public members after `present`:

```cpp
    // Cursor sprites (A2 spec §5.3), kept per `key` for the presenter's lifetime. `bgra` is
    // straight-alpha 0xAARRGGBB. False if the texture cannot be made.
    bool add_sprite(std::uintptr_t key, const std::uint32_t* bgra, core::Size size);
    bool has_sprite(std::uintptr_t key) const { return sprites_.count(key) != 0; }
    // Blends the sprite over what render() drew, nearest-neighbour scaled into `where`, which may
    // extend past the back buffer. False for an unknown key or an empty rect.
    bool draw_sprite(std::uintptr_t key, core::Rect where);
```

Add these private members:

```cpp
    // Binds the pipeline and draws the quad into `where` from `view` (constants already set).
    void draw_quad(ID3D11ShaderResourceView* view, core::Rect where, ID3D11BlendState* blend);

    std::map<std::uintptr_t, Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>> sprites_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> alpha_blend_;
```

`src/platform/presenter_d3d11.cpp` — in `create_pipeline`, before the `if (FAILED(hr = device_->CreateBuffer(...` chain, add:

```cpp
    D3D11_BLEND_DESC blend{};
    D3D11_RENDER_TARGET_BLEND_DESC& target = blend.RenderTarget[0];
    target.BlendEnable = TRUE;
    target.SrcBlend = D3D11_BLEND_SRC_ALPHA;
    target.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOp = D3D11_BLEND_OP_ADD;
    target.SrcBlendAlpha = D3D11_BLEND_ONE;
    target.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    target.BlendOpAlpha = D3D11_BLEND_OP_ADD;
    target.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
```

and extend the chain with `|| FAILED(hr = device_->CreateBlendState(&blend, &alpha_blend_))`.

In `render`, replace everything from `// The viewport is the destination rect...` to `context_->Draw(4, 0);` with:

```cpp
    draw_quad(frame_view_.Get(), dest, nullptr);
```

Add:

```cpp
void D3DPresenter::draw_quad(ID3D11ShaderResourceView* view, core::Rect where,
                             ID3D11BlendState* blend) {
    // The viewport is the target rect; parts outside the back buffer are clipped.
    const D3D11_VIEWPORT viewport{static_cast<float>(where.x),
                                  static_cast<float>(where.y),
                                  static_cast<float>(where.width),
                                  static_cast<float>(where.height),
                                  0.0f,
                                  1.0f};
    ID3D11RenderTargetView* const targets[] = {target_.Get()};
    ID3D11Buffer* const buffers[] = {constants_.Get()};
    ID3D11ShaderResourceView* const views[] = {view};
    ID3D11SamplerState* const samplers[] = {point_sampler_.Get(), linear_sampler_.Get()};
    context_->OMSetRenderTargets(1, targets, nullptr);
    context_->OMSetBlendState(blend, nullptr, 0xFFFFFFFFu);
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

bool D3DPresenter::add_sprite(std::uintptr_t key, const std::uint32_t* bgra, core::Size size) {
    if (bgra == nullptr || size.width <= 0 || size.height <= 0) {
        return false;
    }
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>(size.width);
    desc.Height = static_cast<UINT>(size.height);
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data{bgra, static_cast<UINT>(size.width) * 4, 0};
    ComPtr<ID3D11Texture2D> texture;
    ComPtr<ID3D11ShaderResourceView> view;
    if (FAILED(device_->CreateTexture2D(&desc, &data, &texture)) ||
        FAILED(device_->CreateShaderResourceView(texture.Get(), nullptr, &view))) {
        return false;
    }
    sprites_[key] = view;
    return true;
}

bool D3DPresenter::draw_sprite(std::uintptr_t key, core::Rect where) {
    const auto it = sprites_.find(key);
    if (it == sprites_.end() || where.width <= 0 || where.height <= 0) {
        return false;
    }
    const Constants constants{{1.0f, 1.0f}, {1.0f, 1.0f}, 0u, {}}; // mode 0: point sampling
    context_->UpdateSubresource(constants_.Get(), 0, nullptr, &constants, 0, 0);
    draw_quad(it->second.Get(), where, alpha_blend_.Get());
    return true;
}
```

- [ ] **Step 4: Run the tests to verify they pass**

Run: `cmake --build --preset x86-debug; ./build/x86-debug/bin/platform_tests.exe "[presenter_d3d11]"`
Expected: all 10 cases pass, the 8 existing ones included. Then `ctest --preset x86-debug`: all pass.

- [ ] **Step 5: Commit**

```bash
clang-format -i src/platform/presenter_d3d11.h src/platform/presenter_d3d11.cpp tests/platform/presenter_d3d11_test.cpp
git add src/platform tests/platform
git commit -m "platform: alpha-blended cursor sprites in the D3D11 presenter" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

---

### Task 8: Wire the scaled cursor into the presenter

**Files:**
- Modify: `src/wing32/a2.cpp`, `docs/smoke-test.md`

**Interfaces:**
- Consumes: `core::cursor_rect` (Task 5); `platform::cursor_image`, `CursorImage` (Task 6); `D3DPresenter::add_sprite` / `draw_sprite` (Task 7); `platform::cursor_in_client`, `PresenterWindow::refresh_cursor`, `Input::shown_cursor` / `set_shown_cursor` (Task 4); `g_cursor`, `g_cursor_x`, `g_cursor_y`, `content_rect()` (`a2.cpp`).

- [ ] **Step 1: Implement** (glue)

In `src/wing32/a2.cpp`, add `#include "platform/cursor_image.h"` and `#include <map>`. In `presenter_body`, after the `const HCURSOR arrow = ...` line and the cursor-mode log from Task 4, add:

```cpp
        // Cursor sprites by handle (spec §5.3); nullopt = could not be converted, shown natively.
        struct CursorSprite {
            core::Size size;
            core::Point hotspot;
        };
        std::map<HCURSOR, std::optional<CursorSprite>> sprites;
        const auto sprite_for = [&](HCURSOR cursor) -> const std::optional<CursorSprite>& {
            auto it = sprites.find(cursor);
            if (it == sprites.end()) {
                std::optional<CursorSprite> entry;
                const auto image = platform::cursor_image(cursor);
                if (image && d3d->add_sprite(reinterpret_cast<std::uintptr_t>(cursor),
                                             image->pixels.data(), image->size)) {
                    entry = CursorSprite{image->size, image->hotspot};
                }
                char line[128];
                std::snprintf(line, sizeof line, "presenter: cursor 0x%p %s",
                              static_cast<void*>(cursor),
                              entry ? "drawn as a sprite" : "cannot be drawn, shown natively");
                platform::log(line);
                it = sprites.emplace(cursor, entry).first;
            }
            return it->second;
        };
```

In the `frame` lambda, replace the Task 4 block (from `// Native for now; Task 8 adds the scaled sprite.` to its closing `}`) with:

```cpp
            const HCURSOR game_cursor = g_cursor;
            const CursorSprite* sprite = nullptr;
            if (config.cursor == core::CursorMode::Scaled && game_cursor != nullptr) {
                if (const auto& entry = sprite_for(game_cursor)) {
                    sprite = &*entry;
                }
            }
            // While a sprite is drawn the system cursor is hidden over the client area.
            const HCURSOR shown =
                sprite != nullptr ? nullptr : (game_cursor != nullptr ? game_cursor : arrow);
            if (shown != input.shown_cursor()) {
                input.set_shown_cursor(shown);
                window.refresh_cursor();
            }
```

Replace `d3d->render(capture.pixels(), capture.size(), dest, filter);` with:

```cpp
            d3d->render(capture.pixels(), capture.size(), dest, filter);
            if (sprite != nullptr && platform::cursor_in_client(window.hwnd())) {
                d3d->draw_sprite(reinterpret_cast<std::uintptr_t>(game_cursor),
                                 core::cursor_rect({g_cursor_x.load(), g_cursor_y.load()},
                                                   {content.width, content.height}, dest,
                                                   sprite->size, sprite->hotspot));
            }
```

- [ ] **Step 2: Extend the smoke test** — append to `docs/smoke-test.md`:

```markdown
### A2 cursor (from A2.3)
- [ ] `cursor = scaled`: the game's own cursor is drawn enlarged with the picture (2× at 1280×960),
      and clicks land on its hotspot.
- [ ] `cursor = native`: the game's cursor is shown at normal size by Windows.
- [ ] Books menu: the game's cursor appears (not the OS arrow), and hovering a special item shows
      its subtitle.
- [ ] The loading cursor appears while the game loads and goes away afterwards.
- [ ] Battle: the cursor changes shape over units and terrain as in vanilla.
- [ ] Over the black bars the cursor stays at the picture's edge; over the title bar, other
      windows and the desktop the normal Windows cursor is shown.
```

- [ ] **Step 3: Build and run all tests**

Run: `cmake --build --preset x86-debug; ctest --preset x86-debug; cmake --build --preset x86-release; ctest --preset x86-release`
Expected: no warnings; all tests pass.

- [ ] **Step 4: Quick manual check (ask the user first)**

Ask for permission to install the release build. The user starts the game with the default `cursor = scaled` (not set in the ini) and moves over the menus. Expected: an enlarged game cursor, and clicks hitting where its tip points. Read `horned-rat.log` for `presenter: cursor scaled`, the `presenter: cursor 0x... drawn as a sprite` lines and no `exception`.

- [ ] **Step 5: Commit**

```bash
clang-format -i src/wing32/a2.cpp
git add src/wing32/a2.cpp docs/smoke-test.md
git commit -m "wing32: scaled cursor sprite drawn with the picture" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
```

---

### Task 9: A2.3 exit — smoke test, notes, spec, PR

**Files:**
- Modify: `docs/re-notes/a2-3-exit.md`, `docs/superpowers/specs/2026-10-05-stage-a2-presenter-design.md`

This task needs the user. Ask before installing into the game folder or changing `horned-rat.ini`.

- [ ] **Step 1: Install the release build (ask the user first)**

Run: `. ./tools/vsenv.ps1; cmake --build --preset x86-release; ./tools/install.ps1 -GameDir 'D:\Program Files (x86)\GOG Galaxy\Games\Warhammer - Shadow of the Horned Rat' -SourceDll build/x86-release/bin/wing32.dll`
Expected: `Installed horned-rat into '...\WARFB'.`

- [ ] **Step 2: The user runs the smoke test**

Two runs, with permission to edit the ini between them:
1. `cursor = scaled` (the default).
2. `cursor = native`.

Both runs go through the "A2 cursor" items. The carried items are fitted in where convenient: save-name typing, videos, off-screen drag, click-through, `aspect = stretch`, maximized → borderless → back. Record pass / fail / not tested per item.

- [ ] **Step 3: Restore the ini (ask first)**

Remove `message_census = true` and any `cursor =` line added for the runs, so the user's ini is as it was before A2.3.

- [ ] **Step 4: Complete `docs/re-notes/a2-3-exit.md`** — add after §1:
- §2 smoke-test table;
- §3 performance (the 10 s presenter lines with a sprite drawn, compared with A2.2's ~600 frames / 10 s);
- §4 bugs found and fixed;
- §5 open items (anything carried to A2.4).

- [ ] **Step 5: Update the spec**
- §5.3: add the `WM_SETCURSOR` forwarding and the cursor choice (decisions 2–3).
- Add "§5.7 Decisions made in A2.3" with decisions 1 and 4–7.
- §6: add `[debug] message_census = false ; diagnostics`.
- §10: add the A2.3 results, including what the investigation found.

- [ ] **Step 6: Commit, push, mark the PR ready**

Check the PR is still open first (`gh pr view --json state`). If it was merged, put the docs commit on a new branch from `origin/main` and open a follow-up PR.

```bash
git add docs
git commit -m "docs: A2.3 exit results" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
git push
gh pr ready
```

**Milestone A2.3 is complete.** Next: plan A2.4 (background and polish) with superpowers:writing-plans.
