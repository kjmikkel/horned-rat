# A2.2 exit: scaling

- **Date:** 2026-10-06
- **Build:** release build of commit `ff8bcbc`, GOG game build per Stage A spec §8, hardware D3D11,
  1920×1080 monitor at 100 % (a second monitor attached)
- **Sessions:** a first check, a logging run, then four smoke-test runs (`sharp`, `integer`,
  `nearest-fit`, `mode = borderless`). Logs are not committed.

## 1. Smoke test

| Item | Result |
|---|---|
| Monitor keeps its resolution | Pass (after the display-mode fix, §3) |
| Presenter opens centred at `window_size` | Pass (1280×960) |
| Live scaling while dragging the frame | Pass (~60 size updates per second, picture follows) |
| Maximize | Pass (1920×1057 client, picture 1409×1057) |
| `sharp` | Pass: crisp, fills at 4:3 |
| `integer` | Pass: always 1280×960, centred with bars |
| `nearest-fit` | Pass |
| `mode = borderless` at start | Pass (1920×1080, picture 1440×1080) |
| Alt+Enter windowed ↔ borderless | Pass, also 14 quick toggles in a row |
| Alt+Enter back to a *maximized* window | Not confirmed (never seen in the logs) |
| Clicks at large sizes, menus and battle | Pass |
| Quit with ✕ / Alt+F4 ends the process | Pass (after the exit fix, §3) |
| `aspect = stretch` | Not tested |
| Display scaling 125 %/150 % | Not tested (the user's monitor is at 100 %); covered by the DPI tests |
| Cursor shape in the books menu | **Fail**: OS arrow or loading icon instead of the game's cursor |
| Hover subtitles for items in the books menu | **Fail**: do not appear |
| Carried from A2.1 (save name, videos, off-screen drag, click-through) | Not tested |

## 2. Performance

Hardware D3D11, `vsync = true`: ~590–600 frames per 10 s at every size up to 1920×1080; capture
2.4–6 ms, present 9.5–14 ms (mostly waiting for vsync). The scaling shader adds nothing
measurable compared with A2.1's 1:1 copy.

## 3. Bugs found and fixed

1. **The game switches the monitor to 640×480.** `WHSHR.EXE` looks `ChangeDisplaySettingsA` up
   with `GetProcAddress` (so the IAT hooks never saw it) and sets 640×480, 32 bpp,
   `CDS_FULLSCREEN` at start-up; it restores and sets it again at exit. The A2 spike had missed
   this. The low resolution shrank the presenter window to 640×457. Fix: the `GetProcAddress` hook
   hands out a wrapper; while the presenter owns the display the call is recorded and reported
   successful (`core::defer_display_change`, tested), and replayed if the presenter gives up.
2. **The process did not exit when closed with ✕ / Alt+F4.** The game posts `WM_QUIT` only on its
   own exit path (the curtain dialog); closed any other way its main thread waited in
   `GetMessage` with no window left. Found with the new `tools/stack_dump` (main thread in
   `win32u!NtUserGetMessage`); posting `WM_QUIT` to the hung process by hand made it exit through
   `ExitProcess(0)`. Fix: the `Glue` subclass posts `WM_QUIT` after the game's `WM_DESTROY`.

## 4. Known issue for A2.3: cursor and hover subtitles

In the books menu the presenter shows the OS arrow (or the loading icon) instead of the game's
cursor, and item subtitles do not appear on hover. Hypothesis (unverified): `WHSHR.EXE` imports
`SetClassLongA` and ships `gmcur.dll`, so the game most likely sets cursors as window-class
cursors, which Windows applies in `WM_SETCURSOR` — sent only to the window under the *real*
cursor, which is always the presenter. The subtitles may hang off the same message. A2.3 (cursor)
starts by checking this with evidence.

## 5. Observations

- The hidden game window's position varies between runs ((0,0), (-8,-8) maximized, (480,270));
  capture worked in every case.
- At start-up the game sometimes restores the display mode right after setting it; both are
  deferred.
