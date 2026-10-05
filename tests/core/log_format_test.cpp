#include "core/log_format.h"

#include <catch2/catch_test_macros.hpp>

using hr::core::format_log_line;

TEST_CASE("a log line has a fixed-width timestamp and ends with a newline", "[log_format]") {
    CHECK(format_log_line(12.3456, "hello") == "[   12.346] hello\n");
    CHECK(format_log_line(0.0, "") == "[    0.000] \n");
}

TEST_CASE("trailing line breaks are removed", "[log_format]") {
    CHECK(format_log_line(0.0, "msg\r\n") == "[    0.000] msg\n");
    CHECK(format_log_line(0.0, "msg\n\n") == "[    0.000] msg\n");
}

TEST_CASE("interior line breaks become spaces so one record is one line", "[log_format]") {
    CHECK(format_log_line(0.0, "a\nb\r\nc") == "[    0.000] a b  c\n");
}
