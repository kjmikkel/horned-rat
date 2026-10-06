#pragma once

#include "core/geometry.h"

#include <windows.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace hr::platform {

struct CursorImage {
    core::Size size;
    core::Point hotspot;
    std::vector<std::uint32_t> pixels; // straight-alpha 0xAARRGGBB (core::cursor_pixels)
};

// The picture of `cursor` (GetIconInfo), or nullopt if it cannot be read.
std::optional<CursorImage> cursor_image(HCURSOR cursor);

} // namespace hr::platform
