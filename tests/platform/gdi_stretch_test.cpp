#include "core/overlay.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <cstdint>
#include <vector>

// The low-detail 3D view is stretched by GDI (WinGStretchBlt -> StretchBlt). The B1 mask needs
// the same pixels, so core::stretch_indices must map exactly like GDI at the game's sizes.
namespace {

struct Dib {
    HDC dc = CreateCompatibleDC(nullptr);
    HBITMAP bitmap = nullptr;
    std::uint8_t* bits = nullptr;
    Dib(int width, int height) {
        struct {
            BITMAPINFOHEADER header;
            RGBQUAD colours[256];
        } info{};
        info.header.biSize = sizeof info.header;
        info.header.biWidth = width;
        info.header.biHeight = -height;
        info.header.biPlanes = 1;
        info.header.biBitCount = 8;
        info.header.biClrUsed = 256;
        for (int i = 0; i < 256; ++i) { // distinct colours, so GDI keeps every index
            info.colours[i] = {static_cast<BYTE>(i), static_cast<BYTE>(255 - i),
                               static_cast<BYTE>(i * 7), 0};
        }
        void* raw = nullptr;
        bitmap = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&info), DIB_RGB_COLORS, &raw,
                                  nullptr, 0);
        bits = static_cast<std::uint8_t*>(raw);
        SelectObject(dc, bitmap);
    }
    ~Dib() {
        DeleteDC(dc);
        DeleteObject(bitmap);
    }
    Dib(const Dib&) = delete;
    Dib& operator=(const Dib&) = delete;
};

void check_like_gdi(int src_w, int src_h, int dst_w, int dst_h) {
    constexpr int stride = 624; // the game's bitmap width; a multiple of 4, as DIB rows are
    Dib src(stride, src_h);
    Dib dst(stride, dst_h);
    REQUIRE(src.bits != nullptr);
    REQUIRE(dst.bits != nullptr);
    for (int y = 0; y < src_h; ++y) {
        for (int x = 0; x < stride; ++x) {
            src.bits[y * stride + x] = static_cast<std::uint8_t>((x + y * 3) % 251);
        }
    }
    REQUIRE(StretchBlt(dst.dc, 0, 0, dst_w, dst_h, src.dc, 0, 0, src_w, src_h, SRCCOPY));
    GdiFlush();
    const auto ours = hr::core::stretch_indices(src.bits, stride, {src_w, src_h}, {dst_w, dst_h});
    int differ = 0;
    for (int y = 0; y < dst_h; ++y) {
        for (int x = 0; x < dst_w; ++x) {
            differ += ours[y * dst_w + x] != dst.bits[y * stride + x] ? 1 : 0;
        }
    }
    CHECK(differ == 0);
}

} // namespace

TEST_CASE("stretch_indices matches GDI for the narrow low-detail view (208x148 -> 417x296)",
          "[gdi_stretch]") {
    check_like_gdi(208, 148, 417, 296);
}

TEST_CASE("stretch_indices matches GDI for the wide low-detail view (312x148 -> 624x296)",
          "[gdi_stretch]") {
    check_like_gdi(312, 148, 624, 296);
}
