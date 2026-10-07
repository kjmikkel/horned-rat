#pragma once

#include "core/geometry.h"
#include "core/overlay.h"

#include <cstdint>
#include <vector>

namespace hr::core {

// One second render of the battle's 3D view (spec 2026-10-07 §5.1), handed from the game thread
// to the presenter.
struct ShadowFrame {
    std::vector<std::uint8_t> hires; // factor× palette indices, rows of `stride` bytes
    int stride = 0;
    Size size{}; // the view's 1× size (the visible picture is size·factor)
    int factor = 1;
    std::vector<std::uint8_t> game; // the game's own 1× indices, rows of size.width
    Palette palette{};              // the game's palette (shared by both renders)
    std::uintptr_t view_window = 0; // the Map3DClass window
    double time_s = 0.0;            // steady clock, seconds
};

} // namespace hr::core
