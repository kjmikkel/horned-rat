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

// SetCursor: the game's current cursor, shown by the presenter.
void set_game_cursor(HCURSOR cursor);

// The presenter window while active (owner for the game's message boxes), else nullptr.
HWND presenter_window();

// Diagnostics: logs when the physical state or the masking of a mouse button changes.
void note_button(int vk, bool physically_down, bool masked);

} // namespace hr::wing32::a2
