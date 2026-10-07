#pragma once

#include "core/shadow_frame.h"

#include <windows.h>

#include <cstdint>
#include <string>

// Stage B1 (spec 2026-10-07 §5.1): when the game blits its battle 3D view, renders the same scene
// a second time through our own Reality Lab device at the factor the presenter asks for, and
// publishes the frame for the presenter's overlay.
namespace hr::wing32::rl_shadow {

// Game thread, after every successful CreateWindowExA in a game module: remembers the
// Map3DClass window and subclasses it so our device goes before the game's own WM_DESTROY handler.
void on_window_created(HWND window, const std::string& class_name);

// Game thread, from WinGBitBlt / WinGStretchBlt before the original.
void before_blit(HDC dest, HDC src);

// Presenter thread: the factor to render at (1 or less = do not render).
void set_factor(int factor);

// Presenter thread: copies the newest frame into `out` if newer than `seen`.
bool take_frame(std::uint64_t& seen, core::ShadowFrame& out);

// Presenter thread: render times since the last call (for the 10 s performance line).
struct Stats {
    int frames = 0;
    double total_ms = 0.0;
    double max_ms = 0.0;
    int factor = 0;
};
Stats take_stats();

// Steady clock in seconds; ShadowFrame::time_s uses it.
double now_s();

} // namespace hr::wing32::rl_shadow
