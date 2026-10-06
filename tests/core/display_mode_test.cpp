#include "core/display_mode.h"

#include <catch2/catch_test_macros.hpp>

using hr::core::defer_display_change;
using hr::core::kCdsFullscreen;
using hr::core::kCdsTest;

TEST_CASE("display-mode changes are deferred while the presenter owns the display",
          "[display_mode]") {
    CHECK(defer_display_change(true, kCdsFullscreen));
    CHECK(defer_display_change(true, 0)); // restoring the registry mode too
    CHECK_FALSE(defer_display_change(false, kCdsFullscreen));
    CHECK_FALSE(defer_display_change(false, 0));
}

TEST_CASE("test-only display-mode calls always reach Windows", "[display_mode]") {
    CHECK_FALSE(defer_display_change(true, kCdsTest));
    CHECK_FALSE(defer_display_change(true, kCdsTest | kCdsFullscreen));
}