# Stage A2: In-process Presenter — Design

- **Date:** 2026-10-05
- **Status:** Approved; A2.1–A2.4 complete (2026-10-07). **Revised 2026-10-05 during A2.1 Task 5:** option A (off-screen parking) does
  not work; the game window is instead kept on-screen, invisible and click-through (§2, §3).
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
- ~~It keeps working with the window moved off-screen.~~ **Corrected (A2.1 Task 5):** a controlled
  experiment shows GDI drawing is clipped to the on-screen part of a window, so an off-screen window
  keeps a stale picture. The spike had not checked the window position; the game most likely moved
  its window back on-screen itself. Drawing *does* reach the capture when the window is on-screen
  but invisible (layered, alpha 0) or fully covered by another window.
- `PrintWindow(PW_RENDERFULLCONTENT)` costs 22–31 ms and sometimes returns black frames:
  rejected.
- The `Glue` client is 650×491; the 640×480 picture is the `BitmapScrn` child at offset (5,5).
- ~~The display mode stays at the desktop resolution while the game is focused.~~ **Corrected
  (A2.2 exit):** `WHSHR.EXE` switches the primary monitor to 640×480 (`ChangeDisplaySettingsA`,
  looked up with `GetProcAddress`, so the IAT hooks missed it). The presenter defers the switch.
- Click messages posted from another process are ignored, even with the real cursor moved to
  the right place: the game decides by polling cursor position and button state.
- The game shrinks/changes when it loses focus (observed by the user).
- ~~Open: whether palette fades are visible on a modern 32-bit desktop.~~ **Measured (A2.1
  exit):** they are, and the presenter reproduces them step for step (`a2-1-exit.md` §4).

From the A2.1 exit (`a2-1-exit.md` §3):
- `ChildWindowFromPointEx` finds nothing under a layered alpha-0 parent; child hit-testing must be
  geometric.
- The game's DirectSound buffers lack `DSBCAPS_GLOBALFOCUS` and its cooperative window is `Glue`,
  so effects and speech are muted while `Glue` is not foreground (always, once hidden).
- The game's dialogs are `MessageBoxA` boxes owned by `Glue`; each runs a modal loop on the game
  thread.

From the A2.2 exit (`a2-2-exit.md` §3):
- The game posts `WM_QUIT` only on its own exit path; closed otherwise, its `GetMessage` loop
  waits forever. The `Glue` subclass posts `WM_QUIT` after `WM_DESTROY`.

## 3. Approach

**Separate in-process presenter; the game window stays on-screen but hidden** — layered with
alpha 0, click-through (`WS_EX_TRANSPARENT`), at the bottom of the z-order, kept fully inside the
work area of its monitor (it does not follow the presenter, so dragging the presenter partly
off-screen cannot clip the game's drawing). This is the former fallback C, refined. The former
option A (off-screen parking) was dropped after the §2 correction.
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
  config (extended)    background, cursor keys (§6)
  viewport (extended)  keep_inside: position that keeps a window inside an area
platform/
  window_capture       BitBlt a window's client region into a 32-bit BGRA buffer
  presenter_window     our top-level window: windowed/borderless, resize, Alt+Enter
  presenter_d3d11      device (hardware, then WARP), swapchain, frame texture upload,
                       scaling shader (point / sharp-bilinear), cursor sprite pass
  child_input          deepest child window under a point; post mapped mouse messages
  window_subclass      replace a window's WNDPROC; restore on demand
  hidden_window        hide a window (invisible, click-through, bottom, inside an area) / unhide
wing32/
  a2_hooks             GetCursorPos (translates), GetAsyncKeyState (masks mouse buttons),
                       SetCursor (records shape); position lock via the Glue subclass (§5.4)
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
  window really is at its hidden position, so the game's own (unhooked) `ScreenToClient` yields
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
- **`WM_SETCURSOR` forwarding (A2.3).** The game sets most cursors as window-class cursors, which
  Windows applies in `DefWindowProc(WM_SETCURSOR)`. It also drives hover tooltips and similar
  effects from that message. Windows sends it only to the window under the real cursor, which is
  never a game window.
  - Before each forwarded mouse message except the wheel, the presenter sends
    `WM_SETCURSOR(child, MAKELPARAM(HTCLIENT, msg))` to the deepest game child under g with
    `SendMessageCallbackA`. The send is asynchronous, so the presenter never blocks on the game
    thread.
  - Nothing is sent while a game window has mouse capture.
  - When the game has handled it, the presenter records the cursor the game wants: the one it
    set with `SetCursor` meanwhile (a call counter in the hook changed), else the hit window's
    class cursor (`GetClassLongPtrA(GCLP_HCURSOR)`, invisible to the IAT hook because
    `DefWindowProc` sets it internally), else unchanged (`core::cursor_after_set_cursor`).

### 5.4 Window state

- `Glue` is subclassed. With `background = run`, `WM_ACTIVATE(WA_INACTIVE)` and
  `WM_ACTIVATEAPP(FALSE)` are passed on as active.
- **`background = pause` (A2.4).** What counts is whether the *application* is active (the
  presenter's `WM_ACTIVATEAPP`; a switch to another thread of our own process does not count):
  - The presenter posts `WM_ACTIVATE` and `WM_ACTIVATEAPP` to `Glue` in vanilla's order, so the
    game sees the switch although `Glue` itself never has the focus.
  - Once the game has handled the inactive `WM_ACTIVATEAPP`, every game thread entering
    `PeekMessageA` / `GetMessageA` is held there until the application is active again or closing.
    Sent messages are still handled while held, so no other thread ever waits on the game.
  - While held, the game's clocks stand still, and its sound effects, MCI devices and MIDI streams
    are paused (§5.8). On return it carries on where it stopped.
- While hidden, `Glue` is a tool window (`WS_EX_TOOLWINDOW`): no taskbar entry, not in alt-tab. If
  it becomes the foreground window anyway, the presenter takes the focus back, except while a game
  message box is open. DWM transitions are off for it, and the process has no "Not Responding"
  ghost windows (`DisableProcessWindowsGhosting`), since a held game fetches no messages.
- The `Glue` subclass handles `WM_WINDOWPOSCHANGING`: size changes pass through, the position is
  forced to the hidden position (catches `SetWindowPos`, `MoveWindow` and everything else).
- Closing the presenter (✕, Alt+F4) posts `WM_CLOSE` to `Glue`. `WM_DESTROY` of `Glue` (seen by
  the subclass) shuts the presenter thread down.
- Secondary DirectSound buffers get `DSBCAPS_GLOBALFOCUS` (vtable hook on
  `IDirectSound::CreateSoundBuffer`), so sound plays while `Glue` is hidden.
- `MessageBoxA` is owned by the presenter while A2 is active, so dialogs appear on top of it.

### 5.5 Deviations made in A2.1

- Position lock via `WM_WINDOWPOSCHANGING` in the subclass (as written above), not by hooking
  `SetWindowPos`/`MoveWindow`.
- No `background` key yet: activation loss is always filtered (`run`); `pause` comes in A2.4.
- The 1:1 frame is copied with `CopySubresourceRegion`; the scaling shader arrives in A2.2.
- `Glue` is hidden in the `CreateWindowExA` hook, before the game first shows it (§7), instead of
  after the first capture — the late hide showed the window for ~1.4 s.

### 5.6 Decisions made in A2.2

- `integer` with `aspect = stretch` uses the largest whole factor per axis (e.g. 3×2 on
  1920×1080): it ignores 4:3 while every game pixel stays a whole number of screen pixels.
- Shaders compile at runtime (`D3DCompile`); if that fails the presenter is disabled and the game
  unhidden — no separate 1:1 fallback.
- Live redraw while resizing: the window ticks every 15 ms during the modal size/move loop and
  the presenter renders a frame per tick.
- The presenter window is per-monitor DPI aware (V2, else V1); the game's window stays unaware.
  Presenter-window handlers that touch the game's windows switch back to the game's DPI context.
- Sharp-bilinear is single-pass ("sharp-bilinear-simple"): samples snap to the texel centre
  except in a band of width 1/prescale at texel edges.
- The game's display-mode changes are deferred while the presenter owns the display and replayed
  if it gives up (`core::defer_display_change`).

### 5.7 Decisions made in A2.3

- **Evidence first.** The books-menu issue was investigated before any fix, with diagnostics that
  stay in the code:
  - `LoadCursorA`, `SetClassLongA` and `RegisterClassA/ExA` are logged, and `SetCursor` changes
    too;
  - an opt-in message census (`[debug] message_census`) logs per window class which messages the
    game thread's windows receive (`WH_CALLWNDPROC` + `WH_GETMESSAGE`).
- `PresenterInput::cursor()` is the cursor shown over the client area; `nullptr` means none.
  `PresenterWindow::refresh_cursor()` applies a change at once instead of waiting for the next
  mouse move.
- **Scaled sprite:**
  - It is enlarged by the picture's whole factor per axis, at least 1 (`core::cursor_scale`).
  - Its hotspot sits on the top-left corner of game pixel g (`core::cursor_rect`).
  - It is drawn only while the real cursor is over the client area.
  - A cursor that cannot be converted is shown natively.
- **Cursor pixels** (`core::cursor_pixels`):
  - Monochrome "invert screen" pixels are drawn opaque white.
  - A colour cursor without alpha takes its alpha from the AND mask; under a set AND bit, black
    is transparent and other colours are opaque.
- The sprite cache is never evicted: the game has few cursors, and `LoadCursorA` handles live as
  long as the process.
- **Keyboard:** while the presenter runs, a `TranslateMessage` hook leaves the game's key-downs
  untranslated (`core::skip_translate`). The presenter already forwards the characters it made
  from the real keyboard state; translating again doubled every typed character.

### 5.8 Decisions made in A2.4

- **`pause` is a real freeze** (user's choice): the game's own reaction to deactivation only
  turns its music off. Holding happens in the message hooks, not by suspending threads, so sent
  messages and the game's dialogs keep working.
- **Clocks:** one `core::PauseClock` per time base (`GetTickCount`, `timeGetTime`). The hooks
  return real time minus the total time held, so the game does not catch up on return.
- **Sound effects:** DirectSound vtable hooks (`platform::VtablePatches`, which keeps the
  originals per vtable since the game's modules create several DirectSound objects). Buffers are
  tracked per device (`core::SoundPause`) and forgotten with it: releasing a device frees its
  buffers without their own `Release`. While held, playing buffers are stopped; the game's sound
  thread, which runs on, cannot restart them (`Play` is held back until resume, `Stop` cancels it,
  `GetStatus` reports them as playing).
- **Music:** MCI devices (`mciSendCommandA`) and MIDI streams (`midiStream*`) are tracked
  (`core::PlaybackPause`) and paused while held. An MCI device resumes with the game's own end
  notification, for its tune repeats.
- **Diagnostics that stay:** a first-chance access-violation log (`platform::AccessViolationLog`,
  first 5, with module+offset and stack), since the game exits with its own handler's code; the
  hold logs its thread and the counts of paused buffers, devices and streams; the performance
  line names the slowest frame's parts (`core::FrameStats`); a start-up brightness trace.

## 6. Configuration

New `horned-rat.ini` keys (existing keys from Stage A §6.1 unchanged; invalid values warn and use
the default):

```ini
[general]
background = run        ; run (keeps playing when not focused) | pause (freezes, silent)
[display]
cursor     = scaled     ; scaled | native
[debug]
message_census = false  ; diagnostics: log the messages the game's windows receive (A2.3)
```

There is no parking option: hiding on-screen is the only mode (§3).

## 7. Start-up and error handling ("never worse than vanilla")

Start-up:
1. `DllMain`: as in A1 (config, log, hooks).
2. `CreateWindowExA` hook: on the first top-level window (`Glue`), hide it (before the game
   shows it) and start the presenter thread.
3. Presenter thread: create window and D3D11 device (hardware, else WARP), capture one frame
   successfully, **then** subclass `Glue` and show the presenter.

| Failure | Response |
|---|---|
| No D3D11 device / window creation fails / first capture fails | Unhide `Glue`; game runs as in A1; reason logged |
| Capture fails (`BitBlt` false) | Keep the last good frame; log once; after 2 s of failures unhide `Glue` at (0,0) and close the presenter |
| Exception on the presenter thread | Top-level catch: unhide `Glue`, log, close the presenter |
| Exception in a hook | Guard logs and calls the original (unchanged) |
| `enabled = false` / `HORNED_RAT_DISABLE=1` | Pure passthrough (unchanged) |

**Unhide** = restore the original extended style (removing layered/transparent), move `Glue` to
(0,0) and show it; the subclass stays installed but passes everything through once inactive. Every failure path ends with a playable game.

Logging adds: presenter lifecycle, device type, scale mode and destination rect, hide/unhide with
reason, and every 10 s average capture ms, present ms and frame count, plus the slowest frame
and its capture and present parts (A2.4).

## 8. Testing

| Layer | Covers | Runs |
|---|---|---|
| `core` | viewport rects for every scale × aspect at many sizes; presenter ↔ game mapping round-trip and clamping; button-mask truth table; forwarded-message selection; child-relative coordinates; content-rect choice; cursor scale/hotspot; new config keys | CI: Windows + Linux (gcc/clang, sanitizers) |
| `platform` | `window_capture` on a test window painted with a known pattern, including live drawing while covered by another window; `hidden_window`: a hidden window covered by another still captures live drawing; hide/unhide round-trip; `presenter_d3d11` on WARP with exact pixel checks for integer/nearest; `child_input` posts reach the deepest child with correct coordinates; `window_subclass` reports an inactive `WM_ACTIVATE` as active and restores | CI: Windows x86 |
| `wing32` glue | thin; calls `core` + stored state | manual smoke test |
| Smoke test | additions: resize, every scale mode, Alt+Enter, both cursor modes, alt-tab and second monitor with `background = run`, menu clicks, typing a save-game name, battle mouse control, videos, dragging the presenter partly off-screen, panic switch | each milestone exit |

Red/green TDD for every behaviour (project rule).

## 9. Milestones

Each leaves the game playable; `enabled = false` always gives vanilla behaviour.

- **A2.1 — Presenter at 1:1.** Presenter thread, capture, D3D11 at exactly 640×480, hiding,
  subclass, full input path, unhide on failure. **Exit:** smoke test plays exactly like vanilla;
  fade check (frame-by-frame brightness of a screen
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
| The game reacts badly to its window being hidden (layered/transparent) | A2.1 smoke test |
| Standard controls ignore posted messages without real focus/capture (e.g. `Edit` caret) | A2.1 smoke test includes typing a save name; if needed, also post `WM_SETFOCUS`/activate the control |
| Game uses `SetCapture` and expects mouse messages to its captured window | `child_input` posts to the captured window (`GetCapture` on the game thread) when one is set |
| Hidden window partly outside the desktop (clipped drawing) | Kept inside its monitor's work area; never follows the presenter |
| Performance on weak GPUs | WARP fallback; capture is ~1.3 ms; 10 s performance log line |
| Torn frames | Accepted (§5.1) |

A2.1 exit results (`a2-1-exit.md`): the hidden window caused three bugs — child hit-testing,
muted DirectSound, dialogs under the presenter (which also hung the exit) — plus a start-up flash;
all fixed. Capture costs 4–6 ms on the hidden window (spike: 1.3 ms visible). Not yet tested:
typing a save name, battle mouse control, `SetCapture` behaviour, dragging partly off-screen,
click-through — carried into the A2.2 smoke test. A ~0.2 s brightness dip when the presenter first
appears is noted for A2.4.

A2.2 exit results (`a2-2-exit.md`): all scale modes, maximize, live resizing, borderless and
Alt+Enter work at ~60 fps up to 1920×1080. Two bugs found and fixed: the game's 640×480 display
switch and the missing `WM_QUIT` when closed with ✕/Alt+F4. Known issue moved to A2.3: in the
books menu the game's cursor and item hover subtitles do not appear (hypothesis: class cursors
and `WM_SETCURSOR`, which only reaches the window under the real cursor).

A2.3 exit results (`a2-3-exit.md`):
- **Hypothesis confirmed with a message census.** No game window ever received `WM_SETCURSOR`
  under the presenter. The game's classes carry sword/hand class cursors, and the game resets its
  wait cursor through that message. Forwarding it (§5.3) restored the cursor shapes, the hover
  tooltips, the man's remark in the books menu, and the end of the stuck wait cursor.
- **Both cursor modes pass the smoke test**, at an unchanged ~60 fps.
- **Typing doubled every character;** fixed with the `TranslateMessage` hook (§5.7).
- **Open:** æ/ø/å cannot be typed (accepted); a brief small cursor flash at exit;
  `aspect = stretch` and DPI above 100 % are still untested.

A2.4 exit results (`a2-4-exit.md`):
- **`background = run` and `pause` pass the smoke test**, as do `aspect = stretch` (with `sharp`
  and `integer`) and raised Windows display scaling.
- `pause` needed more than the activation messages: the game is held in its message loop, with
  still clocks and paused sound and music (§5.8). Nine bugs were found and fixed on the way,
  among them a crash from stale sound buffers and three ways the hidden window showed up
  (taskbar / alt-tab, restore animation, "Not Responding" ghost).
- Performance: ~600 frames / 10 s; the slowest frame is normally one refresh (~17 ms). Frames of
  45–63 ms occur only in `Present` at window or focus changes; accepted.
- **Open:** one unreproduced missed hold, the MCI pause that never acts (calls go through GOG's
  `WINMS.dll`), rare first-chance access violations inside USER32 (`a2-4-exit.md` §5).

**Stage A2 is complete.**
