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

Later milestones add items here (window resizing, scale modes, Alt+Enter, ...).
