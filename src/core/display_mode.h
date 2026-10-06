#pragma once

#include <cstdint>

namespace hr::core {

// ChangeDisplaySettings flags (checked against <windows.h> in the wing32 hooks).
inline constexpr std::uint32_t kCdsTest = 0x00000002;
inline constexpr std::uint32_t kCdsFullscreen = 0x00000004;

// The game switches the monitor to 640x480 at start-up (A2.2 finding). While the presenter owns
// the display that is pointless and shrinks every window, so such calls are recorded instead of
// applied, and replayed if the presenter gives up. CDS_TEST calls change nothing and pass.
bool defer_display_change(bool presenter_owns_display, std::uint32_t flags);

} // namespace hr::core