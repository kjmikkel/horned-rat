#include "core/scaling.h"

#include <catch2/catch_test_macros.hpp>

using namespace hr::core;

namespace {
constexpr Size kGame{640, 480};
}

TEST_CASE("integer scaling uses the largest whole factor that fits, centred", "[scaling]") {
    CHECK(dest_rect({640, 480}, kGame, ScaleMode::Integer, AspectMode::Keep4x3) ==
          Rect{0, 0, 640, 480});
    CHECK(dest_rect({1920, 1080}, kGame, ScaleMode::Integer, AspectMode::Keep4x3) ==
          Rect{320, 60, 1280, 960});
    CHECK(dest_rect({2560, 1440}, kGame, ScaleMode::Integer, AspectMode::Keep4x3) ==
          Rect{320, 0, 1920, 1440});
    CHECK(dest_rect({1279, 959}, kGame, ScaleMode::Integer, AspectMode::Keep4x3) ==
          Rect{319, 239, 640, 480});
}

TEST_CASE("integer scaling never goes below 1x and overflows a small client", "[scaling]") {
    CHECK(dest_rect({600, 400}, kGame, ScaleMode::Integer, AspectMode::Keep4x3) ==
          Rect{-20, -40, 640, 480});
}

TEST_CASE("integer scaling with stretch uses a whole factor per axis", "[scaling]") {
    CHECK(dest_rect({1920, 1080}, kGame, ScaleMode::Integer, AspectMode::Stretch) ==
          Rect{0, 60, 1920, 960});
}

TEST_CASE("sharp and nearest-fit fill the client with the picture's aspect", "[scaling]") {
    for (const ScaleMode mode : {ScaleMode::Sharp, ScaleMode::NearestFit}) {
        CHECK(dest_rect({1920, 1080}, kGame, mode, AspectMode::Keep4x3) ==
              Rect{240, 0, 1440, 1080});
        CHECK(dest_rect({1280, 1024}, kGame, mode, AspectMode::Keep4x3) == Rect{0, 32, 1280, 960});
        CHECK(dest_rect({1280, 960}, kGame, mode, AspectMode::Keep4x3) == Rect{0, 0, 1280, 960});
        CHECK(dest_rect({1001, 700}, kGame, mode, AspectMode::Keep4x3) == Rect{34, 0, 933, 700});
        CHECK(dest_rect({320, 240}, kGame, mode, AspectMode::Keep4x3) == Rect{0, 0, 320, 240});
    }
}

TEST_CASE("sharp and nearest-fit with stretch fill the whole client", "[scaling]") {
    CHECK(dest_rect({1920, 1080}, kGame, ScaleMode::Sharp, AspectMode::Stretch) ==
          Rect{0, 0, 1920, 1080});
    CHECK(dest_rect({1001, 700}, kGame, ScaleMode::NearestFit, AspectMode::Stretch) ==
          Rect{0, 0, 1001, 700});
}

TEST_CASE("the aspect is the content's own, also for the full-client fallback", "[scaling]") {
    CHECK(dest_rect({1300, 982}, {650, 491}, ScaleMode::Sharp, AspectMode::Keep4x3) ==
          Rect{0, 0, 1300, 982});
}

TEST_CASE("an empty client or picture gives an empty destination", "[scaling]") {
    CHECK(dest_rect({0, 0}, kGame, ScaleMode::Sharp, AspectMode::Keep4x3) == Rect{0, 0, 0, 0});
    CHECK(dest_rect({800, 600}, {0, 0}, ScaleMode::Integer, AspectMode::Keep4x3) ==
          Rect{0, 0, 0, 0});
}

TEST_CASE("the sharp prescale is the whole part of the scale per axis, at least 1", "[scaling]") {
    CHECK(sharp_prescale(kGame, {240, 0, 1440, 1080}) == Size{2, 2});
    CHECK(sharp_prescale(kGame, {0, 0, 1280, 960}) == Size{2, 2});
    CHECK(sharp_prescale(kGame, {0, 0, 1920, 960}) == Size{3, 2});
    CHECK(sharp_prescale(kGame, {0, 0, 600, 450}) == Size{1, 1});
    CHECK(sharp_prescale({0, 0}, {0, 0, 600, 450}) == Size{1, 1});
}

TEST_CASE("fit_inside keeps sizes that fit and shrinks others keeping their aspect", "[scaling]") {
    CHECK(fit_inside({1280, 960}, {1920, 1040}) == Size{1280, 960});
    CHECK(fit_inside({1280, 960}, {1366, 728}) == Size{970, 728});
    CHECK(fit_inside({2000, 1000}, {1000, 1000}) == Size{1000, 500});
    CHECK(fit_inside({1280, 960}, {1280, 960}) == Size{1280, 960});
}
