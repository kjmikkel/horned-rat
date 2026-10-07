#include "core/gamef_layout.h"

#include <catch2/catch_test_macros.hpp>

using hr::core::gamef_layout;

namespace {
constexpr const char* kGog = "e8afe836e9c99b54a76643b2adb4edb52f56a991d53ab8af60f746129b1132d6";
}

TEST_CASE("the GOG build of GAMEF.DLL has a layout", "[gamef_layout]") {
    const auto layout = gamef_layout(kGog);
    REQUIRE(layout);
    CHECK(layout->create_device == 0x57040);
    CHECK(layout->create_viewport == 0x57BF0);
    CHECK(layout->render_viewport == 0x583C0);
    CHECK(layout->viewport_type == 0xFB0E8);
    CHECK(layout->device_type == 0xFAFC0);
    CHECK(layout->w_viewport == 0x32874);
    CHECK(layout->w_device_desc == 0x32878);
    CHECK(layout->vp_back == 0xC4);
}

TEST_CASE("the hash is matched case-insensitively", "[gamef_layout]") {
    CHECK(gamef_layout("E8AFE836E9C99B54A76643B2ADB4EDB52F56A991D53AB8AF60F746129B1132D6"));
}

TEST_CASE("any other build has no layout", "[gamef_layout]") {
    CHECK_FALSE(gamef_layout("0000000000000000000000000000000000000000000000000000000000000000"));
    CHECK_FALSE(gamef_layout(""));
}
