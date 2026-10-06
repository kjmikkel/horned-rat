#pragma once

#include <cstdint>

namespace hr::core {

// DirectSound buffer flags mirrored so core stays free of Windows headers (checked against
// <dsound.h> in wing32/hooks.cpp).
inline constexpr std::uint32_t kDsbcapsPrimaryBuffer = 0x00000001;
inline constexpr std::uint32_t kDsbcapsGlobalFocus = 0x00008000;

// The game ties DirectSound to its own (hidden) main window, so without global focus its effects
// and speech are muted whenever our presenter is active (A2.1 smoke test). The primary buffer must
// not get the flag.
std::uint32_t with_global_focus(std::uint32_t buffer_flags);

} // namespace hr::core
