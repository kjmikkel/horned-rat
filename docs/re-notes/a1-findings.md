# A1 findings: how the game draws, creates windows and reads the mouse

- **Date:** 2026-10-05
- **Build:** horned-rat 0.1.0 (commit `77eccf8`), instrumented release build, GOG game build per spec §8
- **Session:** ~252 s — intro, main menu, new campaign, campaign map, first battle (deploy, move,
  attack), save, quit to menu, load (second battle start), quit. No visible difference from
  vanilla; no `exception`/`fatal` lines in the log.
- Numbers below come from our own hook log (sampled: first 50 calls per function and DC kind,
  then every 1000th). The log itself is not committed.

## 1. Window-DC drawing — the spec's main assumption does not hold

Spec §5.1 assumed every frame reaches the screen through WinG blits into one main window. In
reality **almost all drawing goes straight to window DCs of many different child windows**:

| API | From | Target window classes | Volume |
|---|---|---|---|
| `WinGBitBlt` | `WHSHR.EXE` | `GlueWindow` (menus, 640×480 full screens), `BitmapScrn`, `EditScroll` | ≥ 5 000 calls |
| `WinGBitBlt` | `GAMEF.DLL` | `Map3DClass` (battle 3D view, **417×296**), `Map2DVMapClass` (184×216), `IconButton5` | ≥ 4 000 calls |
| `StretchDIBits` (8 bpp, 1:1, bottom-up) | `GAMEF.DLL` | `Map2DClass`, `WarHammerClass` (thin frame/border strips) | ~24 calls |
| `TextOutA` | `WHSHR.EXE` | `GlueWindow`, `ATGMovieIntro` | ≥ 6 000 calls to window DCs |
| `TextOutA` | `WHSHR.EXE`, `GAMEF.DLL` | memory DCs | ≥ 4 000 calls |
| `BitBlt` | `OMNI000R.DLL` (video player) | `ATGMovie` (640-wide video strip) | 15 sampled |

Never called during the session: `PatBlt`, `SetDIBitsToDevice`, `SetCursorPos`, `ClipCursor`.

In addition, the UI uses **standard USER32 controls** (`Button`, `Edit`) created as children.
Windows paints those itself, inside USER32, so no game-module hook can ever see their drawing.

## 2. Main window

- The first top-level `CreateWindowExA` is the main window: class `Glue`, style
  `WS_OVERLAPPEDWINDOW`, created by `WHSHR.EXE`. Spec §5.3's rule holds.
- `WHSHR.EXE` then moves it to (0,0) with a 640×480 size via `SetWindowPos` (`SWP_FRAMECHANGED`).
  This is the "tiny window".
- All content lives in a **tree of child windows**: `BitmapScrn` (640×480 at 0,0) is the root
  content child; `GlueWindow` screens, `MissionWindow`, `Button`, `Edit`, `EditScroll`, the
  movie windows (`ATGMovie*`) and, in battle, `WarHammerClass`, `Map3DClass`, `Map2DClass`,
  `Map2DVMapClass` and `IconButton5` are nested below it.
- `GAMEF.DLL` repositions child windows often (`SetWindowPos`, 47 sampled calls, move-only).

## 3. Mouse

- `GetCursorPos` from `WHSHR.EXE` is polled constantly (millions of calls in four minutes),
  always followed by `ScreenToClient` against the relevant child window. `ClientToScreen` is
  also used. Coordinates are plain screen coordinates of the 640×480 window at (0,0).
- No `SetCursorPos` or `ClipCursor`.
- Window messages were not instrumented in A1, but the standard `Button`/`Edit` controls
  necessarily rely on real mouse messages delivered to the right child window.

## 4. Reality Lab and video

- The battle 3D renderer is loaded per battle: `RLDDF.DLL` loads `rl\softf.dll`, which loads
  `rl\soft\rlfrm256.dll`, and the game creates a **memory driver**
  (`RLDDIMemoryCreateDriver2`, ramp/ramp-lighting drivers). `softf.dll` was hooked but never
  called `BitBlt` or `SetDIBitsToDevice`: 3D frames are rendered into memory and reach the screen
  via `WinGBitBlt` from `GAMEF.DLL` into the 417×296 `Map3DClass` window.
- Videos are presented by `OMNI000R.DLL` with plain GDI `BitBlt` from a memory DC into the
  `ATGMovie` child window.

## 5. Dynamic loading and `GetProcAddress`

- Loaded at run time: the resource DLLs in `FILE\dll\` (repeatedly), `AWEMAN32.DLL`,
  `rl\softf.dll` + `rl\soft\rlfrm256.dll` (per battle) and the mission script DLL
  `FILE\script\bfNNN.DLL` (per battle; exports a script pointer and an instruction count).
  The `LoadLibraryA` hook re-applied our hooks to each new module as designed.
- The only GDI/USER32 function resolved via `GetProcAddress` is
  **`USER32!ChangeDisplaySettingsA`** (by `WHSHR.EXE`). It bypasses IAT hooks; any future handling
  of display-mode changes must go through the `GetProcAddress` hook.
- No GDI drawing function is resolved dynamically, so IAT hooks see all game-module drawing.

## Consequences for A2

1. **Capture-by-blit (spec §5.1) cannot work as specified.** The screen is a composition of a
   dozen child windows drawn by WinG, GDI blits, GDI text and USER32's own control painting.
   Copying blit rectangles into one `ShadowFrame` would need per-window offset mapping *and*
   would still miss `Button`/`Edit` painting and direct text.
2. **The capture target should be the composed window, not individual calls.** Options for the
   A2 design (to be decided in A2 brainstorming, ideally after a short spike):
   - **a)** Let Windows compose the game's 640×480 window tree as today, grab the composed
     pixels each frame (e.g. `PrintWindow(PW_RENDERFULLCONTENT)`, window-DC `BitBlt`, or
     Windows.Graphics.Capture) and present them scaled with our D3D11 presenter in our own window;
     forward input to the real window tree.
   - **b)** Rely on Windows' own DPI virtualisation / GDI scaling of a DPI-unaware window, which
     scales the whole window tree *and* maps input, at the cost of fixed scale factors and
     bilinear filtering.
   - **c)** Keep blit capture but redirect child-window drawing into an off-screen composition
     surface (heavier; touches many APIs and USER32 control painting).
3. **Input** must reach the correct child window, because the standard controls need genuine
   messages. Translating only `GetCursorPos`/`ScreenToClient` (spec §5.4) is not sufficient
   on its own if the game window is not where the user clicks.
4. **Palette tracking (spec §5.2) may be unnecessary** if we capture the composed 32-bit output:
   GDI already converts the 8-bit DIBs. Open question: do the game's `AnimatePalette` fades still
   show on a 32-bit desktop today? (Not instrumented in A1; check during the A2 spike.)
5. **Stage B data point:** the battle 3D view is only **417×296** pixels, rendered by Reality
   Lab's memory driver into a buffer we can see being blitted. Rendering it larger is a
   Reality-Lab/`GAMEF.DLL` change, separate from the UI windows.
