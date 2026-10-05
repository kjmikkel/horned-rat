#include "core/ini.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using Catch::Matchers::ContainsSubstring;
using hr::core::parse_ini;

TEST_CASE("empty text gives an empty document", "[ini]") {
    const auto doc = parse_ini("");
    CHECK(doc.sections.empty());
    CHECK(doc.warnings.empty());
}

TEST_CASE("keys are read per section, names lower-cased and values trimmed", "[ini]") {
    const auto doc = parse_ini("[Display]\n  Mode =  windowed  \nscale=sharp\n");
    REQUIRE(doc.find("display", "mode") != nullptr);
    CHECK(*doc.find("display", "mode") == "windowed");
    CHECK(*doc.find("display", "scale") == "sharp");
    CHECK(doc.warnings.empty());
}

TEST_CASE("find returns nullptr for missing sections and keys", "[ini]") {
    const auto doc = parse_ini("[a]\nk = v\n");
    CHECK(doc.find("b", "k") == nullptr);
    CHECK(doc.find("a", "x") == nullptr);
}

TEST_CASE("comments are ignored, including inline ones", "[ini]") {
    const auto doc = parse_ini("; full line\n# also a comment\n[a]\nk = v ; trailing\n");
    CHECK(*doc.find("a", "k") == "v");
    CHECK(doc.warnings.empty());
}

TEST_CASE("CRLF line endings are accepted", "[ini]") {
    const auto doc = parse_ini("[a]\r\nk = v\r\n");
    CHECK(*doc.find("a", "k") == "v");
    CHECK(doc.warnings.empty());
}

TEST_CASE("keys before any section go into the unnamed section", "[ini]") {
    const auto doc = parse_ini("k = v\n");
    CHECK(*doc.find("", "k") == "v");
}

TEST_CASE("the last duplicate key wins", "[ini]") {
    const auto doc = parse_ini("[a]\nk = 1\nk = 2\n");
    CHECK(*doc.find("a", "k") == "2");
}

TEST_CASE("malformed lines give warnings with line numbers and parsing continues", "[ini]") {
    const auto doc = parse_ini("[a]\nnonsense\n[broken\n= novalue\nk = v\n");
    REQUIRE(doc.warnings.size() == 3);
    CHECK(doc.warnings[0].line == 2);
    CHECK_THAT(doc.warnings[0].message, ContainsSubstring("key = value"));
    CHECK(doc.warnings[1].line == 3);
    CHECK_THAT(doc.warnings[1].message, ContainsSubstring("section"));
    CHECK(doc.warnings[2].line == 4);
    CHECK_THAT(doc.warnings[2].message, ContainsSubstring("key name"));
    CHECK(*doc.find("a", "k") == "v");
}
