#pragma once

#include "core/geometry.h"

#include <array>
#include <cstdint>
#include <vector>

namespace hr::core {

// 256 colours as 0x00RRGGBB.
using Palette = std::array<std::uint32_t, 256>;

// 8-bit pixels (rows of `stride` bytes) as 0x00RRGGBB, rows of size.width.
std::vector<std::uint32_t> to_bgra(const std::uint8_t* pixels, int stride, Size size,
                                   const Palette& palette);

// Stage B1 (spec 2026-10-07 §5.3): per pixel of the 3D view, 1 where the captured picture shows
// the game's own 1× render (`game`, view.width x view.height, 0x00RRGGBB), else 0. `view` is
// where the 3D view lies in the capture; parts outside the capture count as covered. Alpha bytes
// of the capture are ignored.
std::vector<std::uint8_t> overlay_mask(const std::uint32_t* captured, Size captured_size, Rect view,
                                       const std::vector<std::uint32_t>& game);

// How many pixels of a mask are set.
int mask_coverage(const std::vector<std::uint8_t>& mask);

// True if the capture shows the previous render rather than the newest: the newest mask covers
// less than half the view and the previous one (same size) covers more. The game publishes a
// render just before it blits it, so a capture can still show the one before.
bool prefer_previous(const std::vector<std::uint8_t>& current,
                     const std::vector<std::uint8_t>& previous);

// Our factor× render (rows of `stride` bytes; size is the 1× size) as straight-alpha 0xAARRGGBB
// rows of size.width·factor: opaque where the 1× mask is set, alpha 0 (colour kept, so filtering
// at mask edges does not darken) elsewhere.
std::vector<std::uint32_t> compose_overlay(const std::uint8_t* hires, int stride, Size size,
                                           int factor, const Palette& palette,
                                           const std::vector<std::uint8_t>& mask);

} // namespace hr::core
