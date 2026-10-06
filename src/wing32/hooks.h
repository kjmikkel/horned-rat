#pragma once

#include <windows.h>

namespace hr::wing32 {

// Installs or refreshes the A1 logging hooks in every game module (spec §3). Idempotent; no-op
// unless settings().hooks_enabled. Safe under the loader lock.
void install_hooks();

// Called by the WinG blit exports so their calls are logged like the GDI ones.
void log_wing_bit_blt(const void* caller, HDC dest, int x, int y, int width, int height);
void log_wing_stretch_blt(const void* caller, HDC dest, int x, int y, int width, int height);

// background = pause: stops the game's playing sound buffers while it is held, and plays them
// again afterwards (core::SoundPause).
void pause_game_sounds(bool paused);

} // namespace hr::wing32
