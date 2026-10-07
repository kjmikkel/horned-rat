# B1.0 exit: spike for a second Reality Lab render

Spec: `docs/superpowers/specs/2026-10-07-stage-b-battle-view-design.md` §9 (B1.0).
Plan: `docs/superpowers/plans/2026-10-07-stage-b1-0-spike.md`.

## 1. Setup

- 2026-10-07, release builds `0500dbc` … `6b7f20a` (run 1e also copied the game device's shade table into ours at frame 100, an experiment removed afterwards), GOG build (`GAMEF.DLL` SHA-256 recognised in every run), 1280×960 window, `scale = sharp`.
- `[debug] b1_spike = k` turned the spike on. Each run: one battle, about a minute or more with camera movement.
- Nothing from the spike is shown on screen; the comparison works on the game's frame and ours in memory, plus BMP dumps (kept out of the repo).

## 2. Runs

| Run | Build | k | Device | Result |
|---|---|---|---|---|
| 1 | `0500dbc` | 1 | 420×296 | Scene rendered (seen in a greyscale view of the indices). ~122 000 of 123 432 pixels "differ": our palette is all black, **0 palette callbacks**. Render 0.47 ms. |
| 1b | `30939a0` | 1 | 420×296 | Compared with the **game's palette**: 54 100 differ, the same as by index. Differences are swaps between neighbouring colours of the same texture, spread evenly over textured surfaces; geometry identical. |
| 1c | `30939a0` + device as wide as the game's (committed with `321bebf`) | 1 | 624×296 | Exactly the same counts as 1b (54 100 / 54 693 / 54 698 on frames 1–3): device width is not the cause; the difference is deterministic. |
| 1d | `321bebf` | 1 | 624×296 | Field-by-field diff of the game's and our viewport and device objects (below). |
| 1e | `6b7f20a` | 1 | 624×296 | Viewport configured 4-aligned like the game: **2 of 123 432 pixels differ** on frames 1–3 and in the dumped frame (two isolated neighbouring-colour swaps). Later frames mostly 1–85; rare frames up to 10 012 (§5). Render 0.49 ms. |
| 2 | `6b7f20a` | 2 | 840×592 | No errors; render **1.5 ms** avg, 3.1 ms max over ~900 frames. Same view as the game, finer edges. The user saw no change and no stutter. |
| 3 | `6b7f20a` | 3 | 1260×888 | No errors; render **2.8 ms** avg, 5.4 ms max over 1 350 frames. Same view, finer outlines and banner detail. |

All runs: the game looked and played as before; our device was destroyed cleanly at the end of each battle; no exception inside the spike. The USER32 first-chance access violations in some logs are the known issue #9 (presenter thread, outside battles).

## 3. What we learned

1. **A second device works.** It renders the game's scene through the game's camera, on the game thread, during the game's own paint, and does not disturb the game's frame.
2. **The ramp palette is shared.** Reality Lab's 8-bit colour allocation belongs to the driver, not the device: our device never gets a palette callback and its pixels are the game's palette indices. The presenter can colour our frame with the game's palette (read from the WinG DIB's colour table).
3. **The viewport must be configured 4-aligned**, as the game does (420 for the 417-wide view, `420·k` at k×), showing the first `417·k` columns. With 417 the projection centre and scale shifted by a fraction of a pixel, which swapped neighbouring texture colours across the whole view.
4. **Not causes:** the device's width (417→624 changed nothing) and a per-device 256-byte table plus a counter at device `+0x50` that differ between the two devices (copying the game's values changed nothing measurable).
5. **What k× gains:** finer polygon edges, unit outlines, banners and texture sampling; textures and sprites keep their original detail (they are sampled more often, not replaced).

## 4. Cost

| k | Device | Render avg | Max |
|---|---|---|---|
| 1 | 420/624×296 | 0.5 ms | 2.3 ms |
| 2 | 840×592 | 1.5 ms | 3.1 ms |
| 3 | 1260×888 | 2.8 ms | 5.4 ms |

The render runs once per game frame (100 ms timer), so even k = 3 uses under 3 % of the game thread's time. The spec's slow-render fallback (§7, 50 ms) is far away on this machine.

## 5. Open items

- **Occasional larger differences at k = 1** (hundreds to ~10 000 pixels in a few logged frames, while most frames differ by 0–85). Not investigated; candidates are animated state that changes between the game's render and ours. Only matters for the 1× comparison, which B1.1 does not need.
- The spike's diagnostics (1× comparison, object diff, BMP dumps) stay in the code until B1.1 replaces the spike with the real overlay.

## 6. Recommendation

**Go for B1.1.** The approach works, costs little, and needs two corrections to the spec, both now known: use the game's palette for our frame (no own palette), and configure the viewport 4-aligned (`align4(w)·k` wide).

Filed as GitHub issue #14 (the occasional larger differences).
