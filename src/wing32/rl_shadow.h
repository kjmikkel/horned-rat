#pragma once

#include <windows.h>

#include <string>

// Stage B1.0 spike (spec 2026-10-07 §9): with [debug] b1_spike = k, renders the battle's 3D scene
// a second time through our own Reality Lab device at k× when the game blits its 3D view, compares
// k = 1 with the game's frame, logs the cost and writes one BMP of a k× frame. Game thread only.
namespace hr::wing32::rl_shadow {

// After every successful CreateWindowExA in a game module: remembers the Map3DClass window and
// subclasses it so our device is destroyed before the game's own WM_DESTROY handler runs.
void on_window_created(HWND window, const std::string& class_name);

// From WinGBitBlt / WinGStretchBlt before the original: renders the shadow frame when `dest` is
// the Map3DClass window's DC.
void before_blit(HDC dest, HDC src);

} // namespace hr::wing32::rl_shadow
