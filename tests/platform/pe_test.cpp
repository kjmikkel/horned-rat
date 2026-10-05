#include "platform/pe.h"
#include "platform/wing_api.h"
#include "test_paths.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <string>
#include <vector>

TEST_CASE("export_names lists every named export of a module", "[pe]") {
    const auto path = hr::test::bin_dir() / "wing32_orig.dll";
    // DONT_RESOLVE_DLL_REFERENCES maps the image without running its DllMain.
    const HMODULE module = LoadLibraryExW(path.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    REQUIRE(module != nullptr);
    auto names = hr::platform::pe::export_names(module);
    FreeLibrary(module);

    std::vector<std::string> expected(hr::platform::wing::export_names.begin(),
                                      hr::platform::wing::export_names.end());
    expected.emplace_back("FakeWing_LastCall");
    std::ranges::sort(names);
    std::ranges::sort(expected);
    CHECK(names == expected);
}

TEST_CASE("export_names of a module without exports is empty", "[pe]") {
    CHECK(hr::platform::pe::export_names(GetModuleHandleW(nullptr)).empty());
}
