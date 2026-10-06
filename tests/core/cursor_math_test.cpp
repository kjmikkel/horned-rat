#include "core/cursor_math.h"

#include <catch2/catch_test_macros.hpp>

using namespace hr::core;

TEST_CASE("after a forwarded WM_SETCURSOR the game's own SetCursor wins", "[cursor_math]") {
    CHECK(cursor_after_set_cursor(true, 0x10, 0x20) == 0x10);
}

TEST_CASE("without a SetCursor call the hit window's class cursor applies", "[cursor_math]") {
    CHECK(cursor_after_set_cursor(false, 0x10, 0x20) == 0x20);
}

TEST_CASE("without either the cursor stays as it was", "[cursor_math]") {
    CHECK(cursor_after_set_cursor(false, 0x10, 0) == 0x10);
}

namespace {
constexpr Size kGame{640, 480};
}

TEST_CASE("the cursor scale is the picture's whole factor per axis, at least 1", "[cursor_math]") {
    CHECK(cursor_scale(kGame, {0, 0, 640, 480}) == Size{1, 1});
    CHECK(cursor_scale(kGame, {320, 60, 1280, 960}) == Size{2, 2});
    CHECK(cursor_scale(kGame, {240, 0, 1440, 1080}) == Size{2, 2});
    CHECK(cursor_scale(kGame, {0, 60, 1920, 960}) == Size{3, 2});
    CHECK(cursor_scale(kGame, {0, 0, 320, 240}) == Size{1, 1});
    CHECK(cursor_scale({0, 0}, {0, 0, 640, 480}) == Size{1, 1});
}

TEST_CASE("the cursor's hotspot sits on the top-left corner of game pixel g", "[cursor_math]") {
    // 1:1
    CHECK(cursor_rect({100, 50}, kGame, {0, 0, 640, 480}, {32, 32}, {5, 3}) ==
          Rect{95, 47, 32, 32});
    // integer 2x with bars
    CHECK(cursor_rect({100, 50}, kGame, {320, 60, 1280, 960}, {32, 32}, {5, 3}) ==
          Rect{510, 154, 64, 64});
    // sharp 2.25x: pixel edges round down
    CHECK(cursor_rect({639, 479}, kGame, {240, 0, 1440, 1080}, {32, 32}, {0, 0}) ==
          Rect{1677, 1077, 64, 64});
    // stretch 3x2
    CHECK(cursor_rect({10, 10}, kGame, {0, 60, 1920, 960}, {16, 16}, {1, 1}) ==
          Rect{27, 78, 48, 32});
}

TEST_CASE("a cursor near the top-left corner may start outside the client", "[cursor_math]") {
    CHECK(cursor_rect({0, 0}, kGame, {0, 0, 640, 480}, {32, 32}, {5, 3}) == Rect{-5, -3, 32, 32});
}
