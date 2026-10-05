#include "platform/modules.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cstring>

using Catch::Matchers::EndsWith;

namespace {
int some_function_in_this_exe() { return 42; }
} // namespace

TEST_CASE("loaded_modules includes the executable and kernel32", "[modules]") {
    const auto modules = hr::platform::loaded_modules();
    const auto has = [&](const char* suffix) {
        return std::ranges::any_of(modules, [&](const auto& m) {
            return m.path.size() >= std::strlen(suffix) &&
                   _stricmp(m.path.c_str() + m.path.size() - std::strlen(suffix), suffix) == 0;
        });
    };
    CHECK(has("platform_tests.exe"));
    CHECK(has("kernel32.dll"));
}

TEST_CASE("module_path(nullptr) is the main executable", "[modules]") {
    CHECK_THAT(hr::platform::module_path(nullptr), EndsWith("platform_tests.exe"));
}

TEST_CASE("module_file_name_from_address names the module containing an address", "[modules]") {
    CHECK(hr::platform::module_file_name_from_address(
              reinterpret_cast<const void*>(&some_function_in_this_exe)) == "platform_tests.exe");
    CHECK(hr::platform::module_file_name_from_address(nullptr) == "?");
}
