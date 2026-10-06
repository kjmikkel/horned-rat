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
