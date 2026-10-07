#pragma once

#include <array>
#include <cstdint>

namespace hr::core {

// An 8-bit palettised picture (top-down rows of `stride` bytes); palette entries 0x00RRGGBB.
struct IndexedFrame {
    const std::uint8_t* pixels;
    int stride;
    const std::array<std::uint32_t, 256>* palette;
};

// How many of the top-left width x height pixels show a different colour in `a` and `b`
// (Stage B1.0: our second render against the game's own).
int count_differing_pixels(const IndexedFrame& a, const IndexedFrame& b, int width, int height);

} // namespace hr::core
