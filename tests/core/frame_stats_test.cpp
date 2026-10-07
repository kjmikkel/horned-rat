#include "core/frame_stats.h"

#include <catch2/catch_test_macros.hpp>

using hr::core::FrameStats;

TEST_CASE("the performance line has means and the slowest frame", "[frame_stats]") {
    FrameStats stats;
    stats.add(2.0, 14.0);
    stats.add(4.0, 16.0);
    CHECK(stats.frames() == 2);
    CHECK(stats.take_line(10) == "2 frames in 10 s, capture 3.00 ms, present 15.00 ms, "
                                 "slowest 20.00 ms (capture 4.00, present 16.00)");
}

TEST_CASE("taking the line starts a new period", "[frame_stats]") {
    FrameStats stats;
    stats.add(30.0, 5.0);
    (void)stats.take_line(10);
    CHECK(stats.frames() == 0);
    stats.add(1.0, 1.0);
    CHECK(stats.take_line(10) == "1 frames in 10 s, capture 1.00 ms, present 1.00 ms, "
                                 "slowest 2.00 ms (capture 1.00, present 1.00)");
}

TEST_CASE("a period without frames says so", "[frame_stats]") {
    FrameStats stats;
    CHECK(stats.take_line(10) == "0 frames in 10 s");
}

TEST_CASE("the slowest frame's parts come from that frame", "[frame_stats]") {
    FrameStats stats;
    stats.add(60.0, 5.0); // slow capture
    stats.add(1.0, 40.0); // slower in present, but faster overall
    CHECK(stats.take_line(10) == "2 frames in 10 s, capture 30.50 ms, present 22.50 ms, "
                                 "slowest 65.00 ms (capture 60.00, present 5.00)");
}
