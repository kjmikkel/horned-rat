#include "core/cursor_bitmap.h"

#include <algorithm>
#include <cstddef>

namespace hr::core {

std::vector<std::uint32_t> cursor_pixels(Size size, const std::uint32_t* color,
                                         const std::uint8_t* mask) {
    const std::size_t count = static_cast<std::size_t>(std::max(size.width, 0)) *
                              static_cast<std::size_t>(std::max(size.height, 0));
    std::vector<std::uint32_t> out(count, 0u);
    constexpr std::uint32_t kOpaque = 0xFF000000u;
    constexpr std::uint32_t kWhite = 0xFFFFFFFFu;
    if (color == nullptr) {
        const std::uint8_t* const xor_mask = mask + count;
        for (std::size_t i = 0; i < count; ++i) {
            const bool xor_bit = xor_mask[i] != 0;
            if (mask[i] == 0) {
                out[i] = xor_bit ? kWhite : kOpaque;
            } else if (xor_bit) {
                out[i] = kWhite; // screen inversion
            }
        }
        return out;
    }
    const bool has_alpha =
        std::any_of(color, color + count, [](std::uint32_t p) { return (p >> 24) != 0; });
    for (std::size_t i = 0; i < count; ++i) {
        const std::uint32_t rgb = color[i] & 0x00FFFFFFu;
        if (has_alpha) {
            out[i] = color[i];
        } else if (mask[i] == 0 || rgb != 0) {
            out[i] = kOpaque | rgb;
        }
    }
    return out;
}

} // namespace hr::core
