#include "platform/log.h"
#include "test_paths.h"

#include <catch2/catch_test_macros.hpp>

#include <regex>
#include <sstream>
#include <thread>
#include <vector>

using hr::platform::Log;

TEST_CASE("Log writes formatted, timestamped lines", "[log]") {
    const auto path = hr::test::temp_file("log_basic.txt");
    {
        double now = 0.5;
        Log log(path, [&] { return now; });
        log.write("first");
        now = 2.25;
        log.write("second\n");
    }
    CHECK(hr::test::read_file(path) == "[    0.500] first\n[    2.250] second\n");
}

TEST_CASE("Log truncates an existing file", "[log]") {
    const auto path = hr::test::temp_file("log_truncate.txt");
    {
        Log first(path, [] { return 0.0; });
        first.write("old");
    }
    {
        Log second(path, [] { return 0.0; });
        second.write("new");
    }
    CHECK(hr::test::read_file(path) == "[    0.000] new\n");
}

TEST_CASE("concurrent writes never interleave within a line", "[log]") {
    const auto path = hr::test::temp_file("log_threads.txt");
    {
        Log log(path, [] { return 1.0; });
        std::vector<std::thread> threads;
        for (int t = 0; t < 8; ++t) {
            threads.emplace_back([&log, t] {
                for (int i = 0; i < 200; ++i) {
                    log.write("thread " + std::to_string(t) + " line " + std::to_string(i));
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
    }
    std::istringstream lines(hr::test::read_file(path));
    const std::regex well_formed(R"(\[ *1\.000\] thread \d line \d+)");
    int count = 0;
    for (std::string line; std::getline(lines, line);) {
        INFO(line);
        CHECK(std::regex_match(line, well_formed));
        ++count;
    }
    CHECK(count == 1600);
}

TEST_CASE("the global log forwards to the installed Log and is a no-op without one", "[log]") {
    const auto path = hr::test::temp_file("log_global.txt");
    hr::platform::log("dropped: no global log installed");
    {
        Log log(path, [] { return 0.0; });
        hr::platform::set_global_log(&log);
        hr::platform::log("kept");
        hr::platform::log_exception("Hook", "boom");
        hr::platform::log_exception("Hook", nullptr);
        hr::platform::set_global_log(nullptr);
    }
    CHECK(hr::test::read_file(path) == "[    0.000] kept\n"
                                       "[    0.000] exception in Hook: boom\n"
                                       "[    0.000] unknown exception in Hook\n");
}
