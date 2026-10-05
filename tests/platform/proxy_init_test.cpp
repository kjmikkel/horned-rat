#include "test_paths.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <filesystem>

using Catch::Matchers::ContainsSubstring;

// catch_discover_tests runs every test case in its own process, so this always gets a fresh
// DllMain. Removing the ini first guarantees the test cannot pass on a leftover file (the log is
// truncated by the DLL itself on every start).
TEST_CASE("loading the proxy writes the default config and a log, and does not hook a "
          "non-game host",
          "[proxy_init]") {
    const auto bin = hr::test::bin_dir();
    std::filesystem::remove(bin / "horned-rat.ini");
    REQUIRE(LoadLibraryW((bin / "wing32.dll").c_str()) != nullptr);

    CHECK(std::filesystem::exists(bin / "horned-rat.ini"));
    const auto log = hr::test::read_file(bin / "horned-rat.log");
    CHECK_THAT(log, ContainsSubstring("horned-rat 0.1.0 loaded"));
    CHECK_THAT(log, ContainsSubstring("hooks disabled"));
    CHECK_THAT(log, ContainsSubstring("platform_tests.exe"));
}
