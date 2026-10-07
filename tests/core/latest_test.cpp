#include "core/latest.h"

#include <catch2/catch_test_macros.hpp>

#include <string>

using hr::core::Latest;

TEST_CASE("nothing is taken before the first publish", "[latest]") {
    const Latest<std::string> box;
    std::uint64_t seen = 0;
    std::string out = "unchanged";
    CHECK_FALSE(box.take_newer(seen, out));
    CHECK(out == "unchanged");
}

TEST_CASE("a published value is taken once", "[latest]") {
    Latest<std::string> box;
    box.publish("a");
    std::uint64_t seen = 0;
    std::string out;
    CHECK(box.take_newer(seen, out));
    CHECK(out == "a");
    CHECK_FALSE(box.take_newer(seen, out));
}

TEST_CASE("only the newest value is taken", "[latest]") {
    Latest<std::string> box;
    box.publish("a");
    box.publish("b");
    std::uint64_t seen = 0;
    std::string out;
    CHECK(box.take_newer(seen, out));
    CHECK(out == "b");
}

TEST_CASE("each reader keeps its own place", "[latest]") {
    Latest<int> box;
    box.publish(1);
    std::uint64_t first = 0;
    std::uint64_t second = 0;
    int out = 0;
    CHECK(box.take_newer(first, out));
    box.publish(2);
    CHECK(box.take_newer(second, out));
    CHECK(out == 2);
    CHECK(box.take_newer(first, out));
    CHECK(out == 2);
}
