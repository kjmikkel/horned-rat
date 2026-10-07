# Manual smoke test

Run before every release and at every milestone exit. Record the date, build (git hash) and
result of each item in the PR or release notes. Never commit game logs or screenshots.

## Setup
1. `. ./tools/vsenv.ps1; cmake --preset x86-release; cmake --build --preset x86-release`
2. `./tools/install.ps1 -GameDir '<GOG game folder>' -SourceDll build/x86-release/bin/wing32.dll`
3. Launch the game from GOG Galaxy.

## Checklist
- [ ] Intro/video plays (or can be skipped) without a crash.
- [ ] Main menu appears; mouse clicks hit the right buttons.
- [ ] Start a new campaign; the campaign map appears and scrolls.
- [ ] Enter the first battle; deploy, move and attack with a unit.
- [ ] A palette effect (fade to/from black between screens) looks correct.
- [ ] Save the game, quit to the menu, load the save.
- [ ] Quit the game normally; no error dialog.
- [ ] `horned-rat.log` exists in `WARFB\` and has no `exception` lines.
- [ ] Uninstall: `./tools/install.ps1 -GameDir '<GOG game folder>' -Uninstall`; the game still starts.

### A2 presenter (from A2.1)
- [ ] The game appears in a "Shadow of the Horned Rat" window; the original game window is not visible.
- [ ] Menu hover and clicks work; the "select mission" text and book buttons react.
- [ ] Typing a save-game name works (Edit control receives keys and shows the caret).
- [ ] Battle: selecting, moving and attacking units with the mouse works; the map scrolls.
- [ ] The intro/briefing videos play inside the presenter.
- [ ] Clicking on the desktop or another window does not click inside the game.
- [ ] Dragging the presenter partly off-screen: the picture keeps updating everywhere.
- [ ] Other windows stay clickable where the hidden game window lies (it is click-through).
- [ ] No flash of the original game window at start-up.
- [ ] Sound effects and speech play (not only music).
- [ ] Game dialogs (e.g. the exit question) appear on top of the presenter and can be answered.
- [ ] Closing the presenter quits the game normally; `WHSHR.EXE` is gone from Task Manager.
- [ ] Panic switch `enabled = false`: the game runs as vanilla (no presenter).
- [ ] `horned-rat.log` shows `presenter: running`, 10-second performance lines, and no `exception`.

The log item applies from A1 on. Later milestones add items here (window resizing, scale modes, Alt+Enter, ...).

### A2 scaling (from A2.2)
- [ ] The monitor keeps its resolution when the game starts (the game's 640x480 switch is deferred).
- [ ] The presenter opens centred at `window_size` (or the largest fit on a small screen).
- [ ] Dragging the window edges scales the picture live; maximize fills the screen.
- [ ] `scale = sharp`: the picture fills the window with crisp pixels.
- [ ] `scale = integer`: whole-number scaling with black bars; every game pixel is a clean block.
- [ ] `scale = nearest-fit`: fills the window; slightly uneven pixels are expected.
- [ ] `aspect = stretch`: the picture fills the whole client area (integer: whole factor per axis).
- [ ] `mode = borderless`: the game starts covering the monitor.
- [ ] Alt+Enter toggles windowed ↔ borderless and returns to the previous window size and position,
      also when the window was maximized before.
- [ ] Clicks hit precisely at large sizes, including small buttons at the picture's edges.
- [ ] Quitting with the window's close button or Alt+F4 ends `WHSHR.EXE` (as does the curtain).
- [ ] With Windows display scaling at 125 % or 150 % (if available), the picture stays crisp.
- [ ] Carried from A2.1: typing a save-game name; battle mouse control and map scrolling;
      briefing videos; clicks on the desktop do not reach the game; dragging the presenter partly
      off-screen keeps it updating; other windows stay clickable over the hidden game window.

### A2 cursor (from A2.3)
- [ ] `cursor = scaled`: the game's own cursor is drawn enlarged with the picture (2× at 1280×960),
      and clicks land on its hotspot.
- [ ] `cursor = native`: the game's cursor is shown at normal size by Windows.
- [ ] Books menu: the game's cursor appears (not the OS arrow), and hovering a special item shows
      its subtitle.
- [ ] The loading cursor appears while the game loads and goes away afterwards.
- [ ] Battle: the cursor changes shape over units and terrain as in vanilla.
- [ ] Over the black bars the game's cursor stays (`scaled`: drawn at the picture's edge;
      `native`: shown where the mouse is); over the title bar, other windows and the desktop the
      normal Windows cursor is shown.

### A2 background (from A2.4)
- [ ] The game has one taskbar entry, and Alt+Tab shows no second, smaller copy of the picture.
- [ ] `background = run`: alt-tab away in a battle; the game keeps playing (music, units move) and
      the picture is right on return.
- [ ] `background = pause`: alt-tab away in a battle, the books menu and a briefing video. While
      away the game is frozen and silent (effects, music, video sound); on return it carries on
      where it stopped, without jumping ahead, and video and sound stay in sync.
- [ ] `background = pause`: stay away for 20 s or more; no small copy of the window appears at the
      top left of a monitor.
- [ ] Either mode: the game's own exit question (quitting through the curtain) stays in front and
      can be answered, also after clicking another window and back.
- [ ] Second monitor (if available): the presenter on the second monitor, the focus on the first;
      the game behaves as its `background` mode says.
- [ ] `horned-rat.log`: no `access violation` lines from `WING32.dll` itself; the 10 s lines show
      `slowest` below ~35 ms in normal play.

### Stage B1 sharp 3D view (from B1.1)
- [ ] `render_scale = auto` at 1280×960: the battle 3D view is visibly sharper than the rest of the
      picture (smooth unit outlines and edges); the log shows `3D view x2`.
- [ ] Fullscreen at 1920×1080 (`scale = sharp`): `3D view x3`; still sharp; no stutter.
- [ ] Whatever the game draws over the 3D view (unit info, menus, dialogs, the exit question) is
      shown normally, not hidden behind the sharp view.
- [ ] Camera movement and unit movement stay smooth; the sharp view is never behind the rest of
      the picture, and the landscape does not flicker while the camera pans.
- [ ] `render_scale = 1`: the 3D view looks exactly like vanilla; the log has no `b1:` lines.
- [ ] `horned-rat.log`: `b1: known GAMEF.DLL build`, `b1: device …`, no `b1: off` line.
