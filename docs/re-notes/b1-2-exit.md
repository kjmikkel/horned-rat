# B1.2 exit: layouts and detail for the sharp 3D view

Spec: `docs/superpowers/specs/2026-10-07-stage-b-battle-view-design.md` §9 (B1.2).
Plan: `docs/superpowers/plans/2026-10-12-stage-b1-2-polish.md`, including its outcome note.

## 1. Setup

- 2026-10-12, GOG build (`GAMEF.DLL` recognised in every run), `scale = sharp`, 1280×960 window
  and fullscreen 1920×1080.
- Builds:
  - `4d785ad`: the sharp view also rendered in low detail;
  - a diagnostic build of it (not committed): it timed every `Map3DClass` message and logged gaps
    between 3D frames;
  - `d8694b1`: low detail pauses the sharp view as in B1.1; the B1.2 code was reverted.

## 2. Runs

| Run | Build | What the user did | What the user saw |
|---|---|---|---|
| 1 | `4d785ad` | Low/high detail switches, window and fullscreen | Low detail looked sharp. The **whole game froze** several times and finally hung; ended by the user (§4). |
| 2 | `4d785ad`, `render_scale = 1` | Low detail, B1 off | Pixelated as vanilla; no freeze. |
| 3 | diagnostic | Low detail with B1 on; Widescreenbutton; fullscreen | No freeze. Low detail looked sharp, which made the setting pointless (§3). |
| 4 | `d8694b1` | Low and high detail; Widescreenbutton both ways; fullscreen | Low detail pixelated, high detail sharp; the wide view stays sharp. |

## 3. Decision: low detail stays pixelated

Low detail exists to save CPU on 1995 hardware. Rendering the sharp view in it costs 4–9× the
game's own pixels, and it hides the look the player chose. The only freeze seen also happened in
this combination. So the user chose B1.1's behaviour: in low detail the sharp view pauses, and the
presenter shows the game's own frame within 0.25 s (the overlay's freshness limit).

The B1.2 code for the sharp variant was reverted (`5e3f76d`, `704f9e6`). That code was
`core::stretch_indices`, which matched GDI's `StretchBlt` with centre sampling, plus a test
against real GDI. The measured GDI rule is kept in `Documentation/battle-3d-view.md` in case a
later stage needs it.

## 4. The freeze (issue #17)

**Run 1:**
- the game's 3D frames per 10 s dropped to 58–83 (normally about 91) in several windows;
- from about 255 s there were none, while the presenter kept running;
- the game left through `ExitProcess(0)`;
- no exception, no `b1: off`; our render stayed at 1–2.6 ms.

**Not reproduced** with B1 off (run 2) or with B1 on under the diagnostic build (run 3). In run
3 the only gaps over 0.5 s were 1.7–2.0 s at the detail switches, while the options menu was open.
Nothing ties the freeze to our code. It is filed as issue #17, with what to capture next time.

## 5. Figures

| Layout | Factor | Device | Render avg | Max |
|---|---|---|---|---|
| narrow (417×296) | 2 | 840×592 | 1.6–1.8 ms | 2.7 ms |
| wide (624×296) | 2 | 1248×592 | 1.6 ms | 3.6 ms |
| narrow | 3 | 1260×888 | 2.3–2.8 ms | 4.9 ms |
| wide | 3 | 1872×888 | 2.3–4.8 ms | 7.7 ms |

The presenter held about 600 frames per 10 s, apart from the stalls in run 1.

## 6. In game

- The wide layout is switched with the **Widescreenbutton**, its name in the game's menu; it has
  no tooltip. It is a 24×8 button in the top-right corner of the battle screen. It hides the 2D
  map panel and widens the 3D view to 624×296.
- Our device is rebuilt within a frame of each switch. The previous-render fallback ignores
  renders of the old size, so no overlay at the wrong size was seen.

## 7. Open items

- Issue #17: the unexplained freeze.
- Still open from before: #11 (10 Hz 3D updates), #12 (B2), #14 (1× differences).

## 8. Result

**B1.2 passes, and Stage B1 is complete.** The sharp 3D view works in both layouts, in the window
and fullscreen, for under 5 ms of the game thread per frame. Low detail keeps the game's own look.
