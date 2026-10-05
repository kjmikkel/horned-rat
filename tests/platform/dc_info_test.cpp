#include "platform/dc_info.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <windows.h>

using Catch::Matchers::ContainsSubstring;
using Catch::Matchers::StartsWith;

TEST_CASE("describe_dc tells window, memory and null DCs apart", "[dc_info]") {
    CHECK(hr::platform::describe_dc(nullptr) == "null");

    const HDC memory = CreateCompatibleDC(nullptr);
    REQUIRE(memory != nullptr);
    CHECK(hr::platform::describe_dc(memory) == "memory");
    DeleteDC(memory);

    WNDCLASSA wc{};
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "HrDcInfoTestWindow";
    RegisterClassA(&wc);
    const HWND window = CreateWindowExA(0, "HrDcInfoTestWindow", "", WS_OVERLAPPEDWINDOW, 0, 0, 100,
                                        100, nullptr, nullptr, wc.hInstance, nullptr);
    REQUIRE(window != nullptr);
    const HDC dc = GetDC(window);
    const auto description = hr::platform::describe_dc(dc);
    CHECK_THAT(description, StartsWith("window "));
    CHECK_THAT(description, ContainsSubstring("class 'HrDcInfoTestWindow'"));
    ReleaseDC(window, dc);
    DestroyWindow(window);
}
