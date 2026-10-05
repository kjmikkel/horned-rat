#include "fake_wing/fake_wing.h"
#include "platform/pe.h"
#include "platform/wing_api.h"
#include "test_paths.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <string>
#include <type_traits>
#include <vector>

using namespace hr::platform::wing;

namespace {

HMODULE proxy() {
    static const HMODULE module = LoadLibraryW((hr::test::bin_dir() / "wing32.dll").c_str());
    return module;
}

template <typename F> F proxy_fn(const char* name) {
    REQUIRE(proxy() != nullptr);
    const auto fn = reinterpret_cast<F>(GetProcAddress(proxy(), name));
    REQUIRE(fn != nullptr);
    return fn;
}

const FakeWingCall& last_call() {
    const auto get = reinterpret_cast<FakeWing_LastCall_t>(
        GetProcAddress(GetModuleHandleW(L"wing32_orig.dll"), "FakeWing_LastCall"));
    REQUIRE(get != nullptr);
    return *get();
}

std::vector<std::uintptr_t> args_of(const FakeWingCall& call) {
    return {call.args, call.args + call.argc};
}

template <typename T> std::uintptr_t u(T v) {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<std::uintptr_t>(v);
    } else {
        return static_cast<std::uintptr_t>(v);
    }
}

template <typename H> H handle(std::uintptr_t value) { return reinterpret_cast<H>(value); }

} // namespace

TEST_CASE("the proxy exports exactly the ten WinG functions", "[proxy]") {
    REQUIRE(proxy() != nullptr);
    auto names = hr::platform::pe::export_names(proxy());
    std::vector<std::string> expected(export_names.begin(), export_names.end());
    std::ranges::sort(names);
    CHECK(names == expected);
}

TEST_CASE("every export forwards its arguments and result to the original", "[proxy]") {
    const auto dc = handle<HDC>(11);
    const auto other_dc = handle<HDC>(22);

    SECTION("WinGCreateDC") {
        CHECK(proxy_fn<CreateDC_t>("WinGCreateDC")() == handle<HDC>(0x1001));
        CHECK(std::string(last_call().function) == "WinGCreateDC");
        CHECK(last_call().argc == 0);
    }
    SECTION("WinGRecommendDIBFormat") {
        BITMAPINFO info{};
        CHECK(proxy_fn<RecommendDIBFormat_t>("WinGRecommendDIBFormat")(&info) == TRUE);
        CHECK(std::string(last_call().function) == "WinGRecommendDIBFormat");
        CHECK(args_of(last_call()) == std::vector{u(&info)});
    }
    SECTION("WinGCreateBitmap") {
        BITMAPINFO info{};
        void* bits = nullptr;
        CHECK(proxy_fn<CreateBitmap_t>("WinGCreateBitmap")(dc, &info, &bits) ==
              handle<HBITMAP>(0x1002));
        CHECK(bits == handle<void*>(0x1003));
        CHECK(std::string(last_call().function) == "WinGCreateBitmap");
        CHECK(args_of(last_call()) == std::vector{u(dc), u(&info), u(&bits)});
    }
    SECTION("WinGGetDIBPointer") {
        BITMAPINFO info{};
        CHECK(proxy_fn<GetDIBPointer_t>("WinGGetDIBPointer")(handle<HBITMAP>(33), &info) ==
              handle<void*>(0x1004));
        CHECK(std::string(last_call().function) == "WinGGetDIBPointer");
        CHECK(args_of(last_call()) == std::vector{u(handle<HBITMAP>(33)), u(&info)});
    }
    SECTION("WinGGetDIBColorTable") {
        RGBQUAD colors[4]{};
        CHECK(proxy_fn<GetDIBColorTable_t>("WinGGetDIBColorTable")(dc, 2, 4, colors) == 4u);
        CHECK(std::string(last_call().function) == "WinGGetDIBColorTable");
        CHECK(args_of(last_call()) == std::vector{u(dc), u(2u), u(4u), u(&colors[0])});
    }
    SECTION("WinGSetDIBColorTable") {
        const RGBQUAD colors[3]{};
        CHECK(proxy_fn<SetDIBColorTable_t>("WinGSetDIBColorTable")(dc, 1, 3, colors) == 3u);
        CHECK(std::string(last_call().function) == "WinGSetDIBColorTable");
        CHECK(args_of(last_call()) == std::vector{u(dc), u(1u), u(3u), u(&colors[0])});
    }
    SECTION("WinGCreateHalftonePalette") {
        CHECK(proxy_fn<CreateHalftonePalette_t>("WinGCreateHalftonePalette")() ==
              handle<HPALETTE>(0x1005));
        CHECK(std::string(last_call().function) == "WinGCreateHalftonePalette");
        CHECK(last_call().argc == 0);
    }
    SECTION("WinGCreateHalftoneBrush") {
        CHECK(proxy_fn<CreateHalftoneBrush_t>("WinGCreateHalftoneBrush")(dc, RGB(1, 2, 3), 2) ==
              handle<HBRUSH>(0x1006));
        CHECK(std::string(last_call().function) == "WinGCreateHalftoneBrush");
        CHECK(args_of(last_call()) == std::vector{u(dc), u(RGB(1, 2, 3)), u(2)});
    }
    SECTION("WinGBitBlt") {
        CHECK(proxy_fn<BitBlt_t>("WinGBitBlt")(dc, 1, 2, 3, 4, other_dc, 5, 6) == TRUE);
        CHECK(std::string(last_call().function) == "WinGBitBlt");
        CHECK(args_of(last_call()) ==
              std::vector{u(dc), u(1), u(2), u(3), u(4), u(other_dc), u(5), u(6)});
    }
    SECTION("WinGStretchBlt") {
        CHECK(proxy_fn<StretchBlt_t>("WinGStretchBlt")(dc, 1, 2, 3, 4, other_dc, 5, 6, 7, 8) ==
              TRUE);
        CHECK(std::string(last_call().function) == "WinGStretchBlt");
        CHECK(args_of(last_call()) ==
              std::vector{u(dc), u(1), u(2), u(3), u(4), u(other_dc), u(5), u(6), u(7), u(8)});
    }
}
