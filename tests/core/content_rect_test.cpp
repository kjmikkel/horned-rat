#include "core/content_rect.h"

#include <catch2/catch_test_macros.hpp>

using namespace hr::core;

TEST_CASE("the BitmapScrn child is the content when it lies inside the client", "[content_rect]") {
    CHECK(choose_content_rect({650, 491}, Rect{5, 5, 640, 480}) == Rect{5, 5, 640, 480});
}

TEST_CASE("without a usable child the whole client is captured", "[content_rect]") {
    CHECK(choose_content_rect({650, 491}, std::nullopt) == Rect{0, 0, 650, 491});
    CHECK(choose_content_rect({650, 491}, Rect{5, 5, 0, 480}) == Rect{0, 0, 650, 491});
    CHECK(choose_content_rect({650, 491}, Rect{20, 20, 640, 480}) == Rect{0, 0, 650, 491});
    CHECK(choose_content_rect({650, 491}, Rect{-1, 0, 640, 480}) == Rect{0, 0, 650, 491});
}
