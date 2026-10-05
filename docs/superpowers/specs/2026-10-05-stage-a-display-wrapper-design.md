# Stage A: Display Wrapper — Design

- **Date:** 2026-10-05
- **Status:** Approved in brainstorming, awaiting spec review
- **Scope:** Stage A of the horned-rat project (clean scaling of the existing 640×480 output).
  Stage B (higher internal resolution / more visible battlefield) gets its own spec later.

## 1. Context and goals

The 1995 Windows release of *Shadow of the Horned Rat* (GOG build, `WHSHR.EXE` linked
1995-12-11) runs on modern Windows, but only in a tiny fixed 640×480 window, with unreliable
colours caused by its reliance on the Windows 256-colour palette manager.

Project priorities (from the user): (1) run well on modern Windows — far ahead of —
(3) gameplay improvements (AI, pathfinding, bug fixes, mods, UI), (2) other platforms,
(4) documentation of internals.

**Stage A goal:** show the game's unchanged 640×480 image at any window size or fullscreen,
with correct aspect, crisp scaling, correct palette colours, and correctly mapped mouse input.
Stage A also establishes the project foundation that later stages build on:
the build system, TDD/CI pipeline, the in-process hook host, and logging.

### Non-goals for Stage A

- Rendering more of the battlefield (Stage B).
- Any gameplay, AI, UI-layout, save-format or mod changes.
- Non-Windows platforms (beyond `core` compiling on Linux).
- Supporting any game build other than the one listed in §8.
- Audio/MIDI changes.

### Hard constraints

- **No game assets, original binaries, or decompiled original code are ever committed**
  or sent to CI. `.gitignore` blocks the game's file types. RE notes in `docs/` are written
  in our own words.
- **Red/green TDD** for all behaviour: the failing test is written and seen to fail before
  the implementation is written.
- **CI everywhere it is possible**; anything that needs the real game is local-only and opt-in.
- Language: **C++20**, build: **CMake** (+ presets), target: **32-bit x86 Windows (MSVC)**.
  License: MIT.

## 2. Findings that drive the design

Taken from the import tables and strings of the GOG binaries:

- All presentation goes through GDI: `WHSHR.EXE` and `GAMEF.DLL` import `WinGCreateDC`,
  `WinGCreateBitmap`, `WinGGetDIBPointer`, `WinGSetDIBColorTable`, `WinGBitBlt`,
  `WinGStretchBlt` (GAMEF), `WinGRecommendDIBFormat`, `WinGGetDIBColorTable` (WHSHR), and
  `GDI32!StretchDIBits`. No DirectDraw, no hardware 3D.
- 3D (Reality Lab, `RLDDF.DLL` + `RL\SOFTF.DLL`) is a software rasterizer that ends up in the
  same 8-bit DIBs.
- Colours come from the palette manager: `CreatePalette`, `SelectPalette`, `RealizePalette`,
  `SetPaletteEntries`, `AnimatePalette`, `SetSystemPaletteUse`.
- The game imports `TextOutA` and `PatBlt`; whether they ever target the window DC directly
  (bypassing the WinG DIB) is **unknown** and is resolved by milestone A1.
- `WHSHR.EXE`, `GAMEF.DLL`, `MINDSYS.DLL` all call `OutputDebugStringA` with ~266
  `[Function] message` style strings.
- `wing32.dll` is shipped locally in `WARFB\` (not a system DLL) and exports exactly 10
  functions: `WinGBitBlt`, `WinGCreateBitmap`, `WinGCreateDC`, `WinGCreateHalftoneBrush`,
  `WinGCreateHalftonePalette`, `WinGGetDIBColorTable`, `WinGGetDIBPointer`,
  `WinGRecommendDIBFormat`, `WinGSetDIBColorTable`, `WinGStretchBlt`.

## 3. Approach

A **proxy `wing32.dll`** placed in `WARFB\`. The original is renamed `wing32_orig.dll`.
Our DLL:

1. exports the same 10 functions; those we don't intercept forward to the original;
2. on load, patches the **import address tables** of `WHSHR.EXE` and `GAMEF.DLL` to intercept
   the GDI/USER32 calls listed in §5;
3. presents frames via a **Direct3D 11** presenter it owns.

Rejected alternatives: a launcher + DLL injection (no benefit for goal 1, breaks launching
from GOG Galaxy, more code), and generic wrappers such as DxWnd (no foundation for later
stages, no TDD story).

## 4. Architecture and repository layout

```
horned-rat/
├─ CMakeLists.txt, CMakePresets.json   # presets: x86-debug, x86-release, ci, linux-core
├─ src/
│  ├─ core/        # pure C++20, NO Windows headers; compiles on Linux
│  │   palette.{h,cpp}        index + palette → RGBA; PaletteTracker
│  │   shadow_frame.{h,cpp}   640×480 index buffer, clipped rect copies, dirty tracking
│  │   viewport.{h,cpp}       scale modes, letterbox rect, window↔game coordinate mapping
│  │   config.{h,cpp}         horned-rat.ini parsing/defaults/serialisation
│  ├─ platform/    # thin Win32 layer
│  │   iat_hook.{h,cpp}       patch/unpatch one import of one module
│  │   wing_forward.{h,cpp}   load wing32_orig.dll, resolve original functions
│  │   presenter_d3d11.{h,cpp} swapchain, index+palette textures, scaling shader, present thread
│  │   window.{h,cpp}         window style, subclassed wndproc, fullscreen toggle
│  │   log.{h,cpp}            horned-rat.log writer (thread-safe, timestamped)
│  └─ wing32/      # the proxy DLL: exports.def, DllMain, hook entry points only
├─ tests/
│  ├─ core/        # Catch2 unit tests
│  ├─ platform/    # Windows-only tests + fixture exe/dll + fake wing32_orig.dll
│  └─ gamedata/    # opt-in, only when SOTHR_GAME_DIR is set
├─ tools/install.ps1
├─ docs/           # specs, smoke-test checklist, RE notes (own words only)
└─ .github/workflows/ci.yml
```

**Dependency rule:** `wing32 → platform → core`, never the reverse. `core` must not include
`<windows.h>`; this is enforced by the Linux CI job compiling `core`.

`wing32/` contains only glue: every exported function / hook body is a few lines that call
into `platform`/`core` inside the exception guard (§6).

## 5. Data flow

### 5.1 Frames out

```
game writes pixels ──► WinG DIB (640×480, 8-bit)
WinGBitBlt / WinGStretchBlt / StretchDIBits targeting the game window DC
   └─► (our hook; original blit NOT forwarded)
       ShadowFrame.copy_rect(src_bits, src_rect, dst_point)   — clipped, marks dirty
       PaletteTracker                                         — current 256 colours
   └─► signal present thread
Present thread: snapshot ShadowFrame + palette under a mutex
   → upload R8_UINT 640×480 index texture + 256×1 RGBA palette texture
   → pixel shader: palette lookup + scaling
   → swapchain Present (vsync per config)
```

- Blits whose destination is **not** the game window DC (e.g. DIB-to-DIB) are forwarded
  to the original unchanged.
- `WinGStretchBlt` / `StretchDIBits` with a non-1:1 size are resampled (nearest) into the
  ShadowFrame at their destination rect.
- At most one present per vsync interval, so many small dirty-rect blits per game frame do
  not cause many vsync stalls.

### 5.2 Palette

`PaletteTracker` models the **logical palette realised for the game window**, because on a
real 8-bit display `AnimatePalette` changed on-screen colours without touching pixels or the
DIB colour table. Hooks: `CreatePalette` (record initial entries per `HPALETTE`),
`SetPaletteEntries`, `AnimatePalette`, `SelectPalette` (which palette is current for the
window DC), `RealizePalette`, and `WinGSetDIBColorTable` (fallback when no logical palette
has been selected). The presenter always uses the tracker's current 256 entries.

### 5.3 Window

The **main window** is the first top-level window (no parent, not `WS_CHILD`) created through
the hooked `CreateWindowExA`. A1 logs every `CreateWindowExA` call to confirm this rule holds;
if it does not, the rule is revised before A2.

- Hook `CreateWindowExA`: for the game's main window, apply a resizable overlapped style
  (`mode = windowed`, initial client size `window_size`) or a borderless monitor-sized popup
  (`mode = borderless`).
- Hook `SetWindowPos`: ignore the game's attempts to resize/move the main window back to 640×480.
- `Alt+Enter` toggles windowed ↔ borderless.
- The D3D11 swapchain owns the client area; the game's GDI blits to the window never reach it.

### 5.4 Mouse in

- Subclass the main window procedure; for all `WM_*MOUSE*` / `WM_*BUTTON*` messages, map
  `lParam` from client coordinates to game coordinates via `core/viewport`. Points in the
  letterbox bars clamp to the nearest edge of the game image.
- `GetCursorPos` returns *client-origin-in-screen + game coordinates*, so the game's own
  `ScreenToClient` yields game coordinates.
- `SetCursorPos` and `ClipCursor` apply the inverse mapping.

### 5.5 Scaling modes (`core/viewport`)

- `integer`: largest integer multiple of 640×480 that fits; centred; bars elsewhere.
- `sharp`: integer prescale (nearest) then bilinear to the largest aspect-correct fit
  ("sharp-bilinear").
- `nearest-fit`: nearest-neighbour to the largest aspect-correct fit.
- `aspect = stretch` fills the whole client area ignoring 4:3 (for any mode).

## 6. Configuration, error handling, logging, install

### 6.1 `WARFB\horned-rat.ini`

```ini
[general]
enabled      = true        ; false = pure passthrough to the original WinG
[display]
mode         = windowed    ; windowed | borderless
window_size  = 1280x960
scale        = sharp       ; integer | sharp | nearest-fit
aspect       = 4:3         ; 4:3 | stretch
vsync        = true
[debug]
log          = true        ; horned-rat.log
game_debug   = true        ; capture the game's OutputDebugStringA output
```

Missing file → written with defaults. Unknown key or invalid value → warning in the log,
default used. Config problems never prevent the game from starting.

### 6.2 Error handling — "never worse than vanilla"

- Every exported function and hook body runs inside a guard: `try { … } catch (...)`;
  on exception, log it and call the original function. No C++ exception may unwind into
  game code.
- D3D11 initialisation failure → the whole DLL switches to **passthrough mode** (all
  original functions, no hooks besides logging), with the reason logged.
- `wing32_orig.dll` missing → one `MessageBox` naming the problem and the fix
  (`tools/install.ps1 -Repair`), then the process exits — the game cannot run without it.
- Panic switch: `enabled = false` or environment variable `HORNED_RAT_DISABLE=1` →
  passthrough mode.

### 6.3 Logging

`WARFB\horned-rat.log`, truncated on each start, timestamped, thread-safe. Contains our
own messages and, when `game_debug = true`, every `OutputDebugStringA` string from the game
(the hook also forwards to the original so external debuggers still see them).

### 6.4 `tools/install.ps1`

- `-GameDir <path>` (the folder containing `WARFB\`).
- Verifies SHA-256 of `WHSHR.EXE` and `wing32.dll` against §8; refuses on mismatch unless
  `-Force`.
- Renames `wing32.dll` → `wing32_orig.dll` only if `wing32_orig.dll` does not yet exist
  (re-install is safe), copies our `wing32.dll`, writes a default `.ini` if absent.
- `-Uninstall`: removes our DLL, renames `wing32_orig.dll` back, leaves `.ini`/`.log`.
- `-Repair`: restores `wing32_orig.dll` from the current `wing32.dll` if it is the original
  (hash match), otherwise instructs the user to run GOG Galaxy "Verify / Repair".

## 7. Testing and CI

### 7.1 Test layers (Catch2 v3 via `FetchContent`)

| Layer | Covers | Runs on |
|---|---|---|
| `tests/core` | palette conversion, `PaletteTracker` (create/set/animate/select semantics), `ShadowFrame` rect copy + clipping + stretch, viewport rects for every scale/aspect mode, client↔game mapping incl. letterbox clamping, config parse/defaults/invalid values | CI: Windows MSVC x86; Linux GCC + Clang with ASan/UBSan |
| `tests/platform` | IAT hook redirect + restore against a repo-built fixture exe/dll; proxy forwarding for all 10 exports against a fake `wing32_orig.dll` that records calls; D3D11 presenter rendering a known index pattern via **WARP** offscreen, exact pixel comparison; window subclass mapping via synthetic messages to a hidden window; guard behaviour (exception → original called) | CI: Windows x86 |
| `tests/gamedata` | real files match §8 hashes; our export names match the real `wing32.dll` export table | Local only, when `SOTHR_GAME_DIR` is set; skipped otherwise |
| `docs/smoke-test.md` | manual: main menu, campaign map, a battle, a palette fade, video, save/load, resize, `Alt+Enter`, uninstall | Before each release |

### 7.2 TDD workflow

For each behaviour: write the test → run it and confirm it fails for the expected reason →
minimal implementation → green → refactor while green. Bug fixes start with a test that
reproduces the bug.

### 7.3 CI (`.github/workflows/ci.yml`, on push and pull request)

1. `windows-x86`: configure (`ci` preset), build, `ctest --output-on-failure`; upload
   `wing32.dll` + `tools/install.ps1` as a build artifact.
2. `linux-core`: build and test `core` only, with sanitizers.
3. `format`: `clang-format --dry-run --Werror` over `src/` and `tests/`.

Dependency downloads (`FetchContent`) are cached. The repo is private, so Actions minutes
are limited (Windows minutes count double); target run time is under ~6 minutes.

## 8. Supported game build

GOG release, `WARFB\`:

| File | SHA-256 |
|---|---|
| `WHSHR.EXE` | `df462ba4dc29d0832c44f14207b47459f281e6dd4a9e3f3ad972fd43a5921326` |
| `wing32.dll` | `dfc3a949141dad973ad2eb4b66e3c8dc8dc7851fe8f360cd42697aab439d70c0` |
| `GAMEF.DLL` | `e8afe836e9c99b54a76643b2adb4edb52f56a991d53ab8af60f746129b1132d6` |

## 9. Milestones

Each milestone leaves the game playable.

- **A0 — Skeleton:** CMake + presets, Catch2, CI green (all three jobs); proxy `wing32.dll`
  that forwards all 10 exports (game behaves exactly as vanilla); `install.ps1`
  (install/uninstall/repair).
- **A1 — Instrumentation:** config, logging, `OutputDebugStringA` capture, logging-only
  hooks on the blit functions, `TextOutA`, `PatBlt`. **Exit criterion:** documented answer
  to whether anything draws directly to the window DC (§2); if yes, add redirecting those
  calls into the ShadowFrame to A2's scope.
- **A2 — Capture at 1:1:** `ShadowFrame`, `PaletteTracker`, D3D11 presenter presenting at
  exactly 640×480. **Exit criterion:** smoke test looks identical to vanilla, including
  palette fades.
- **A3 — Scaling (the deliverable):** resizable window, scale and aspect modes, mouse mapping.
- **A4 — Polish:** borderless mode, `Alt+Enter`, fixes from the smoke test.

## 10. Risks

| Risk | Mitigation |
|---|---|
| Some drawing bypasses the WinG DIB (window-DC `TextOutA`/`PatBlt`, etc.) | A1 instrumentation answers it before A2 relies on it |
| Game reads back from the window DC (e.g. `BitBlt` from screen) | A1 logging covers DC sources too; emulate from ShadowFrame if seen |
| Cursor drawn by Windows (`SetCursor`) stays small when scaled | Acceptable for Stage A; scaled cursor considered in A4 |
| Mouse code uses an API we didn't map (e.g. `GetMessagePos`, `WM_NCHITTEST`) | A1 logs USER32 cursor/mouse API usage; add mappings as found, each with a test |
| Timing changes from vsync/present thread | Present thread is decoupled from game thread; `vsync = false` escape hatch |
| Other builds of the game (CD release) | Hash check in installer; out of scope |
