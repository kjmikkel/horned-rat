#include "platform/wing_forward.h"
#include "test_paths.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cstdint>

using Catch::Matchers::ContainsSubstring;
using hr::platform::load_original_wing;

TEST_CASE("load_original_wing resolves all ten functions", "[wing_forward]") {
    const auto r = load_original_wing(hr::test::bin_dir() / "wing32_orig.dll");
    REQUIRE(r.error.empty());
    REQUIRE(r.functions.has_value());
    const auto& f = *r.functions;
    CHECK(f.bit_blt != nullptr);
    CHECK(f.create_bitmap != nullptr);
    CHECK(f.create_dc != nullptr);
    CHECK(f.create_halftone_brush != nullptr);
    CHECK(f.create_halftone_palette != nullptr);
    CHECK(f.get_dib_color_table != nullptr);
    CHECK(f.get_dib_pointer != nullptr);
    CHECK(f.recommend_dib_format != nullptr);
    CHECK(f.set_dib_color_table != nullptr);
    CHECK(f.stretch_blt != nullptr);
    CHECK(f.create_dc() == reinterpret_cast<HDC>(std::uintptr_t{0x1001}));
}

TEST_CASE("load_original_wing reports a missing file", "[wing_forward]") {
    const auto r = load_original_wing(hr::test::bin_dir() / "does_not_exist.dll");
    CHECK_FALSE(r.functions.has_value());
    CHECK_THAT(r.error, ContainsSubstring("could not load"));
}

TEST_CASE("load_original_wing reports a DLL that is not WinG", "[wing_forward]") {
    wchar_t system_dir[MAX_PATH];
    const UINT n = GetSystemDirectoryW(system_dir, MAX_PATH);
    const auto path = std::filesystem::path(std::wstring(system_dir, n)) / "version.dll";
    const auto r = load_original_wing(path);
    CHECK_FALSE(r.functions.has_value());
    CHECK_THAT(r.error, ContainsSubstring("does not export WinGBitBlt"));
}
