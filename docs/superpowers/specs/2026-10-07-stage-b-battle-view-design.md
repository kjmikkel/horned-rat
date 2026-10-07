# Stage B: Battle 3D View — Design

- **Date:** 2026-10-07
- **Status:** Approved (design). B1 is specified here; B2 is a sketch and gets its own spec.
- **Builds on:** `2026-10-05-stage-a2-presenter-design.md` (the presenter, capture and mapping).
- **Inputs:** `docs/re-notes/a1-findings.md` §4–5 and the reverse engineering of `GAMEF.DLL`.
  The detailed notes are kept outside the repository, in `Documentation/` next to the checkout,
  because they are derived from disassembly; this spec states only what the design needs.

## 1. Goal

- **B1 — sharper 3D view:** the battle's 3D view is rendered by Reality Lab at k× its size and
  shown in the presenter at that resolution, in the unchanged 640×480 layout. Same camera, same
  picture, more detail.
- **B2 — bigger 3D view (later):** more of the battlefield visible at once.

Non-goals for B1: a higher 3D frame rate (vanilla renders the view on a 100 ms timer), changes to
the UI layout, unit picking, colour depth (the view stays 8-bit ramp mode), other game builds.

## 2. Facts this design rests on

From `GAMEF.DLL` (GOG build):
- **Reality Lab is statically linked into `GAMEF.DLL`;** only the RLDDI driver layer is imported
  from `RLDDF.DLL`. The software rasteriser is `RL\SOFT\RLFRM256.DLL` (float, ramp, 8-bit).
- **The 3D view is a `Map3DClass` child window** with a view object in the battle window's data
  block. The object holds two layouts: 417×296 (projection extents ±0.5) and 624×296 (extents
  ±0.748 horizontally, a wider field). Vanilla switches between them at runtime, hiding a side
  panel for the wide one.
- **Device:** on `WM_CREATE` the game creates an 8-bit WinG bitmap of 624×296 and a Reality Lab
  memory driver on its bits, with a palette callback; then frames (scene, camera, light) and a
  viewport on the camera frame. Device window, viewport extents and field of view come from the
  active layout; a detail divisor (1 or 2) renders at half size.
- **Frame:** a 100 ms `WM_TIMER` updates the camera and simulation and invalidates the view;
  `WM_PAINT` clears and renders the viewport, then copies the bitmap to the window with
  **`WinGBitBlt`** (divisor 1) or **`WinGStretchBlt`** (divisor 2). Both are functions of our proxy
  `wing32.dll`, so the game hands us the finished frame.
- Mouse camera steering maps the cursor through `ScreenToClient(Map3DClass)`; it does not depend on
  the render size.

## 3. Approach

**A second, high-resolution render of the game's own scene.** When the game blits its 3D frame,
our code renders the same scene through the game's camera with a second Reality Lab device of k×
size (calling `GAMEF.DLL`'s internal Reality Lab functions by address), and the presenter draws it
over the 3D view's rectangle. The game's own render, layout, input and state are untouched; on any
failure the picture is today's.

Rejected:
- **Enlarging the game's own render:** window and render sizes share fields, so it needs code
  patches, and every user of the sizes (steering, picking, the layout switch) found and corrected.
- **An upscaling filter only:** no real detail.

## 4. Architecture

Layering unchanged: `wing32 → platform → core`; `core` has no Windows headers.

```
core/
  render_scale.{h,cpp}   k from the 3D rect's on-screen scale and the ini key
  overlay_mask.{h,cpp}   CPU reference of the per-pixel overlay decision (§5.3)
  config (extended)      render_scale key (§6)
platform/
  palette_frame.{h,cpp}  8-bit pixels + 256-entry palette → 32-bit BGRA
  presenter_d3d11 (ext.) overlay pass: k× texture into a destination rect, masked
wing32/
  gamef_layout.{h,cpp}   hash-gated table of GAMEF.DLL addresses and offsets
  rl_shadow.{h,cpp}      our device/viewport; render on the game's blit; publish frames
```

## 5. Data flow

### 5.1 The second render (game thread)

In the `WinGBitBlt` / `WinGStretchBlt` hooks, when the source DC has the 3D view's bitmap selected
(we created it, so we know it) and `gamef_layout` is active:
1. Read the view object (layout, detail divisor, viewport, camera and scene frames) from the
   battle window's data block.
2. If k, the layout or the battle changed, (re)create our device on our own 8-bit buffer of
   `align4(w)·k × h·k`, and a viewport on the game's camera frame configured `align4(w)·k` wide
   like the game's, through the same internal functions the game uses for its own device. (The
   ramp palette is shared by all devices, so our pixels are the game's palette indices; B1.0.)
3. Copy the game viewport's current extents and field of view to ours.
4. Clear and render the game's scene frame through our viewport.
5. Publish {our buffer, the game's 1× frame, the game's palette, w, h, k, time} to the
   presenter (double-buffered, swapped under a mutex).
6. Call the original blit (always, also after a failure in 1–5).

### 5.2 Choosing k

- `render_scale = auto`: k = the 3D rect's on-screen scale (the presenter's destination scale per
  axis) **rounded up**; the larger axis wins; clamped to 1..4. Examples: 1280×960 → 2;
  1920×1080 with `sharp` (2.25) → 3.
- `render_scale = 1..4`: fixed. `1` turns B1 off (vanilla view).
- The presenter publishes k atomically; the game thread rebuilds its device only between frames.

### 5.3 Overlay (presenter thread)

- The `Map3DClass` rectangle in `Glue` client coordinates → destination rect through
  `core::viewport`, exactly like the main picture.
- The overlay pass draws our k× texture into that rect with the active scaling shader.
- **Per-pixel mask:** a 1× pixel shows the hi-res block only where the captured picture equals the
  game's own 1× render (index → palette → BGRA) at that point. Anything the game draws over its
  view (tooltips, menus, dialogs) keeps the captured pixels; a capture briefly out of step with the
  render shows vanilla pixels for a frame.
- Palette conversion is done on the CPU (`palette_frame`): ~1.6 Mpx at k = 3, 10 times a second.
- **Freshness:** the overlay is drawn only while the newest frame is younger than 250 ms and
  `Map3DClass` is visible.

## 6. Configuration

```ini
[display]
render_scale = auto   ; auto | 1 (off, vanilla 3D view) | 2 | 3 | 4
```

Invalid values warn and use the default.

## 7. Error handling ("never worse than vanilla")

| Failure | Response |
|---|---|
| `GAMEF.DLL` SHA-256 is not the GOG build's | Stage B off for the session; logged once |
| Our device or viewport cannot be created | Overlay off for this battle; logged; retried next battle |
| Exception inside our render calls (SEH around the shadow render only) | Overlay off for the session; the access-violation log records it; the game's blit still runs |
| Shadow render slower than 50 ms on average over 2 s | k lowered by one, down to 1 (off); logged |

Logging adds: B1 active/inactive with the reason, device size and k on each (re)creation, and the
shadow render's time in the 10 s performance line.

## 8. Testing

| Layer | Covers | Runs |
|---|---|---|
| `core` | `render_scale` (rounding, clamping, override, the examples of §5.2); `overlay_mask`; config key | CI: Windows + Linux |
| `platform` | `palette_frame`; presenter overlay pass on WARP with exact pixels, including a covered pixel that keeps the captured value | CI: Windows x86 |
| `wing32` | `gamef_layout` accepts only the known hash (fake module) | CI: Windows x86 |
| In game | the spike (§9) and smoke-test items: battle view sharp at 1280×960 and fullscreen, tooltips/menus over the view intact, layout switch, half detail, `render_scale = 1` | each milestone exit |

Red/green TDD for every behaviour (project rule).

## 9. Milestones

- **B1.0 — Spike (evidence first).** A diagnostic build creates our second device at **1×** and
  renders the game's scene. **Exit:** our frame is pixel-identical to the game's after palette
  mapping; then 2× and 3× render without errors; render time measured. Results in
  `docs/re-notes/b1-0-exit.md`; the user decides whether B1.1 goes ahead.
  **Result (2026-10-07):** passed: 2 of 123 432 pixels differ at 1×; 1.5 ms at 2×, 2.8 ms at
  3×. Two corrections for B1.1: the ramp palette is shared (our frame uses the game's palette;
  no own palette callback), and the viewport is configured 4-aligned (`align4(w)·k` wide).
- **B1.1 — Hi-res overlay.** Publishing, the presenter overlay with the mask, `render_scale`, the
  slow-render fallback.
  **Result (2026-10-07):** passed: 2× in a 1280×960 window (1.1–1.7 ms per render), 3× fullscreen
  (2.2–2.8 ms); overlaid UI stays visible; `render_scale = 1` looks like vanilla. A flicker while
  panning (capture one frame behind the published render) was fixed by falling back to the
  previous render. Details in `docs/re-notes/b1-1-exit.md`.
- **B1.2 — Polish.** Layout switch (both layouts), half-detail divisor, performance line, smoke
  test.
- **B2 — Bigger view (own spec).** Builds on our own viewport: larger extents and a larger
  presenter area, after vanilla's wide layout. Open: where the 2D map and panels go, and how clicks
  in the added area reach the game. Unit picking is not traced yet and is B2's first
  investigation.

## 10. Risks

| Risk | Mitigation |
|---|---|
| Two devices share Reality Lab state (ramp colour allocation, caches) and disturb the game's render | B1.0: the game's frame is unchanged; the colour allocation is shared, and our frame uses the game's palette |
| Calling internal functions by address on a different build | SHA-256 gate (§7) |
| Shadow render too slow on weak CPUs | Measured in B1.0; automatic k reduction; `render_scale` key |
| Mask misfires (e.g. palette animation between render and capture) | Falls back to vanilla pixels; visible only as momentary softness |
