#include "core/indexed_frame.h"

#include <cstddef>

namespace hr::core {

int count_differing_pixels(const IndexedFrame& a, const IndexedFrame& b, int width, int height) {
    int differing = 0;
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* ra = a.pixels + static_cast<std::ptrdiff_t>(y) * a.stride;
        const std::uint8_t* rb = b.pixels + static_cast<std::ptrdiff_t>(y) * b.stride;
        for (int x = 0; x < width; ++x) {
            if ((*a.palette)[ra[x]] != (*b.palette)[rb[x]]) {
                ++differing;
            }
        }
    }
    return differing;
}

} // namespace hr::core
