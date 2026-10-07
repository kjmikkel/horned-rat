#include "core/config.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

using namespace hr::core;
using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

TEST_CASE("an empty file gives the defaults without warnings", "[config]") {
    const auto r = load_config("");
    CHECK(r.config == Config{});
    CHECK(r.warnings.empty());
}

TEST_CASE("every key is read", "[config]") {
    const auto r = load_config("[general]\nenabled = false\nbackground = pause\n"
                               "[display]\nmode = borderless\nwindow_size = 1920x1440\n"
                               "scale = integer\naspect = stretch\nvsync = no\ncursor = native\n"
                               "render_scale = 2\n"
                               "[debug]\nlog = off\ngame_debug = 0\nmessage_census = on\n");
    CHECK(r.warnings.empty());
    CHECK_FALSE(r.config.enabled);
    CHECK(r.config.background == Background::Pause);
    CHECK(r.config.mode == DisplayMode::Borderless);
    CHECK(r.config.window_size == Size{1920, 1440});
    CHECK(r.config.scale == ScaleMode::Integer);
    CHECK(r.config.aspect == AspectMode::Stretch);
    CHECK_FALSE(r.config.vsync);
    CHECK_FALSE(r.config.log);
    CHECK_FALSE(r.config.game_debug);
    CHECK(r.config.message_census);
    CHECK(r.config.cursor == CursorMode::Native);
    CHECK(r.config.render_scale == 2);
}

TEST_CASE("an invalid cursor mode warns and keeps scaled", "[config]") {
    const auto r = load_config("[display]\ncursor = huge\n");
    CHECK(r.config.cursor == CursorMode::Scaled);
    REQUIRE(r.warnings.size() == 1);
    CHECK_THAT(r.warnings[0], ContainsSubstring("display.cursor") && ContainsSubstring("huge"));
}

TEST_CASE("an invalid background mode warns and keeps run", "[config]") {
    const auto r = load_config("[general]\nbackground = sleep\n");
    CHECK(r.config.background == Background::Run);
    REQUIRE(r.warnings.size() == 1);
    CHECK_THAT(r.warnings[0],
               ContainsSubstring("general.background") && ContainsSubstring("sleep"));
}

TEST_CASE("values are case-insensitive", "[config]") {
    const auto r =
        load_config("[display]\nscale = Nearest-Fit\nmode = WINDOWED\n[general]\nenabled = Yes\n");
    CHECK(r.warnings.empty());
    CHECK(r.config.scale == ScaleMode::NearestFit);
    CHECK(r.config.mode == DisplayMode::Windowed);
    CHECK(r.config.enabled);
}

TEST_CASE("invalid values keep the default and warn", "[config]") {
    const auto r = load_config(
        "[display]\nscale = blurry\nwindow_size = 100x50\n[general]\nenabled = maybe\n");
    CHECK(r.config == Config{});
    REQUIRE(r.warnings.size() == 3);
    CHECK_THAT(r.warnings[0], ContainsSubstring("display.scale") && ContainsSubstring("blurry"));
    CHECK_THAT(r.warnings[1],
               ContainsSubstring("display.window_size") && ContainsSubstring("100x50"));
    CHECK_THAT(r.warnings[2], ContainsSubstring("general.enabled") && ContainsSubstring("maybe"));
}

TEST_CASE("unknown keys and sections warn", "[config]") {
    const auto r = load_config("top = 1\n[display]\nzoom = 2\n[extra]\nx = 3\n");
    REQUIRE(r.warnings.size() == 3);
    CHECK_THAT(r.warnings[0], ContainsSubstring("unknown key 'top'"));
    CHECK_THAT(r.warnings[1], ContainsSubstring("unknown key 'display.zoom'"));
    CHECK_THAT(r.warnings[2], ContainsSubstring("unknown key 'extra.x'"));
}

TEST_CASE("INI syntax warnings are passed on with their line number", "[config]") {
    const auto r = load_config("[display]\nnonsense\n");
    REQUIRE(r.warnings.size() == 1);
    CHECK_THAT(r.warnings[0], StartsWith("line 2: "));
}

TEST_CASE("window_size accepts an upper-case X and rejects malformed sizes", "[config]") {
    CHECK(load_config("[display]\nwindow_size = 800X600\n").config.window_size == Size{800, 600});
    for (const char* bad :
         {"800", "x600", "800x", "800x600x2", "-800x600", "abcxdef", "800 x 600", "99999x600"}) {
        INFO(bad);
        CHECK(load_config(std::string("[display]\nwindow_size = ") + bad + "\n").warnings.size() ==
              1);
    }
}

TEST_CASE("the default file parses back to the defaults", "[config]") {
    const auto r = load_config(default_config_ini());
    CHECK(r.warnings.empty());
    CHECK(r.config == Config{});
}

TEST_CASE("b1_spike is off by default", "[config]") { CHECK(load_config("").config.b1_spike == 0); }

TEST_CASE("b1_spike accepts 0 to 3", "[config]") {
    const auto r = load_config("[debug]\nb1_spike = 3\n");
    CHECK(r.warnings.empty());
    CHECK(r.config.b1_spike == 3);
}

TEST_CASE("an out-of-range b1_spike warns and stays off", "[config]") {
    const auto r = load_config("[debug]\nb1_spike = 5\n");
    CHECK(r.config.b1_spike == 0);
    CHECK(r.warnings.size() == 1);
}

TEST_CASE("render_scale is auto by default", "[config]") {
    CHECK(load_config("").config.render_scale == 0);
}

TEST_CASE("render_scale reads auto and 1 to 4", "[config]") {
    CHECK(load_config("[display]\nrender_scale = 3\n").config.render_scale == 3);
    CHECK(load_config("[display]\nrender_scale = 1\n").config.render_scale == 1);
    const auto r = load_config("[display]\nrender_scale = AUTO\n");
    CHECK(r.warnings.empty());
    CHECK(r.config.render_scale == 0);
}

TEST_CASE("an invalid render_scale warns and stays auto", "[config]") {
    for (const char* bad : {"0", "5", "sharp"}) {
        const auto r = load_config(std::string("[display]\nrender_scale = ") + bad + "\n");
        CHECK(r.config.render_scale == 0);
        CHECK(r.warnings.size() == 1);
    }
}
