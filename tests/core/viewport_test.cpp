#include "core/viewport.h"

#include <catch2/catch_test_macros.hpp>

using namespace hr::core;

TEST_CASE("the picture is centred in the client area at 1:1", "[viewport]") {
    CHECK(centered_rect({640, 480}, {640, 480}) == Rect{0, 0, 640, 480});
    CHECK(centered_rect({800, 600}, {640, 480}) == Rect{80, 60, 640, 480});
    CHECK(centered_rect({600, 400}, {640, 480}) == Rect{-20, -40, 640, 480});
}

TEST_CASE("client points map to content points through the destination rect", "[viewport]") {
    const Rect dest{80, 60, 640, 480};
    CHECK(to_content({80, 60}, dest, {640, 480}) == Point{0, 0});
    CHECK(to_content({85, 70}, dest, {640, 480}) == Point{5, 10});
    CHECK(to_content({719, 539}, dest, {640, 480}) == Point{639, 479});
}

TEST_CASE("points outside the picture clamp to its nearest edge", "[viewport]") {
    const Rect dest{80, 60, 640, 480};
    CHECK(to_content({0, 0}, dest, {640, 480}) == Point{0, 0});
    CHECK(to_content({79, 59}, dest, {640, 480}) == Point{0, 0});
    CHECK(to_content({5000, -5}, dest, {640, 480}) == Point{639, 0});
}

TEST_CASE("mapping also works for scaled destinations", "[viewport]") {
    const Rect dest{0, 0, 1280, 960};
    CHECK(to_content({3, 3}, dest, {640, 480}) == Point{1, 1});
    CHECK(to_content({1279, 959}, dest, {640, 480}) == Point{639, 479});
}

TEST_CASE("an empty destination maps everything to the origin", "[viewport]") {
    CHECK(to_content({10, 10}, Rect{0, 0, 0, 0}, {640, 480}) == Point{0, 0});
}

TEST_CASE("the fake cursor is the hidden window's client origin plus content offset and point",
          "[viewport]") {
    CHECK(fake_cursor_screen({-32000, -32000}, Rect{5, 5, 640, 480}, {10, 20}) ==
          Point{-31985, -31975});
}

TEST_CASE("keep_inside moves a window as little as possible to fit an area", "[viewport]") {
    const Rect area{0, 0, 1920, 1040};
    CHECK(keep_inside({100, 100, 656, 520}, area) == Point{100, 100});
    CHECK(keep_inside({-150, 100, 656, 520}, area) == Point{0, 100});
    CHECK(keep_inside({1800, 900, 656, 520}, area) == Point{1264, 520});
    CHECK(keep_inside({-32000, -32000, 656, 520}, area) == Point{0, 0});
    CHECK(keep_inside({50, 50, 4000, 3000}, area) == Point{0, 0});
    CHECK(keep_inside({1950, 10, 100, 100}, Rect{1920, 0, 1920, 1080}) == Point{1950, 10});
}
