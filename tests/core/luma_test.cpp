#include "core/luma.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>
#include <vector>

using Catch::Approx;
using hr::core::BrightnessTrace;
using hr::core::luma_csv_row;
using hr::core::mean_luma;

TEST_CASE("brightness of uniform pictures", "[luma]") {
    const std::vector<std::uint32_t> black(16, 0x000000), white(16, 0xFFFFFF), red(16, 0xFF0000);
    CHECK(mean_luma(black.data(), 4, 4) == Approx(0.0));
    CHECK(mean_luma(white.data(), 4, 4) == Approx(255.0));
    CHECK(mean_luma(red.data(), 4, 4) == Approx(0.299 * 255));
}

TEST_CASE("brightness is the mean over all pixels and ignores alpha", "[luma]") {
    const std::vector<std::uint32_t> half{0xFF000000, 0xFFFFFFFF};
    CHECK(mean_luma(half.data(), 2, 1) == Approx(127.5));
}

TEST_CASE("an empty picture has zero brightness", "[luma]") {
    CHECK(mean_luma(nullptr, 0, 0) == Approx(0.0));
}

TEST_CASE("probe rows carry the run and the mode so several runs can share one file", "[luma]") {
    CHECK(std::string(hr::core::kLumaCsvHeader) == "run,mode,t,luma");
    CHECK(luma_csv_row("2026-10-06 08:30:00", "a2", 1.23456, 127.5) ==
          "2026-10-06 08:30:00,a2,1.2346,127.50");
    CHECK(luma_csv_row("r", "vanilla", 0.0, 0.0) == "r,vanilla,0.0000,0.00");
}

TEST_CASE("the brightness trace logs the first frame and changes of at least the step", "[luma]") {
    BrightnessTrace trace(5.0, 4.0);
    CHECK(trace.should_log(0.0, 46.0));
    CHECK_FALSE(trace.should_log(0.1, 48.0)); // +2: below the step
    CHECK(trace.should_log(0.2, 15.0));       // the dip
    CHECK_FALSE(trace.should_log(0.3, 18.9)); // +3.9 since the last logged value
    CHECK(trace.should_log(0.4, 53.6));
}

TEST_CASE("the brightness trace stops after its duration", "[luma]") {
    BrightnessTrace trace(5.0, 4.0);
    CHECK(trace.running(4.99));
    CHECK_FALSE(trace.running(5.0));
    CHECK_FALSE(trace.should_log(5.0, 100.0));
}
