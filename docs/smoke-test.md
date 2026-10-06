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
