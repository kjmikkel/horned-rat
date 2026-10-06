#include "platform/cursor_image.h"

#include "core/cursor_bitmap.h"

#include <cstddef>

namespace hr::platform {
namespace {

// GetIconInfo hands out copies of the cursor's bitmaps; they must be deleted.
struct IconBitmaps {
    ICONINFO info{};
    ~IconBitmaps() {
        if (info.hbmMask != nullptr) {
            DeleteObject(info.hbmMask);
        }
        if (info.hbmColor != nullptr) {
            DeleteObject(info.hbmColor);
        }
    }
};

BITMAPINFOHEADER top_down(int width, int height, WORD bits) {
    BITMAPINFOHEADER header{};
    header.biSize = sizeof header;
    header.biWidth = width;
    header.biHeight = -height;
    header.biPlanes = 1;
    header.biBitCount = bits;
    header.biCompression = BI_RGB;
    return header;
}

// A 1-bit bitmap as one byte per pixel, 1 = bit set (white in its colour table).
std::vector<std::uint8_t> read_mask(HDC dc, HBITMAP bitmap, int width, int height) {
    struct {
        BITMAPINFOHEADER header;
        RGBQUAD colors[2];
    } info{top_down(width, height, 1), {}};
    const int stride = (width + 31) / 32 * 4;
    std::vector<std::uint8_t> bits(static_cast<std::size_t>(stride) * height);
    if (GetDIBits(dc, bitmap, 0, static_cast<UINT>(height), bits.data(),
                  reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS) != height) {
        return {};
    }
    const bool inverted = info.colors[0].rgbRed != 0; // index 0 is white
    std::vector<std::uint8_t> mask(static_cast<std::size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            const bool bit =
                ((bits[static_cast<std::size_t>(y) * stride + x / 8] >> (7 - x % 8)) & 1) != 0;
            mask[static_cast<std::size_t>(y) * width + x] = bit != inverted ? 1 : 0;
        }
    }
    return mask;
}

std::vector<std::uint32_t> read_color(HDC dc, HBITMAP bitmap, int width, int height) {
    BITMAPINFO info{};
    info.bmiHeader = top_down(width, height, 32);
    std::vector<std::uint32_t> pixels(static_cast<std::size_t>(width) * height);
    if (GetDIBits(dc, bitmap, 0, static_cast<UINT>(height), pixels.data(), &info, DIB_RGB_COLORS) !=
        height) {
        return {};
    }
    return pixels;
}

} // namespace

std::optional<CursorImage> cursor_image(HCURSOR cursor) {
    IconBitmaps bitmaps;
    if (cursor == nullptr || !GetIconInfo(cursor, &bitmaps.info) ||
        bitmaps.info.hbmMask == nullptr) {
        return std::nullopt;
    }
    BITMAP mask{};
    if (GetObjectA(bitmaps.info.hbmMask, sizeof mask, &mask) == 0) {
        return std::nullopt;
    }
    const bool mono = bitmaps.info.hbmColor == nullptr;
    const int width = mask.bmWidth;
    const int height = mono ? mask.bmHeight / 2 : mask.bmHeight;
    if (width <= 0 || height <= 0) {
        return std::nullopt;
    }
    const HDC dc = GetDC(nullptr);
    const auto mask_bits = read_mask(dc, bitmaps.info.hbmMask, width, mask.bmHeight);
    const auto color =
        mono ? std::vector<std::uint32_t>() : read_color(dc, bitmaps.info.hbmColor, width, height);
    ReleaseDC(nullptr, dc);
    if (mask_bits.empty() || (!mono && color.empty())) {
        return std::nullopt;
    }
    CursorImage image;
    image.size = {width, height};
    image.hotspot = {static_cast<int>(bitmaps.info.xHotspot),
                     static_cast<int>(bitmaps.info.yHotspot)};
    image.pixels = core::cursor_pixels(image.size, mono ? nullptr : color.data(), mask_bits.data());
    return image;
}

} // namespace hr::platform
