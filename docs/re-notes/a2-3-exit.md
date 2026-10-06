# A2.3 exit: cursor

## 1. Investigation (books-menu cursor and hover subtitles)

### Run A

- **Run:** 2026-10-06, build `e7cbc7b`, release build, `message_census = true`, hardware D3D11.
- **What the user saw:**
  - In the books menu the cursor is the game's black-and-white sword at first, but it does not change over the books.
  - Items with tooltips show none.
  - Clicking the man in the middle no longer shows his remark (something like "stop touching me").
  - Briefing subtitles play.
  - After the first mission the cursor stays the Windows loading (wait) icon.
- **Cursors loaded:**
  - `WHSHR.EXE` loads its own `SwordCursor` and `HandCursor` at start-up.
  - It loads system `#32514` (`IDC_WAIT`) for loading screens.
  - `GAMEF.DLL` loads system `#32512` (`IDC_ARROW`) and `gmcur.dll` cursors `#101`, `#102` and `#103` when the first mission starts.
- **Class cursors:**
  - `WHSHR.EXE` registers its window classes with a class cursor:
    - the sword for `Glue`, `BitmapScrn`, `GlueWindow`, `TroopWindow`, the movie classes, `MagicBookWindow` and others;
    - the hand for `MissionWindow`, `LoadSaveClass` and `EditScroll`.
  - In the menus it repeatedly calls `SetClassLongA(GCL_HCURSOR)` on the standard `Button` class with the hand cursor. The books and menu items are buttons.
  - `GAMEF.DLL`'s battle classes have no class cursor.
- **SetCursor:** only `WHSHR.EXE` calls it, in pairs: `IDC_WAIT` at the start of loading, then the sword.
  - The last call of the run is `IDC_WAIT` at 266.8 s. Under vanilla Windows the next `WM_SETCURSOR` would bring back the class cursor. Without that message the wait icon stays.
  - One call sets a `gmcur.dll` cursor (`0x0C970DC1`).
- **Messages under the presenter:**
  - 2867 census lines. Game windows receive `WM_MOUSEMOVE` and button messages through the presenter's forwarding. That covers `Button`, `GlueWindow`, `IconButton(5)`, the map classes and others.
  - **No game window ever receives `WM_SETCURSOR`.** The only ones in the whole run went to the exit dialog (`#32770`) and its buttons, at 281 s. That dialog is on screen and owned by the presenter.
- **Conclusion:** the A2.2 hypothesis is confirmed.
  - The game shows its cursors through window-class cursors, which Windows applies in `DefWindowProc(WM_SETCURSOR)`. It also resets after its wait cursor through the same message.
  - Under the presenter the message never reaches the game, because Windows sends it only to the window under the real cursor.
  - The hover tooltips and the man's remark also stop. They are most likely triggered from `WM_SETCURSOR` handling too, since the mouse moves do arrive. Run B will show whether forwarding the message brings them back.
