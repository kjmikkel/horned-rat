#include "platform/guard.h"
#include "platform/log.h"
#include "test_paths.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using hr::platform::guarded;

TEST_CASE("guarded returns the body's result when nothing throws", "[guard]") {
    CHECK(guarded("t", [] { return 1; }, [] { return 2; }) == 1);
}

TEST_CASE("guarded logs an exception and returns the fallback", "[guard]") {
    const auto path = hr::test::temp_file("guard.txt");
    {
        hr::platform::Log log(path, [] { return 0.0; });
        hr::platform::set_global_log(&log);
        const int r =
            guarded("Hook", []() -> int { throw std::runtime_error("boom"); }, [] { return 2; });
        CHECK(r == 2);
        const int r2 = guarded("Other", []() -> int { throw 42; }, [] { return 3; });
        CHECK(r2 == 3);
        hr::platform::set_global_log(nullptr);
    }
    CHECK(hr::test::read_file(path) == "[    0.000] exception in Hook: boom\n"
                                       "[    0.000] unknown exception in Other\n");
}

TEST_CASE("guarded supports void bodies", "[guard]") {
    bool fallback_ran = false;
    guarded("v", [] { throw std::runtime_error("x"); }, [&] { fallback_ran = true; });
    CHECK(fallback_ran);
}
