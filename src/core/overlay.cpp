#include "core/overlay.h"

#include <cstddef>

namespace hr::core {

std::vector<std::uint32_t> to_bgra(const std::uint8_t* pixels, int stride, Size size,
                                   const Palette& palette) {
    std::vector<std::uint32_t> out(static_cast<std::size_t>(size.width) * size.height);
    for (int y = 0; y < size.height; ++y) {
        const std::uint8_t* row = pixels + static_cast<std::ptrdiff_t>(y) * stride;
        for (int x = 0; x < size.width; ++x) {
            out[static_cast<std::size_t>(y) * size.width + x] = palette[row[x]];
        }
    }
    return out;
}

std::vector<std::uint8_t> overlay_mask(const std::uint32_t* captured, Size captured_size, Rect view,
                                       const std::vector<std::uint32_t>& game) {
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(view.width) * view.height, 0);
    for (int y = 0; y < view.height; ++y) {
        const int cy = view.y + y;
        if (cy < 0 || cy >= captured_size.height) {
            continue;
        }
        for (int x = 0; x < view.width; ++x) {
            const int cx = view.x + x;
            if (cx < 0 || cx >= captured_size.width) {
                continue;
            }
            const std::size_t i = static_cast<std::size_t>(y) * view.width + x;
            const std::uint32_t seen =
                captured[static_cast<std::size_t>(cy) * captured_size.width + cx] & 0xFFFFFFu;
            mask[i] = seen == (game[i] & 0xFFFFFFu) ? 1 : 0;
        }
    }
    return mask;
}

std::vector<std::uint32_t> compose_overlay(const std::uint8_t* hires, int stride, Size size,
                                           int factor, const Palette& palette,
                                           const std::vector<std::uint8_t>& mask) {
    const int width = size.width * factor;
    const int height = size.height * factor;
    std::vector<std::uint32_t> out(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        const std::uint8_t* row = hires + static_cast<std::ptrdiff_t>(y) * stride;
        const std::uint8_t* mask_row =
            mask.data() + static_cast<std::size_t>(y / factor) * size.width;
        for (int x = 0; x < width; ++x) {
            const std::uint32_t alpha = mask_row[x / factor] != 0 ? 0xFF000000u : 0u;
            out[static_cast<std::size_t>(y) * width + x] = alpha | (palette[row[x]] & 0xFFFFFFu);
        }
    }
    return out;
}

} // namespace hr::core
