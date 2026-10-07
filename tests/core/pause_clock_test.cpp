#include "core/pause_clock.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using hr::core::PauseClock;

TEST_CASE("the game clock is real time until the game is held", "[pause_clock]") {
    PauseClock clock;
    CHECK_FALSE(clock.held());
    CHECK(clock.game_time(1000) == 1000u);
}

TEST_CASE("the game clock stands still while held and resumes without a jump", "[pause_clock]") {
    PauseClock clock;
    clock.hold(1000);
    CHECK(clock.held());
    CHECK(clock.game_time(1000) == 1000u);
    CHECK(clock.game_time(6000) == 1000u);
    clock.release(6000);
    CHECK_FALSE(clock.held());
    CHECK(clock.game_time(6000) == 1000u);
    CHECK(clock.game_time(6500) == 1500u);
}

TEST_CASE("held time adds up over several holds", "[pause_clock]") {
    PauseClock clock;
    clock.hold(100);
    clock.release(300); // 200 held
    clock.hold(400);
    clock.hold(450);    // already held: ignored
    clock.release(700); // 300 held
    clock.release(800); // not held: ignored
    CHECK(clock.game_time(1000) == 500u);
}

TEST_CASE("the game clock wraps like GetTickCount", "[pause_clock]") {
    PauseClock clock;
    const std::uint32_t before_wrap = 0xFFFFFF00u;
    clock.hold(before_wrap);
    clock.release(0x00000100u); // 0x200 held across the wrap
    CHECK(clock.game_time(0x00000200u) == before_wrap + 0x100u);
}
