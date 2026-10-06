#include "core/cursor_bitmap.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using namespace hr::core;

TEST_CASE("monochrome cursors: black, white, transparent, and inverted drawn white",
          "[cursor_bitmap]") {
    // 4x1: AND row 0 0 1 1, then XOR row 0 1 0 1.
    const std::uint8_t mask[] = {0, 0, 1, 1, 0, 1, 0, 1};
    CHECK(cursor_pixels({4, 1}, nullptr, mask) ==
          std::vector<std::uint32_t>{0xFF000000u, 0xFFFFFFFFu, 0u, 0xFFFFFFFFu});
}

TEST_CASE("colour cursors with alpha keep their pixels", "[cursor_bitmap]") {
    const std::uint32_t color[] = {0x80FF0000u, 0x00000000u, 0xFF00FF00u};
    const std::uint8_t mask[] = {1, 1, 1};
    CHECK(cursor_pixels({3, 1}, color, mask) ==
          std::vector<std::uint32_t>{0x80FF0000u, 0u, 0xFF00FF00u});
}

TEST_CASE("colour cursors without alpha take it from the AND mask", "[cursor_bitmap]") {
    // clear: opaque colour (also black); set: transparent if black, else opaque (XOR on dark)
    const std::uint32_t color[] = {0x00FF0000u, 0x00000000u, 0x00000000u, 0x0000FF00u};
    const std::uint8_t mask[] = {0, 0, 1, 1};
    CHECK(cursor_pixels({2, 2}, color, mask) ==
          std::vector<std::uint32_t>{0xFFFF0000u, 0xFF000000u, 0u, 0xFF00FF00u});
}

TEST_CASE("an empty cursor has no pixels", "[cursor_bitmap]") {
    const std::uint8_t mask[] = {0};
    CHECK(cursor_pixels({0, 0}, nullptr, mask).empty());
}
