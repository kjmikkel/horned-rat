# B1.1 exit: the sharp battle 3D view

Spec: `docs/superpowers/specs/2026-10-07-stage-b-battle-view-design.md` §9 (B1.1).
Plan: `docs/superpowers/plans/2026-10-07-stage-b1-1-hires-overlay.md`.

## 1. Setup

- 2026-10-07, release builds `bf79404` (run 1) and `0663755` (runs 2–4), GOG build (`GAMEF.DLL`
  SHA-256 recognised in every B1 run), `scale = sharp`.
- `render_scale` at its default (`auto`) except in run 4. Each run: at least one battle with camera
  and unit movement.

## 2. Runs

| Run | Build | Window | Factor | What the user saw |
|---|---|---|---|---|
| 1 | `bf79404` | 1280×960 | 2 | The 3D view is sharp, but **the landscape sometimes flickered while the camera panned** (§4). |
| 2 | `0663755` | 1280×960 | 2 | No flicker. |
| 3 | `0663755` | 1280×960, then fullscreen 1920×1080 (Alt+Enter) | 2, then 3 | Unit info, the menu and the exit question over the 3D view all showed normally; fullscreen stayed sharp and smooth, and Alt+Enter rebuilt our device within about 0.1 s. Includes the world-map ambush of §5. |
| 4 | `0663755` | 1280×960, `render_scale = 1` | – | The old blocky view, as in vanilla; no `b1:` and no `3D view` lines in the log. |

Runs 1–3 logged `b1: known GAMEF.DLL build, sharp 3D view available` and `b1: device 840x592 (x2)`
or `1260x888 (x3)`, and no `b1: off` line.

## 3. Figures

From the 10 s `presenter: 3D view xK` lines (about 91 renders per 10 s, one per game frame):

| Factor | Device | Render avg | Max |
|---|---|---|---|
| 2 | 840×592 | 1.1–1.7 ms | 1.5–2.8 ms |
| 3 | 1260×888 | 2.2–2.8 ms | 3.2–4.4 ms |

The presenter kept its 60 frames per second (about 601 frames per 10 s) with the overlay on. The
overlay line read `601 overlay draws, 0–1 from the previous render, 0 mostly hidden` in normal play.
The slow-render fallback (mean over 50 ms) was never close.

## 4. Problem found and fixed: flicker while panning

**Seen:** in run 1 parts of the landscape flickered while the camera panned.

**Cause:** the game thread publishes our k× frame just before the game blits its 1× frame. The
presenter's capture can still show the previous frame at that moment, so the mask (capture vs the
game's new 1× frame) marked most of the view as "covered" and left it out for one presenter frame.
While the camera pans, every game frame differs from the one before, so this happened often.

**Fix (`0663755`):** the presenter keeps the previous render and its mask as well. When the current
mask covers under half the view and the previous one covers more (`core::prefer_previous`), it draws
the previous render. Unit-tested; the user saw no flicker afterwards.

## 5. Observation, not confirmed

During a world-map ambush the user saw a brief flash, and was not sure of it. B1 only renders and
draws inside battles (the overlay needs a frame younger than 0.25 s from the battle view), so it is
probably not related. Not filed; worth watching in later runs.

## 6. Open items

- The half-detail pause (`W+0x329A0`), the narrow↔wide layout switch and a performance line in the
  smoke test belong to B1.2 and were not exercised here.
- Issue #14 (occasional larger differences at 1×) does not affect B1.1: the mask compares the
  capture with the game's own 1× frame, not with ours.
- Issue #11 (the view updates only ~10 times a second) and #12 (B2) stay open.

## 7. Result

**B1.1 passes.** The battle 3D view is rendered at 2× in a 1280×960 window and 3× fullscreen, for
under 3 ms of the game thread per frame, with everything drawn over the view still visible and
`render_scale = 1` giving the original look.
