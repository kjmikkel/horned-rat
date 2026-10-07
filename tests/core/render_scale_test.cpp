#include "core/render_scale.h"

#include <catch2/catch_test_macros.hpp>

using hr::core::render_factor;
using hr::core::RenderBudget;

TEST_CASE("auto rounds the on-screen scale up, the larger axis wins", "[render_scale]") {
    CHECK(render_factor(0, {640, 480}, {0, 0, 640, 480}) == 1);
    CHECK(render_factor(0, {640, 480}, {0, 0, 1280, 960}) == 2);
    CHECK(render_factor(0, {640, 480}, {240, 0, 1440, 1080}) == 3); // sharp at 2.25
    CHECK(render_factor(0, {640, 480}, {0, 60, 1920, 960}) == 3);   // integer stretch 3x2
}

TEST_CASE("auto is clamped to 1..4", "[render_scale]") {
    CHECK(render_factor(0, {640, 480}, {0, 0, 320, 240}) == 1);
    CHECK(render_factor(0, {640, 480}, {0, 0, 3840, 2880}) == 4);
}

TEST_CASE("a fixed setting is used as is", "[render_scale]") {
    CHECK(render_factor(2, {640, 480}, {0, 0, 3840, 2880}) == 2);
    CHECK(render_factor(1, {640, 480}, {0, 0, 1920, 1440}) == 1);
}

TEST_CASE("an empty picture or destination renders at 1", "[render_scale]") {
    CHECK(render_factor(0, {0, 0}, {0, 0, 1280, 960}) == 1);
    CHECK(render_factor(0, {640, 480}, {0, 0, 0, 0}) == 1);
}

TEST_CASE("fast renders keep the cap at 4", "[render_scale]") {
    RenderBudget budget;
    for (int i = 0; i <= 30; ++i) {
        budget.add(i * 0.1, 3.0, 3);
    }
    CHECK(budget.cap() == 4);
}

TEST_CASE("a 2 s window averaging over 50 ms lowers the cap below the factor", "[render_scale]") {
    RenderBudget budget;
    for (int i = 0; i <= 20; ++i) {
        budget.add(i * 0.1, 60.0, 3);
    }
    CHECK(budget.cap() == 2);
}

TEST_CASE("the cap never goes below 1", "[render_scale]") {
    RenderBudget budget;
    for (int i = 0; i <= 20; ++i) {
        budget.add(i * 0.1, 80.0, 1);
    }
    CHECK(budget.cap() == 1);
}

TEST_CASE("a slow spike inside a fast window does not lower the cap", "[render_scale]") {
    RenderBudget budget;
    budget.add(0.0, 120.0, 3);
    for (int i = 1; i <= 20; ++i) {
        budget.add(i * 0.1, 2.0, 3);
    }
    CHECK(budget.cap() == 4);
}
