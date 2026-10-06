#pragma once

#include <windows.h>

// A2 presenter (spec 2026-10-05-stage-a2-presenter-design.md). Only ever active inside WHSHR.EXE:
// everything is triggered from game-module hooks.
namespace hr::wing32::a2 {

// After every successful CreateWindowExA in a game module. Starts the presenter thread for the
// first top-level window (the game's main window, A1 finding).
void on_window_created(HWND window, HWND parent, DWORD style);

// True while the presenter shows the game and its main window is hidden.
bool active();

// GetCursorPos replacement while active: the virtual cursor in the hidden window's screen space.
void fake_cursor_pos(POINT& out);

// GetAsyncKeyState while active: true if a mouse button must be reported as "up".
bool mask_button(int vk);

// SetCursor: records the game's cursor and counts the call (a forwarded WM_SETCURSOR uses the
// count to tell whether the game chose a cursor itself).
void set_game_cursor(HCURSOR cursor);

// The presenter window while active (owner for the game's message boxes), else nullptr.
HWND presenter_window();

// True from the game's main window creation until the presenter gives up: the presenter owns
// the display, so the game's display-mode changes are deferred (core::defer_display_change).
bool owns_display();

// Records a deferred display-mode request (nullptr = restore the registry mode). If the presenter
// gives up, the last one is applied so the game gets the screen it asked for.
void defer_display_mode(const DEVMODEA* mode, DWORD flags);

// Diagnostics: logs when the physical state or the masking of a mouse button changes.
void note_button(int vk, bool physically_down, bool masked);

} // namespace hr::wing32::a2
