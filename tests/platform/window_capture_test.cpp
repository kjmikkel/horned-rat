#include "platform/window_capture.h"
#include "test_window.h"

#include <catch2/catch_test_macros.hpp>

using hr::platform::WindowCapture;

namespace {
std::uint32_t rgb_at(const WindowCapture& capture, int x, int y) {
    return capture.pixels()[y * capture.size().width + x] & 0xFFFFFF;
}
} // namespace

TEST_CASE("window_capture copies a window's client pixels", "[window_capture]") {
    const HWND window = hr::test::make_window(100, 100, 200, 150);
    hr::test::pump();
    WindowCapture capture;
    REQUIRE(capture.capture(window, {0, 0, 200, 150}));
    CHECK(capture.size() == hr::core::Size{200, 150});
    CHECK(rgb_at(capture, 25, 35) == 0xFF0000);
    CHECK(rgb_at(capture, 5, 5) == 0x000000);

    REQUIRE(capture.capture(window, {20, 30, 10, 10})); // a sub-rectangle resizes the buffer
    CHECK(capture.size() == hr::core::Size{10, 10});
    CHECK(rgb_at(capture, 0, 0) == 0xFF0000);
    DestroyWindow(window);
}

TEST_CASE("window_capture rejects an empty source", "[window_capture]") {
    const HWND window = hr::test::make_window(100, 100, 50, 50);
    WindowCapture capture;
    CHECK_FALSE(capture.capture(window, {0, 0, 0, 10}));
    DestroyWindow(window);
}

TEST_CASE("window_capture sees live drawing of a window covered by another window",
          "[window_capture]") {
    const HWND window = hr::test::make_window(100, 100, 200, 150);
    const HWND cover = hr::test::make_window(50, 50, 400, 300);
    SetWindowPos(cover, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    hr::test::pump();

    // Draw like the game does: straight to the window DC, outside WM_PAINT.
    const HDC dc = GetDC(window);
    const RECT square{100, 50, 110, 60};
    const HBRUSH blue = CreateSolidBrush(RGB(0, 0, 255));
    FillRect(dc, &square, blue);
    DeleteObject(blue);
    ReleaseDC(window, dc);
    GdiFlush();

    WindowCapture capture;
    REQUIRE(capture.capture(window, {0, 0, 200, 150}));
    CHECK(rgb_at(capture, 105, 55) == 0x0000FF);
    CHECK(rgb_at(capture, 25, 35) == 0xFF0000);
    DestroyWindow(cover);
    DestroyWindow(window);
}
