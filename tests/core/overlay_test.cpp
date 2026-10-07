#include "core/overlay.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using hr::core::compose_overlay;
using hr::core::overlay_mask;
using hr::core::Palette;
using hr::core::to_bgra;

namespace {
Palette test_palette() {
    Palette p{};
    p[1] = 0x112233;
    p[2] = 0x445566;
    p[3] = 0x778899;
    return p;
}
} // namespace

TEST_CASE("to_bgra maps indices through the palette, row by row", "[overlay]") {
    const std::vector<std::uint8_t> px{1, 2, 9, 3, 1, 9}; // 2x2 in stride 3
    const auto out = to_bgra(px.data(), 3, {2, 2}, test_palette());
    CHECK(out == std::vector<std::uint32_t>{0x112233, 0x445566, 0x778899, 0x112233});
}

TEST_CASE("the mask is set where the capture shows the game's own pixel", "[overlay]") {
    // 4x3 capture; the view is the 2x2 at (1,1).
    std::vector<std::uint32_t> captured(12, 0xAA000000u);
    captured[1 * 4 + 1] = 0xFF112233u; // matches (alpha ignored)
    captured[1 * 4 + 2] = 0x00999999u; // covered by something else
    captured[2 * 4 + 1] = 0x00445566u; // matches
    captured[2 * 4 + 2] = 0x00778899u; // matches
    const std::vector<std::uint32_t> game{0x112233, 0x123456, 0x445566, 0x778899};
    CHECK(overlay_mask(captured.data(), {4, 3}, {1, 1, 2, 2}, game) ==
          std::vector<std::uint8_t>{1, 0, 1, 1});
}

TEST_CASE("view pixels outside the capture count as covered", "[overlay]") {
    const std::vector<std::uint32_t> captured(4, 0x00112233u); // 2x2
    const std::vector<std::uint32_t> game(4, 0x112233u);
    CHECK(overlay_mask(captured.data(), {2, 2}, {1, 1, 2, 2}, game) ==
          std::vector<std::uint8_t>{1, 0, 0, 0});
}

TEST_CASE("compose_overlay scales the mask by the factor and keeps colour under alpha 0",
          "[overlay]") {
    // 1x size 2x1 at factor 2: hires 4x2 in stride 5.
    const std::vector<std::uint8_t> hires{1, 2, 3, 1, 0, 2, 3, 1, 2, 0};
    const std::vector<std::uint8_t> mask{1, 0};
    const auto out = compose_overlay(hires.data(), 5, {2, 1}, 2, test_palette(), mask);
    CHECK(out == std::vector<std::uint32_t>{0xFF112233u, 0xFF445566u, 0x00778899u, 0x00112233u,
                                            0xFF445566u, 0xFF778899u, 0x00112233u, 0x00445566u});
}
