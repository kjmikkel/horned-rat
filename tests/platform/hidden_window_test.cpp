#include "platform/hidden_window.h"
#include "platform/window_capture.h"
#include "test_window.h"

#include <catch2/catch_test_macros.hpp>

using namespace hr::platform;

namespace {
RECT work_area() {
    RECT area{};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0);
    return area;
}
} // namespace

TEST_CASE("hide_window makes a window invisible, click-through and fully on-screen",
          "[hidden_window]") {
    const HWND window = hr::test::make_window(-150, 100, 200, 150);
    hr::test::pump();
    const RECT area = work_area();
    const HiddenState state = hide_window(window, area);
    CHECK(state.hidden);

    RECT r{};
    GetWindowRect(window, &r);
    CHECK(r.left >= area.left);
    CHECK(r.top >= area.top);
    CHECK(r.right <= area.right);
    CHECK(r.bottom <= area.bottom);
    CHECK(r.right - r.left == 200);

    const LONG_PTR ex = GetWindowLongPtrA(window, GWL_EXSTYLE);
    CHECK((ex & WS_EX_LAYERED) != 0);
    CHECK((ex & WS_EX_TRANSPARENT) != 0);
    BYTE alpha = 255;
    DWORD flags = 0;
    REQUIRE(GetLayeredWindowAttributes(window, nullptr, &alpha, &flags));
    CHECK(alpha == 0);

    unhide_window(window, state);
    CHECK((GetWindowLongPtrA(window, GWL_EXSTYLE) & (WS_EX_LAYERED | WS_EX_TRANSPARENT)) == 0);
    GetWindowRect(window, &r);
    CHECK(r.left == 0);
    CHECK(r.top == 0);
    CHECK(IsWindowVisible(window));
    DestroyWindow(window);
}

TEST_CASE("a hidden window under another window still delivers live drawing to the capture",
          "[hidden_window]") {
    const HWND window = hr::test::make_window(100, 100, 200, 150);
    hr::test::pump();
    const HiddenState state = hide_window(window, work_area());
    const HWND cover = hr::test::make_window(0, 0, 800, 600);
    hr::test::pump();

    const HDC dc = GetDC(window);
    const RECT square{100, 50, 110, 60};
    const HBRUSH blue = CreateSolidBrush(RGB(0, 0, 255));
    FillRect(dc, &square, blue);
    DeleteObject(blue);
    ReleaseDC(window, dc);
    GdiFlush();

    WindowCapture capture;
    REQUIRE(capture.capture(window, {0, 0, 200, 150}));
    CHECK((capture.pixels()[55 * 200 + 105] & 0xFFFFFF) == 0x0000FFu);
    unhide_window(window, state);
    DestroyWindow(cover);
    DestroyWindow(window);
}

TEST_CASE("unhide of a window that was never hidden does nothing", "[hidden_window]") {
    const HWND window = hr::test::make_window(100, 100, 50, 50);
    unhide_window(window, HiddenState{});
    RECT r{};
    GetWindowRect(window, &r);
    CHECK(r.left == 100);
    DestroyWindow(window);
}
