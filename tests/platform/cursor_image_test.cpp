#include "platform/cursor_image.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iterator>
#include <vector>

using hr::platform::cursor_image;

TEST_CASE("a monochrome cursor is read with its size, hotspot and pixels", "[cursor_image]") {
    // 8x2 cursor: AND rows 0x00, 0xFF; XOR rows 0xF0, 0xF0 (1-bit rows padded to 16 bits).
    const BYTE bits[] = {0x00, 0, 0xFF, 0, 0xF0, 0, 0xF0, 0};
    const HBITMAP mask = CreateBitmap(8, 4, 1, 1, bits);
    ICONINFO info{FALSE, 3, 1, mask, nullptr};
    const HCURSOR cursor = CreateIconIndirect(&info);
    DeleteObject(mask);
    REQUIRE(cursor != nullptr);

    const auto image = cursor_image(cursor);
    DestroyCursor(cursor);
    REQUIRE(image);
    CHECK(image->size == hr::core::Size{8, 2});
    CHECK(image->hotspot == hr::core::Point{3, 1});
    constexpr std::uint32_t W = 0xFFFFFFFFu, B = 0xFF000000u, T = 0u;
    CHECK(image->pixels ==
          std::vector<std::uint32_t>{W, W, W, W, B, B, B, B, W, W, W, W, T, T, T, T});
}

TEST_CASE("a 32-bit cursor keeps its alpha channel", "[cursor_image]") {
    BITMAPINFO bi{};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 2;
    bi.bmiHeader.biHeight = -2; // top-down
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    const HBITMAP color = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    REQUIRE(color != nullptr);
    // Only fully opaque or fully transparent (black) pixels: the result does not depend on
    // whether Windows stores the colours premultiplied.
    const std::uint32_t pixels[] = {0xFFFF0000u, 0x00000000u, 0xFF00FF00u, 0xFF0000FFu};
    std::memcpy(bits, pixels, sizeof pixels);
    const BYTE mask_bits[] = {0, 0, 0, 0};
    const HBITMAP mask = CreateBitmap(2, 2, 1, 1, mask_bits);
    ICONINFO info{FALSE, 0, 0, mask, color};
    const HCURSOR cursor = CreateIconIndirect(&info);
    DeleteObject(mask);
    DeleteObject(color);
    REQUIRE(cursor != nullptr);

    const auto image = cursor_image(cursor);
    DestroyCursor(cursor);
    REQUIRE(image);
    CHECK(image->size == hr::core::Size{2, 2});
    CHECK(image->pixels == std::vector<std::uint32_t>(std::begin(pixels), std::end(pixels)));
}

TEST_CASE("the system arrow can be read", "[cursor_image]") {
    const auto image = cursor_image(LoadCursorA(nullptr, MAKEINTRESOURCEA(32512)));
    REQUIRE(image);
    REQUIRE(image->size.width > 0);
    REQUIRE(image->size.height > 0);
    CHECK(image->pixels.size() ==
          static_cast<size_t>(image->size.width) * static_cast<size_t>(image->size.height));
    const auto opaque = [](std::uint32_t p) { return (p >> 24) == 0xFF; };
    const auto clear = [](std::uint32_t p) { return (p >> 24) == 0; };
    CHECK(std::any_of(image->pixels.begin(), image->pixels.end(), opaque));
    CHECK(std::any_of(image->pixels.begin(), image->pixels.end(), clear));
}

TEST_CASE("no cursor gives no image", "[cursor_image]") { CHECK_FALSE(cursor_image(nullptr)); }
