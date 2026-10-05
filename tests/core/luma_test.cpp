#include "core/luma.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using Catch::Approx;
using hr::core::mean_luma;

TEST_CASE("brightness of uniform pictures", "[luma]") {
    const std::vector<std::uint32_t> black(16, 0x000000), white(16, 0xFFFFFF), red(16, 0xFF0000);
    CHECK(mean_luma(black.data(), 4, 4) == Approx(0.0));
    CHECK(mean_luma(white.data(), 4, 4) == Approx(255.0));
    CHECK(mean_luma(red.data(), 4, 4) == Approx(0.299 * 255));
}

TEST_CASE("brightness is the mean over all pixels and ignores alpha", "[luma]") {
    const std::vector<std::uint32_t> half{0xFF000000, 0xFFFFFFFF};
    CHECK(mean_luma(half.data(), 2, 1) == Approx(127.5));
}

TEST_CASE("an empty picture has zero brightness", "[luma]") {
    CHECK(mean_luma(nullptr, 0, 0) == Approx(0.0));
}
