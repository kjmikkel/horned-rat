#pragma once

#include "core/geometry.h"

#include <cstdint>
#include <vector>

namespace hr::core {

// Straight-alpha 0xAARRGGBB pixels of a Windows cursor (A2 spec §5.3), from its bitmaps as
// GetIconInfo gives them.
// - `mask`: one byte per pixel, nonzero = bit set; never null. A monochrome cursor
//   (`color` == nullptr) has 2 × size.height rows: the AND mask, then the XOR mask.
// - `color`: size.width × size.height pixels 0xAARRGGBB.
// Monochrome: AND clear → black (XOR clear) or white (XOR set); AND set → transparent (XOR clear),
// or screen inversion (XOR set), drawn opaque white: what inversion looks like on the game's dark
// screens.
// Colour: if any pixel has alpha, the pixels are used as they are. Otherwise the AND mask decides:
// clear → opaque colour; set → transparent where the colour is black, else opaque colour (XOR over
// a dark background).
std::vector<std::uint32_t> cursor_pixels(Size size, const std::uint32_t* color,
                                         const std::uint8_t* mask);

} // namespace hr::core
