#include "core/indexed_frame.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdint>
#include <vector>

using hr::core::count_differing_pixels;
using hr::core::IndexedFrame;

namespace {
std::array<std::uint32_t, 256> palette_with(std::uint8_t index, std::uint32_t colour) {
    std::array<std::uint32_t, 256> p{};
    p[index] = colour;
    return p;
}
} // namespace

TEST_CASE("identical frames have no differing pixels", "[indexed_frame]") {
    const std::vector<std::uint8_t> px{1, 2, 3, 4};
    const auto pal = palette_with(1, 0x112233);
    CHECK(count_differing_pixels({px.data(), 2, &pal}, {px.data(), 2, &pal}, 2, 2) == 0);
}

TEST_CASE("different indices with the same colour are equal", "[indexed_frame]") {
    const std::vector<std::uint8_t> a{7};
    const std::vector<std::uint8_t> b{9};
    const auto pa = palette_with(7, 0xABCDEF);
    const auto pb = palette_with(9, 0xABCDEF);
    CHECK(count_differing_pixels({a.data(), 1, &pa}, {b.data(), 1, &pb}, 1, 1) == 0);
}

TEST_CASE("a pixel with another colour is counted", "[indexed_frame]") {
    const std::vector<std::uint8_t> a{1, 1};
    const std::vector<std::uint8_t> b{1, 2};
    const auto pal = palette_with(2, 0xFF0000);
    CHECK(count_differing_pixels({a.data(), 2, &pal}, {b.data(), 2, &pal}, 2, 1) == 1);
}

TEST_CASE("only width x height is compared, each frame with its own stride", "[indexed_frame]") {
    // a: 2x2 inside stride 4 (padding 9s); b: the same 2x2 inside stride 2.
    const std::vector<std::uint8_t> a{1, 2, 9, 9, 3, 4, 9, 9};
    const std::vector<std::uint8_t> b{1, 2, 3, 4};
    std::array<std::uint32_t, 256> pal{};
    for (int i = 0; i < 256; ++i) {
        pal[i] = static_cast<std::uint32_t>(i);
    }
    CHECK(count_differing_pixels({a.data(), 4, &pal}, {b.data(), 2, &pal}, 2, 2) == 0);
}
