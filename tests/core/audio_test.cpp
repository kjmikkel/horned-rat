#include "core/audio.h"

#include <catch2/catch_test_macros.hpp>

using namespace hr::core;

TEST_CASE("secondary sound buffers get global focus so they play while the presenter is active",
          "[audio]") {
    CHECK(with_global_focus(0x00000080u) == (0x00000080u | kDsbcapsGlobalFocus));
    CHECK(with_global_focus(0x00000000u) == kDsbcapsGlobalFocus);
    CHECK(with_global_focus(kDsbcapsGlobalFocus) == kDsbcapsGlobalFocus);
}

TEST_CASE("the primary buffer is left alone", "[audio]") {
    CHECK(with_global_focus(0x00000081u) == 0x00000081u);
}
