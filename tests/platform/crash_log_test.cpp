#include "platform/crash_log.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <windows.h>

#include <string>
#include <vector>

using Catch::Matchers::ContainsSubstring;

namespace {
std::vector<std::string> g_lines;

// Reads address 0x10 and survives through SEH, as the game's own handler would.
__declspec(noinline) bool fault_and_recover() {
    __try {
        volatile int* const bad = reinterpret_cast<volatile int*>(0x10);
        return *bad == 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return true;
    }
}
} // namespace

TEST_CASE("access violations are logged with their place, even when handled", "[crash_log]") {
    g_lines.clear();
    {
        hr::platform::AccessViolationLog log(
            3, [](const std::string& line) { g_lines.push_back(line); });
        CHECK(fault_and_recover());
    }
    REQUIRE(g_lines.size() >= 1);
    CHECK_THAT(g_lines[0], ContainsSubstring("access violation") &&
                               ContainsSubstring("reading 0x00000010") &&
                               ContainsSubstring("platform_tests.exe+0x"));
}

TEST_CASE("only the first access violations are logged", "[crash_log]") {
    g_lines.clear();
    {
        hr::platform::AccessViolationLog log(
            2, [](const std::string& line) { g_lines.push_back(line); });
        for (int i = 0; i < 5; ++i) {
            fault_and_recover();
        }
    }
    CHECK(g_lines.size() == 2);
    fault_and_recover(); // removed with the log
    CHECK(g_lines.size() == 2);
}
