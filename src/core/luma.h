#pragma once

#include <cstdint>

namespace hr::core {

// Mean Rec.601 brightness (0..255) of tightly packed 0x??RRGGBB pixels. Used by the fade check.
double mean_luma(const std::uint32_t* pixels, int width, int height);

} // namespace hr::core
