# A2.4 exit: background and polish

## 1. Investigation

Both runs: 2026-10-06, build `fad4ea5`, release build, hardware D3D11, 1280×960 window, `scale = sharp`, `cursor = scaled`.

### Run A (`background = run`)

- **What the user saw:**
  - No flicker or darkening when the presenter appears.
  - Alt-tab away in the menu: the music keeps playing; the picture is right on return.
  - Alt-tab away in a battle: the units keep moving.
- **Log:**
  - `presenter: lost focus, game runs on` / `presenter: focused` at each switch.
  - Twice the hidden game window became the foreground window (`foreground=Glue` at 75.3 s and 88.0 s) during alt-tab. The user had switched to it: `Glue` is listed in alt-tab and the taskbar (see run B).
  - Performance: 595–602 frames / 10 s, capture 2.8–5.2 ms, present 11.4–13.1 ms. The slowest frame is usually 17–32 ms (one missed refresh), but every 30–100 s one frame takes ~65 ms (4 refreshes). The log does not say which part is slow.

### Run B (`background = pause`)

- **What the user saw:**
  - Menu: the music stops while away and resumes on return.
  - Battle: the music stops, but **the units keep moving**.
  - The taskbar shows two entries for the game (grouped). Alt-tab lists a separate, smaller copy of the picture: the hidden `Glue` window.
  - The exit-question check was not done: the plan asked for the presenter's ✕, which closes the game without a question (A2.2 behaviour). The question comes from quitting inside the game.
- **Log:** the activation round trip works.
  - Each switch away logs `presenter: lost focus, game paused`, then `Glue WM_ACTIVATE 0 -> 0` and `WM_ACTIVATEAPP 0 -> 0`. Each return logs `focused, game resumed`, then `WM_ACTIVATEAPP 1 -> 1` and `WM_ACTIVATE 1 -> 1`.
  - The game's own reaction to deactivation:
    - `ShowWindow(Glue, SW_SHOWNORMAL)` every time;
    - the first time, `ChangeDisplaySettingsA(restore registry mode)`, which the presenter defers, and a resize of `Glue` from maximised to 656×520. The presenter showed one frame with a wrong destination rect (`34,0 1211x960`) during that resize, too short to see.
  - No minimise (`WM_SIZE 1`, `SW_MINIMIZE`) and no hide (`SW_HIDE`).
  - Nothing stops the game's main loop: in a battle the game keeps simulating.
- **The game's clocks and loop** (from the import tables):
  - `WHSHR.EXE` runs the loop with `PeekMessageA` / `GetMessageA`.
  - Time comes from `GetTickCount` (`WHSHR.EXE`, `GAMEF.DLL`) and `timeGetTime` (`OMNI000R.DLL`, `WINMS.DLL`), plus `SetTimer`.

### Start-up brightness

- Run A logged a single `diag: brightness 53.6` 0.01 s after `presenter: running`. Run B was the same. There was no further change of 4 or more in the first 5 s.
- **Conclusion:** the A2.1 dip (46 → 15 → 53.6) is gone. Most likely hiding `Glue` at creation (A2.1 bug 4) removed it: the presenter no longer appears over an already-drawn game window. No fix needed.

### Decisions from the investigation (with the user)

1. **Fix 5a (keep the window restored) is not needed:** the game never minimises or hides `Glue`.
2. **Fix 5b (non-client deactivation) is not needed:** there is no dip.
3. **New, 5c: keep `Glue` out of the taskbar and alt-tab.**
   - While hidden it gets `WS_EX_TOOLWINDOW` (without `WS_EX_APPWINDOW`); unhiding restores its style.
   - If it becomes the foreground window anyway, the presenter takes the foreground back.
4. **New, 5d: `pause` freezes the game (user's choice).** The game's own reaction (music off) is not a pause.
   - While unfocused, the game thread is held in its message loop (the `PeekMessageA` / `GetMessageA` hooks), but only after the game has handled the inactive messages. Messages sent to the game thread are still handled, so the presenter and Windows never wait on it.
   - The game's clocks (`GetTickCount`, `timeGetTime`) stand still while it is held, so it does not catch up on return.
5. **New, 5e: the performance line names the slow part of the slowest frame** (user's choice), to find the ~65 ms stutter.
