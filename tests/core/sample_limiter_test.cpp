#include "core/sample_limiter.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("the first calls are always logged, later only every Nth", "[sample_limiter]") {
    constexpr hr::core::SampleLimiter limiter{};
    CHECK(limiter.should_log(1));
    CHECK(limiter.should_log(50));
    CHECK_FALSE(limiter.should_log(51));
    CHECK_FALSE(limiter.should_log(999));
    CHECK(limiter.should_log(1000));
    CHECK_FALSE(limiter.should_log(1001));
    CHECK(limiter.should_log(2000));
}

TEST_CASE("the limits are configurable", "[sample_limiter]") {
    constexpr hr::core::SampleLimiter limiter{2, 10};
    CHECK(limiter.should_log(2));
    CHECK_FALSE(limiter.should_log(3));
    CHECK(limiter.should_log(10));
}
