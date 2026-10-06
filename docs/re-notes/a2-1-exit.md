# A2.1 exit: presenter at 1:1

- **Date:** 2026-10-06
- **Build:** release build of commit `030e9fa`, GOG game build per Stage A spec §8, hardware
  D3D11
- **Sessions:** several short runs (intro, main menu, books menu, exit dialog, quit) plus two 90 s
  fade-check runs. Logs and probe CSVs are not committed.

## 1. Smoke test

| Item | Result |
|---|---|
| Game appears in the presenter; the original window is not visible | Pass (after the flash fix, §3) |
| Menu hover and clicks | Pass (after the hit-test fix, §3) |
| Sound effects and speech | Pass (after the DirectSound fix, §3) |
| Game dialogs (`MessageBoxA`) appear on top and can be answered | Pass (after the owner fix, §3) |
| Closing the presenter quits the game; the process ends | Pass |
| Intro plays inside the presenter | Pass |
| Panic switch `enabled = false` gives vanilla | Pass (used for the fade check) |
| Log: `presenter: running`, performance lines, no `exception` | Pass |
| Typing a save-game name | Not yet tested |
| Battle mouse control, map scrolling | Not yet tested |
| Briefing videos | Not yet tested |
| Clicks on the desktop do not reach the game | Not yet tested |
| Presenter dragged partly off-screen keeps updating | Not yet tested |
| Other windows clickable over the hidden game window | Not yet tested |

The untested items carry over to the A2.2 smoke test.

## 2. Performance

Presenter log, hardware device, `vsync = true`: ~595 frames per 10 s (display refresh), capture
**4–6 ms**, present ~11 ms (mostly waiting for vsync). Capture is slower than the spike's 1.3 ms;
the spike captured a visible window, this one is layered with alpha 0. Fine for 60 fps; revisit if
A2.2's scaling pass pushes the frame over budget.

## 3. Bugs found and fixed during the exit

1. **Clicks went to the wrong window.** `ChildWindowFromPointEx` hit-tests the parent first and
   finds nothing under a layered alpha-0 parent, so presenter clicks were posted to `Glue` itself,
   which ignores them. Fix: geometric descent over visible, enabled, non-transparent children
   (`child_input`, regression test).
2. **No sound effects or speech.** The game calls `SetCooperativeLevel(Glue, DSSCL_NORMAL)` and
   creates its buffers without `DSBCAPS_GLOBALFOCUS`, so DirectSound mutes them while `Glue` is not
   foreground — which, hidden, it never is. Music (MIDI) was unaffected. Fix: vtable hook on
   `IDirectSound::CreateSoundBuffer` adds `GLOBALFOCUS` to secondary buffers
   (`core::with_global_focus`, tested).
3. **Dialogs piled up under the presenter, and the game never exited.** `MessageBoxA` was owned
   by the hidden `Glue`. Each box ran a modal loop on the game thread, so after closing the
   presenter the game was stuck inside an invisible dialog and the process stayed alive. Fix:
   `MessageBoxA` hook sets the presenter as owner while A2 is active; the exit hang disappeared
   with it (log: `ExitProcess(0)` and DLL detach ~2 s after the window is destroyed).
4. **Full-screen flash at start-up.** `Glue` was hidden only after the D3D device existed
   (~1.4 s). Fix: hide it in the `CreateWindowExA` hook, before the game first shows it; every
   presenter start-up failure unhides it. Now hidden 0.016 s after creation.

## 4. Fade check

`luma_probe` recorded the mean brightness of the game's composed picture for the first 90 s
(intro, no input), once vanilla and once with A2, into one CSV (run and mode per row).

| | vanilla | A2 |
|---|---|---|
| Samples | 5684 | 5697 |
| Brightness min / max | 0.0 / 54.8 | 0.0 / 54.8 |
| Mean | 13.83 | 13.81 |
| Distinct levels | 62 | 64 |
| Changes between samples | 176 | 178 |

Aligned in 0.1 s bins, the best time shift is 0 s and the mean difference **0.07** (of 255).
Vanilla shows ramps of intermediate levels, so **palette fades do work on a modern 32-bit
desktop**, and A2 reproduces them step for step. "Restore palette effects" is **not** needed as a
separate item.

One difference: at ~1.3 s, when the presenter window appears, A2 dips from 46 to 15 and back to
53.6 within ~0.2 s, while vanilla goes straight to 53.6. Likely the game reacting to losing
activation to the presenter (unverified). Cosmetic, first frame only; noted for A2.4.

## 5. Deviations from the plan

- **Position lock** is done in the `Glue` subclass on `WM_WINDOWPOSCHANGING`, not by hooking
  `SetWindowPos`/`MoveWindow`.
- **Always active:** A2.1 has no `background` key yet; activation loss is always filtered
  (`background = run` behaviour). `pause` comes in A2.4.
- **1:1 copy** uses `CopySubresourceRegion` into the back buffer instead of a shader pass; the
  scaling shader arrives in A2.2.
- **Hidden at creation** (bug 4), not after the first successful capture.
- **New hooks** not in the spec: `DirectSoundCreate` (+ vtable), `MessageBoxA`, and the
  diagnostic `ExitProcess`.
