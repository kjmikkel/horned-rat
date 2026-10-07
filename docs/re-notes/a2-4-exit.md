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

### Test rounds after the fixes (`background = pause`)

Each round: the user's report, then what the log showed and what changed. Builds in order.

1. **`8420e71`** (hold, still clocks, taskbar fix):
   - Taskbar: one entry. Battle: units stand still and carry on smoothly, without a jump.
   - Looping battle effects (fighting, charging) kept playing while held, and cut-scene music and effects too.
   - Clicking quickly back and forth once showed a small copy of the window at the top left. Cause: the game restores its maximised hidden window on its first focus loss, and Windows animates the restore. Fix `cdd78e3`: no DWM transitions for the hidden window.
   - The exit question went behind the presenter. Cause: Windows activates `Glue` on the way to the message box, and the focus take-back fired; an open box does not disable the presenter on another thread. Fix `7b63e66`: no take-back while a game message box is open (the `MessageBoxA` hook counts them).
   - Sounds: `7b63e66` tracked the secondary buffers and stopped the playing ones while held.
2. **`7b63e66`**: alt-tab during a cut-scene: silent, then **the game crashed** on return.
   - The game exits with the exception code from its own handler (`ExitProcess(0xC0000005)`), so a first-chance access-violation log was added (`4fd2a4a`), plus buffer counts per hold (`c0f5d92`).
3. **`c0f5d92`**: the crash again. Log and PDB:
   - `access violation reading 0x00690068 at DirectSoundControl::stop_if_playing < SoundPause::pause < pause_game_sounds`.
   - **Cause:** the game makes several DirectSound objects (`WHSHR.EXE`, `MSNDDS.DLL`, `OMNI000R.DLL`). Releasing one frees its buffers without their own `Release`, so the list held freed buffers ("59 known", 3–4 stopped).
   - The user chose to fix the tracking over muting the process's audio session. Fix `9588dcb`:
     - buffers belong to their DirectSound object and are forgotten with it;
     - each patched vtable keeps its own originals (`platform::VtablePatches`);
     - while paused, the game's sound thread (which runs on) cannot restart a stopped buffer: `Play` is held back until resume, `Stop` cancels the restart, and `GetStatus` reports stopped buffers as playing.
4. **`9588dcb`**: no crash. Battle completely silent while away, and sounds end with the level. Exit question stays modal. No small window.
   - MIDI music still played during a cut-scene and the first books scene. The menus play MIDI through `MIDI.DLL` (MCI sequencer, through GOG's `WINMS.dll` layer); the videos play MIDI streams (`OMNI000R.DLL`).
   - The user chose to pause it. Fix `6192f96`: MCI devices and MIDI streams are tracked (`core::PlaybackPause`) and paused while held. An MCI device resumes with the game's own end notification, for its tune repeats.
5. **`6192f96`**: menu music silent while away, resumes and loops; cut-scene silent and in sync on return.
   - The log showed 0 MCI devices and 0 streams paused in that run: the game had paused its music itself, and `MIDI.DLL`'s MCI calls go through `WINMS.dll`, which passes only open and close through the hooked `WINMM` import.
   - After a long hold in the books menu, the small window came back. Cause: Windows' "Not Responding" ghost of a window whose thread fetches no messages for ~5 s. Fix `717b6d3`: `DisableProcessWindowsGhosting`.
6. **`717b6d3`**: no ghost. After "new game", the first time on the book screen the animations did not stop.
   - The log: `WM_ACTIVATEAPP 0` handled, then no `game held` for the whole time away, while the game kept drawing.
   - Hypothesis: that screen runs its loop on another thread. Change `5d017ae`: every game thread with a message loop is held, each hold logged with its thread.
7. **`5d017ae`**: animations stop on the first book screen, everything resumes, battles freeze and resume.
   - But every hold in the log was on the main thread, so the hypothesis is **refuted**. The missed hold of round 6 was not reproduced and is not explained (§5).
   - A cut-scene hold paused 2 of 2 sound buffers and 1 MIDI stream.

## 2. Smoke test

2026-10-07, build `5d017ae`, release build, hardware D3D11, 1280×960 window, `cursor = scaled`. Three runs, each against the full checklist in `docs/smoke-test.md` unless noted.

| Run | Settings | Result |
|---|---|---|
| 1 | `background = pause`, `scale = sharp`, `aspect = 4:3`; Windows display scaling raised above 100 % by the user | All items pass, including the A2.4 background items, the carried A2.1–A2.3 items and the DPI item (picture stays crisp). |
| 2 | `background = run` | All items pass. The log shows `lost focus, game runs on` at each switch, no hold and no access violation. |
| 3 | `aspect = stretch` with `scale = sharp`, then `scale = integer` | Both fill the client area as specified. `integer` gives 2×2 at 1280×960 (no bars), 3×2 at 1920×1080 (`dest (0,60 1920x960)`) and when maximised (`dest (0,48 1920x960)`). Clicks and the cursor's tip land correctly, also near the bars. |

The ini was restored to its original content after run 3.

## 3. Performance

- Normal play: 600–601 frames per 10 s, capture 2.7–6.7 ms, present 10–14 ms (the present time includes the wait for vsync).
- **The slowest frame** is 16.7–17.4 ms in almost every 10 s line, i.e. no missed refresh.
- In run 3 (about an hour, 372 lines), only 3 lines had a slowest frame above 21 ms (32–34 ms, one missed refresh). Two were in `Present` (at start-up and at an Alt+Enter); one was in capture (25.9 ms).
- The ~65 ms frames seen in the investigation (§1, run A) appeared in the test rounds as 45–63 ms frames, always in `Present` and always at a window or focus change (alt-tab, resize, Alt+Enter). They did not appear in the smoke runs during play. They are the cost of DXGI/DWM reacting to the window change and are accepted.

## 4. Bugs found and fixed

| # | Bug | Cause | Fix |
|---|---|---|---|
| 1 | `pause` did not stop the game (units kept moving) | The game's own reaction to deactivation only turns its music off | `5d017ae`, `8420e71`: the game's message loop is held while unfocused; `GetTickCount` / `timeGetTime` stand still meanwhile |
| 2 | Two taskbar entries; a small copy of the picture in alt-tab | The hidden `Glue` was still an app window | `ae3a22c`: `WS_EX_TOOLWINDOW` while hidden; take the foreground back if `Glue` gets it |
| 3 | A small copy of the window at the top left after quick switching | DWM's restore animation of the hidden window | `cdd78e3`: DWM transitions off while hidden |
| 4 | Same symptom after a long hold | Windows' "Not Responding" ghost | `717b6d3`: `DisableProcessWindowsGhosting` |
| 5 | The exit question went behind the presenter | The focus take-back fired while Windows activated `Glue` for the box | `7b63e66`: no take-back while a game message box is open |
| 6 | Looping effects and cut-scene sound kept playing while held | The game's sound threads run on | `7b63e66`, `9588dcb`: secondary buffers tracked and stopped; `Play` held back until resume |
| 7 | Crash on return from a cut-scene | Freed buffers of a released DirectSound object stayed in the list | `9588dcb`: buffers tracked per DirectSound object; vtable patches per vtable |
| 8 | MIDI music played while held | MCI devices and MIDI streams play in Windows' threads | `6192f96`: MCI devices and MIDI streams paused |
| 9 | No way to tell which part of a slow frame is slow | The performance line had averages only | `d7c3b67`: the line names the slowest frame and its capture / present parts |

## 5. Open items

- **Missed hold on the first book screen** (test round 6): the game handled `WM_ACTIVATEAPP 0` but was never held, and kept drawing. Not reproduced after `5d017ae`; the hypothesis (another thread) was refuted. The hold logs its thread, so a repeat can be diagnosed.
- **Music in test round 4** that the MIDI hooks did not see: unexplained, not seen since `6192f96`.
- **The MCI pause has never acted:** `MIDI.DLL` sends its MCI commands through GOG's `WINMS.dll`, and only open and close pass the hooked `WINMM` import. The game pauses its own menu music on deactivation, so nothing is audible. Hooking `WINMS.dll`'s imports would close the gap if needed.
- **First-chance access violations in USER32** on the presenter thread (about one per hour, e.g. reading 0x0 at `USER32.dll+0x263FC` under `dxgi.dll`). Windows handles them itself; no effect seen. Logged by the access-violation log, which stops after 5.
- **Nested activation handling:** if the game's `WM_ACTIVATEAPP` handler re-entered, `g_told_inactive` could be stale for one message. Not seen; noted.
- Carried from A2.3: æ/ø/å cannot be typed (accepted); a brief small cursor flash at exit.
