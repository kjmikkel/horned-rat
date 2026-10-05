# Stage A2: In-process Presenter — Design

- **Date:** 2026-10-05
- **Status:** Approved in brainstorming, awaiting spec review
- **Supersedes:** §5.1, §5.2 and §5.4 of `2026-10-05-stage-a-display-wrapper-design.md` (the Stage A
  spec). Everything else in the Stage A spec (layering, config/error-handling principles,
  installer, CI, supported build) still applies.
- **Inputs:** `docs/re-notes/a1-findings.md` and the A2 capture spike (§2 below).

## 1. Goal

Show the game's 640×480 picture in our own window at any size or fullscreen, **pixel-perfect**
(integer / sharp-bilinear / nearest scaling), with fully working mouse and keyboard input,
optionally scaled cursor, and the option to keep the game running when it is not focused.

Non-goals: rendering more of the battlefield (Stage B), restoring palette effects (a separate
item if A2.1's fade check shows they are broken in vanilla), audio, other platforms.

## 2. Facts this design rests on

From A1 (`a1-findings.md`):
- The screen is a tree of child windows under the main `Glue` window; drawing goes directly to
  their window DCs (WinG blits, `StretchDIBits`, `TextOutA`, `BitBlt` from the video player) and
  USER32 paints the standard `Button`/`Edit` controls itself.
- Mouse handling polls `GetCursorPos` + `ScreenToClient`; `WHSHR.EXE` and `GAMEF.DLL` also import
  `GetAsyncKeyState`, `SetCapture`/`ReleaseCapture`/`GetCapture`.

From the A2 spike (throwaway tool, 2026-10-05, display 1920×1080 at 100 %):
- `BitBlt` from the `Glue` window DC captures the **complete composed picture** (Reality Lab 3D
  view, minimap, direct text, controls) in **~1.2–1.3 ms** per frame at 650×491.
- It keeps working with the window **moved off-screen** (x = −3000): live battle frames.
- `PrintWindow(PW_RENDERFULLCONTENT)` costs 22–31 ms and sometimes returns black frames:
  rejected.
- The `Glue` client is 650×491; the 640×480 picture is the `BitmapScrn` child at offset (5,5).
- The display mode stays at the desktop resolution while the game is focused.
- Click messages posted from another process are ignored, even with the real cursor moved to
  the right place: the game decides by polling cursor position and button state.
- The game shrinks/changes when it loses focus (observed by the user).
- Open: whether palette fades are visible on a modern 32-bit desktop (data lost in the spike;
  measured in A2.1).

## 3. Approach

**A — separate in-process presenter, game window parked off-screen** (chosen), with
**C — game window kept in place but invisible under the presenter** as a config fallback.
Rejected: B (drawing onto the game's own window — its child tree paints over us), Windows DPI
scaling (always bilinear, not pixel-perfect), per-blit capture (A1: misses USER32-painted
controls and direct text).

## 4. Architecture

Layering unchanged: `wing32 → platform → core`; `core` has no Windows headers.

```
core/
  viewport.{h,cpp}     scale modes → destination rect; presenter-client ↔ game-content mapping;
                       letterbox clamping
  input_map.{h,cpp}    button-mask decision; which presenter messages are forwarded;
                       child-relative coordinates
  content_rect.{h,cpp} choose the capture rect (BitmapScrn child rect, else full client)
  cursor_math.{h,cpp}  scaled cursor size and hotspot position
  config (extended)    background, cursor, park keys (§6)
platform/
  window_capture       BitBlt a window's client region into a 32-bit BGRA buffer
  presenter_window     our top-level window: windowed/borderless, resize, Alt+Enter
  presenter_d3d11      device (hardware, then WARP), swapchain, frame texture upload,
                       scaling shader (point / sharp-bilinear), cursor sprite pass
  child_input          deepest child window under a point; post mapped mouse messages
  window_subclass      replace a window's WNDPROC; restore on demand
  parking              park (off-screen or underneath) and unpark a window
wing32/
  a2_hooks             GetCursorPos (translates), GetAsyncKeyState (masks mouse buttons),
                       SetCursor (records shape), SetWindowPos on Glue (keeps parking position)
  frame_pump           presenter thread: create window + device, capture → upload → present
```

`ShadowFrame` and `PaletteTracker` from the Stage A spec are dropped: the captured picture is
already final 32-bit colour.

## 5. Data flow

### 5.1 Frames

On the presenter thread, once per vsync:
1. `window_capture`: `BitBlt` from the `Glue` window DC, region = content rect (the `BitmapScrn`
   child's rect in `Glue` client coordinates; fallback the whole client) → 640×480 BGRA buffer.
2. Upload to a dynamic texture; draw a full-screen quad with the scaling shader into the
   destination rect from `core/viewport` (bars elsewhere are black).
3. Draw the cursor sprite (§5.3) when `cursor = scaled`.
4. `Present` (sync interval 1 when `vsync = true`, else 0).

Capturing while the game draws may occasionally produce a torn frame; accepted (no
synchronisation with the game's drawing).

### 5.2 Input

"The game thread" below is the thread that owns `Glue` (`GetWindowThreadProcessId`).

- **Virtual cursor.** Presenter `WM_MOUSEMOVE` at presenter-client (px,py) → `viewport` maps to a
  game point g in 0..639 × 0..479 (clamped; points in the bars clamp to the nearest edge) → stored
  atomically.
- **`GetCursorPos` hook** returns `glue_client_origin_on_screen + content_offset + g`. The game
  window really is at its parked position, so the game's own (unhooked) `ScreenToClient` yields
  correct child coordinates. `ScreenToClient`/`ClientToScreen` are not translated.
- **`GetAsyncKeyState` hook**: for `VK_LBUTTON`, `VK_RBUTTON`, `VK_MBUTTON`, `VK_XBUTTON1/2`
  returns the physical state only if the presenter is the foreground window **and** the real
  cursor is inside the presenter's client area; otherwise reports "up". Other keys pass through.
- **Forwarded messages** (for USER32 controls): presenter `WM_[LRMX]BUTTON{DOWN,UP,DBLCLK}`,
  `WM_MOUSEMOVE` and `WM_MOUSEWHEEL` → `child_input` finds the deepest visible, enabled game child
  under g (descending from `Glue`) → `PostMessage` with child-relative coordinates (wheel keeps
  screen coordinates per Win32 rules, using the virtual cursor's screen position).
- **Keyboard**: presenter `WM_KEYDOWN/UP`, `WM_CHAR`, `WM_SYSKEYDOWN/UP` (except our own
  `Alt+Enter`) → posted to the game thread's focus window (`GetGUIThreadInfo`), else to `Glue`.

### 5.3 Cursor

- `SetCursor` hook records the handle the game sets, then calls the original.
- `cursor = scaled`: over its client area the presenter hides the system cursor and draws the
  recorded cursor (bitmap from `GetIconInfo`, converted to a texture, cached per handle) at g,
  scaled by the same integer factor as the picture, hotspot-corrected.
- `cursor = native`: on `WM_SETCURSOR` in the client area the presenter sets the recorded handle.

### 5.4 Window state

- `Glue` is subclassed. With `background = run`, `WM_ACTIVATE(WA_INACTIVE)` and
  `WM_ACTIVATEAPP(FALSE)` are passed on as active; with `background = pause` they pass unchanged.
- `SetWindowPos` hook on `Glue`: size changes pass through, position is forced to the parking
  position (A) or to the presenter's position (C).
- Closing the presenter (✕, Alt+F4) posts `WM_CLOSE` to `Glue`. `WM_DESTROY` of `Glue` (seen by
  the subclass) shuts the presenter thread down.

## 6. Configuration

New `horned-rat.ini` keys (existing keys from Stage A §6.1 unchanged; invalid values warn and use
the default):

```ini
[general]
background = run        ; run | pause
[display]
cursor     = scaled     ; scaled | native
park       = offscreen  ; offscreen | underneath
```

- `park = offscreen`: `Glue` at (−32000, −32000).
- `park = underneath`: `Glue` at the presenter's client position, made invisible with
  `WS_EX_LAYERED` and alpha 0; the presenter is above it in z-order.

## 7. Start-up and error handling ("never worse than vanilla")

Start-up:
1. `DllMain`: as in A1 (config, log, hooks).
2. `CreateWindowExA` hook: on the first top-level window (`Glue`), start the presenter thread.
3. Presenter thread: create window and D3D11 device (hardware, else WARP), capture one frame
   successfully, **then** park and subclass `Glue`.

| Failure | Response |
|---|---|
| No D3D11 device / window creation fails | Never park; game runs as in A1; reason logged |
| Capture fails (`BitBlt` false) | Keep the last good frame; log once; after 2 s of failures unpark `Glue` to (0,0) and close the presenter |
| Exception on the presenter thread | Top-level catch: unpark `Glue`, log, close the presenter |
| Exception in a hook | Guard logs and calls the original (unchanged) |
| `enabled = false` / `HORNED_RAT_DISABLE=1` | Pure passthrough (unchanged) |

**Unpark** = restore the original window procedure, remove the layered style if added, move `Glue`
to (0,0) and show it. Every failure path ends with a playable game.

Logging adds: presenter lifecycle, device type, scale mode and destination rect, park/unpark with
reason, and every 10 s average capture ms, present ms and frame count.

## 8. Testing

| Layer | Covers | Runs |
|---|---|---|
| `core` | viewport rects for every scale × aspect at many sizes; presenter ↔ game mapping round-trip and clamping; button-mask truth table; forwarded-message selection; child-relative coordinates; content-rect choice; cursor scale/hotspot; new config keys | CI: Windows + Linux (gcc/clang, sanitizers) |
| `platform` | `window_capture` on a test window painted with a known pattern, on-screen and parked off-screen (skipped when `DwmIsCompositionEnabled` is false); `presenter_d3d11` on WARP with exact pixel checks for integer/nearest; `child_input` posts reach the deepest child with correct coordinates; `window_subclass` reports an inactive `WM_ACTIVATE` as active and restores; `parking` park/unpark round-trip | CI: Windows x86 |
| `wing32` glue | thin; calls `core` + stored state | manual smoke test |
| Smoke test | additions: resize, every scale mode, Alt+Enter, both cursor modes, alt-tab and second monitor with `background = run`, menu clicks, typing a save-game name, battle mouse control, videos, `park = underneath`, panic switch | each milestone exit |

Red/green TDD for every behaviour (project rule).

## 9. Milestones

Each leaves the game playable; `enabled = false` always gives vanilla behaviour.

- **A2.1 — Presenter at 1:1.** Presenter thread, capture, D3D11 at exactly 640×480, parking,
  subclass, full input path, unpark on failure. **Exit:** smoke test plays exactly like vanilla;
  park A and C compared on the user's machine; fade check (frame-by-frame brightness of a screen
  transition recorded in vanilla and with A2 — if vanilla shows no fades on modern Windows,
  "restore palette effects" becomes a separate later item).
- **A2.2 — Scaling (deliverable).** Resizable window, `integer`/`sharp`/`nearest-fit`, aspect,
  borderless, Alt+Enter.
- **A2.3 — Cursor.** `scaled`/`native`.
- **A2.4 — Background and polish.** `background = run/pause`, performance log line, smoke-test
  fixes.

## 10. Risks

| Risk | Mitigation |
|---|---|
| The game reads window position/size and reacts badly to being parked | A2.1 smoke test; fallback C keeps the window in place |
| Standard controls ignore posted messages without real focus/capture (e.g. `Edit` caret) | A2.1 smoke test includes typing a save name; if needed, also post `WM_SETFOCUS`/activate the control |
| Game uses `SetCapture` and expects mouse messages to its captured window | `child_input` posts to the captured window (`GetCapture` on the game thread) when one is set |
| Off-screen windows not composited on some systems (no capture) | Detected at start-up (first capture must succeed before parking); `park = underneath` fallback |
| Performance on weak GPUs | WARP fallback; capture is ~1.3 ms; 10 s performance log line |
| Torn frames | Accepted (§5.1) |
