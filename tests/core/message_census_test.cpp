#include "core/message_census.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

using namespace hr::core;

TEST_CASE("message names: known names, WM_USER/WM_APP offsets, else hex", "[message_census]") {
    CHECK(message_name(0x0020) == "WM_SETCURSOR");
    CHECK(message_name(0x0200) == "WM_MOUSEMOVE");
    CHECK(message_name(0x0084) == "WM_NCHITTEST");
    CHECK(message_name(0x0405) == "WM_USER+5");
    CHECK(message_name(0x8001) == "WM_APP+1");
    CHECK(message_name(0x0123) == "0x0123");
    CHECK(message_name(0xC0A1) == "0xC0A1");
}

TEST_CASE("the census counts per window class and message, sorted, and clears on take",
          "[message_census]") {
    MessageCensus census;
    CHECK(census.empty());
    census.add("Glue", 0x0200);
    census.add("BitmapScrn", 0x0020);
    census.add("Glue", 0x0200);
    census.add("BitmapScrn", 0x0200);
    census.add("Glue", 0x0020);
    CHECK_FALSE(census.empty());
    CHECK(census.take() ==
          std::vector<std::string>{"BitmapScrn WM_SETCURSOR x1", "BitmapScrn WM_MOUSEMOVE x1",
                                   "Glue WM_SETCURSOR x1", "Glue WM_MOUSEMOVE x2"});
    CHECK(census.empty());
    CHECK(census.take().empty());
}
