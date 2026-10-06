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

### Run B

- **Run:** 2026-10-06, build `d80600e` (`WM_SETCURSOR` forwarding), `message_census = true`.
- **What the user saw:** everything works, also after completing the first mission:
  - the cursor changes over the books;
  - item tooltips and the man's remark are back;
  - the loading icon goes away after loading.
- **Log:**
  - `WM_SETCURSOR` now reaches every game window class. Some counts: `Glue` 5225, `BitmapScrn` 5225, `GlueWindow` 1108, `Button` 123, and the battle classes (`WarHammerClass` 4136, `Map2DClass` 1413, `CPClass` 1786, …).
  - The presenter's cursor changes come from class cursors: the sword from `GlueWindow`, `BitmapScrn`, `TroopWindow` and the movie classes; the hand from `Button` and `EditScroll`.
  - No `exception` lines.
  - Performance is unchanged: 601 frames / 10 s, capture ~5.5 ms, present ~11 ms.
- **Conclusion:** forwarding `WM_SETCURSOR` fixes the cursor, the stuck wait icon, the hover tooltips and the man's remark. The game drives all of them from that message.

## 2. Smoke test

- **Builds:** `debe339` (scaled cursor), then `812620a` (typing fix).
- **Setup:** hardware D3D11, 1280×960 window, `scale = sharp`.

| Item | Result |
|---|---|
| `cursor = scaled`: game cursor drawn enlarged (2×), clicks on its hotspot | Pass |
| `cursor = native`: game cursor at normal size by Windows | Pass |
| Books menu: game cursor, hand over the books, hover tooltips, the man's remark | Pass (both modes) |
| Loading cursor appears and goes away | Pass |
| Battle: cursor changes over units and terrain | Pass |
| Over the bars / title bar / desktop | Pass: the game's cursor over the bars, the Windows arrow over the title bar and desktop |
| Typing a save-game name | **Fail, fixed (§4):** every character was doubled. After the fix: pass, including Shift and Backspace |
| æ, ø, å in a save name | Not typed (accepted; see §5) |
| Briefing videos | Pass |
| Dragging the presenter partly off-screen | Pass |
| Clicking the desktop over the hidden game window | Pass |
| Maximize → Alt+Enter → Alt+Enter back to maximized | Pass |
| `aspect = stretch` | Not tested |

## 3. Performance

With a cursor sprite drawn: 601–602 frames per 10 s, capture 1.5–2.4 ms, present 13.8–14.8 ms. This is unchanged from A2.2; the sprite pass costs nothing measurable.

## 4. Bugs found and fixed

1. **Cursor, hover tooltips, the man's remark, stuck wait cursor** (the A2.2 known issue): `WM_SETCURSOR` never reached the game (§1). Fix: the presenter forwards it asynchronously to the game child under the virtual cursor before each forwarded mouse message. It then takes the game's `SetCursor` choice or the hit window's class cursor.
2. **Every typed character doubled** (carried A2.1 item, first tested here).
   - Cause: the presenter forwards `WM_CHAR` made from the real keyboard state, and the game's own `TranslateMessage` (imported by `WHSHR.EXE`) turned the forwarded `WM_KEYDOWN` into a second `WM_CHAR`. The census showed exactly 2 `WM_CHAR` per `WM_KEYDOWN` on the `Edit` control.
   - Fix: a `TranslateMessage` hook skips key-down translation while the presenter runs (`core::skip_translate`).
   - After the fix the census shows at most one `WM_CHAR` per key press.

## 5. Open items and observations

- When the game closes, the small Windows-drawn cursor shows for a split second. Not worth fixing (user).
- æ, ø, å cannot be typed in a save name. Accepted by the user; probably the game's own text handling, not checked against vanilla.
- `aspect = stretch` and display scaling above 100 % are still untested.
