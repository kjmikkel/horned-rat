#include "platform/dpi_context.h"
#include "platform/presenter_window.h"
#include "test_window.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <vector>

namespace {

struct FakeInput final : hr::platform::PresenterInput {
    struct Mouse {
        UINT msg;
        POINT client;
    };
    std::vector<Mouse> mouse;
    std::vector<UINT> keys;
    bool closed = false;

    void on_mouse(UINT msg, WPARAM, POINT client) override { mouse.push_back({msg, client}); }
    void on_key(UINT msg, WPARAM, LPARAM) override { keys.push_back(msg); }
    void on_close() override { closed = true; }
    int ticks = 0;
    void on_tick() override { ++ticks; }
    HCURSOR shown = nullptr;
    HCURSOR cursor() override { return shown; }
};

} // namespace

TEST_CASE("the presenter window has exactly the requested client size", "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    REQUIRE(window.hwnd() != nullptr);
    RECT client{};
    GetClientRect(window.hwnd(), &client);
    CHECK(client.right == 640);
    CHECK(client.bottom == 480);
}

TEST_CASE("the presenter window reports mouse, keyboard and close", "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");

    SendMessageA(window.hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(10, 20));
    SendMessageA(window.hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(11, 21));
    REQUIRE(input.mouse.size() == 2);
    CHECK(input.mouse[0].msg == WM_MOUSEMOVE);
    CHECK(input.mouse[0].client.x == 10);
    CHECK(input.mouse[0].client.y == 20);
    CHECK(input.mouse[1].msg == WM_LBUTTONDOWN);

    SendMessageA(window.hwnd(), WM_KEYDOWN, 'A', 0);
    SendMessageA(window.hwnd(), WM_CHAR, 'a', 0);
    CHECK(input.keys == std::vector<UINT>{WM_KEYDOWN, WM_CHAR});

    SendMessageA(window.hwnd(), WM_CLOSE, 0, 0);
    CHECK(input.closed);
    CHECK(IsWindow(window.hwnd()));
}

TEST_CASE("mouse wheel positions arrive in client coordinates", "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    POINT screen{30, 40};
    ClientToScreen(window.hwnd(), &screen);
    SendMessageA(window.hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA),
                 MAKELPARAM(static_cast<WORD>(screen.x), static_cast<WORD>(screen.y)));
    REQUIRE(input.mouse.size() == 1);
    CHECK(input.mouse[0].client.x == 30);
    CHECK(input.mouse[0].client.y == 40);
}

namespace {
RECT work_area_of(HWND window) {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof monitor;
    GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor);
    return monitor.rcWork;
}
} // namespace

TEST_CASE("the presenter window is resizable and reports its client size", "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    const LONG_PTR style = GetWindowLongPtrA(window.hwnd(), GWL_STYLE);
    CHECK((style & WS_THICKFRAME) != 0);
    CHECK((style & WS_MAXIMIZEBOX) != 0);
    CHECK(window.client_size() == hr::core::Size{640, 480});

    RECT frame{0, 0, 700, 500};
    AdjustWindowRect(&frame, static_cast<DWORD>(style), FALSE);
    SetWindowPos(window.hwnd(), nullptr, 0, 0, frame.right - frame.left, frame.bottom - frame.top,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    CHECK(window.client_size() == hr::core::Size{700, 500});
}

TEST_CASE("the presenter window is centred on the work area", "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {400, 300}, "test");
    RECT r{};
    GetWindowRect(window.hwnd(), &r);
    const RECT work = work_area_of(window.hwnd());
    CHECK(std::abs((r.left + r.right) - (work.left + work.right)) <= 1);
    CHECK(std::abs((r.top + r.bottom) - (work.top + work.bottom)) <= 1);
}

TEST_CASE("a client size larger than the work area is shrunk to fit, keeping its aspect",
          "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {40000, 30000}, "test");
    RECT r{};
    GetWindowRect(window.hwnd(), &r);
    const RECT work = work_area_of(window.hwnd());
    CHECK(r.left >= work.left);
    CHECK(r.top >= work.top);
    CHECK(r.right <= work.right);
    CHECK(r.bottom <= work.bottom);
    const hr::core::Size client = window.client_size();
    CHECK(std::abs(client.width * 3 - client.height * 4) <= 4);
}

TEST_CASE("the presenter window cannot be made smaller than the minimum client size",
          "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    SetWindowPos(window.hwnd(), nullptr, 0, 0, 50, 50, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    CHECK(window.client_size().width >= hr::platform::kMinClient.width);
    CHECK(window.client_size().height >= hr::platform::kMinClient.height);
}

TEST_CASE("the presenter window ticks while it is being moved or resized", "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    SendMessageA(window.hwnd(), WM_ENTERSIZEMOVE, 0, 0);
    for (int i = 0; i < 200 && input.ticks == 0; ++i) {
        hr::test::pump();
        Sleep(5);
    }
    CHECK(input.ticks > 0);

    SendMessageA(window.hwnd(), WM_EXITSIZEMOVE, 0, 0);
    hr::test::pump();
    const int after_exit = input.ticks;
    for (int i = 0; i < 20; ++i) {
        hr::test::pump();
        Sleep(5);
    }
    CHECK(input.ticks == after_exit);
}
namespace {
RECT monitor_of(HWND window) {
    MONITORINFO monitor{};
    monitor.cbSize = sizeof monitor;
    GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor);
    return monitor.rcMonitor;
}
bool same(const RECT& a, const RECT& b) {
    return a.left == b.left && a.top == b.top && a.right == b.right && a.bottom == b.bottom;
}
} // namespace

TEST_CASE("borderless covers the monitor and leaving it restores the window",
          "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    window.show();
    RECT before{};
    GetWindowRect(window.hwnd(), &before);

    window.set_borderless(true);
    CHECK(window.borderless());
    RECT r{};
    GetWindowRect(window.hwnd(), &r);
    CHECK(same(r, monitor_of(window.hwnd())));
    CHECK((GetWindowLongPtrA(window.hwnd(), GWL_STYLE) & WS_CAPTION) == 0);

    window.set_borderless(false);
    CHECK_FALSE(window.borderless());
    GetWindowRect(window.hwnd(), &r);
    CHECK(same(r, before));
    CHECK((GetWindowLongPtrA(window.hwnd(), GWL_STYLE) & WS_CAPTION) == WS_CAPTION);
}

TEST_CASE("Alt+Enter toggles borderless and is not passed on as a key", "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    window.show();
    const LPARAM alt = LPARAM{1} << 29;
    SendMessageA(window.hwnd(), WM_SYSKEYDOWN, VK_RETURN, alt);
    CHECK(window.borderless());
    SendMessageA(window.hwnd(), WM_SYSKEYDOWN, VK_RETURN, alt | (LPARAM{1} << 30));
    CHECK(window.borderless()); // auto-repeat does not toggle back
    SendMessageA(window.hwnd(), WM_SYSKEYDOWN, VK_RETURN, alt);
    CHECK_FALSE(window.borderless());
    CHECK(input.keys == std::vector<UINT>{WM_SYSKEYDOWN}); // only the auto-repeat, which a2 drops
}

TEST_CASE("the presenter window is per-monitor DPI aware even from a DPI-unaware thread",
          "[presenter_window]") {
    hr::platform::ScopedDpiContext unaware(DPI_AWARENESS_CONTEXT_UNAWARE);
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    CHECK(GetAwarenessFromDpiAwarenessContext(GetWindowDpiAwarenessContext(window.hwnd())) ==
          DPI_AWARENESS_PER_MONITOR_AWARE);
    CHECK(GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext()) ==
          DPI_AWARENESS_UNAWARE);
    CHECK(window.client_size() == hr::core::Size{640, 480});
}

namespace {
HCURSOR system_cursor(int id) { return LoadCursorA(nullptr, MAKEINTRESOURCEA(id)); }
constexpr int kArrow = 32512; // IDC_ARROW
constexpr int kCross = 32515; // IDC_CROSS
} // namespace

TEST_CASE("over the client area the window shows the input's cursor; nullptr shows none",
          "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {640, 480}, "test");
    const auto ask = [&] {
        SendMessageA(window.hwnd(), WM_SETCURSOR, reinterpret_cast<WPARAM>(window.hwnd()),
                     MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
    };
    input.shown = system_cursor(kCross);
    ask();
    CHECK(GetCursor() == system_cursor(kCross));
    input.shown = nullptr;
    ask();
    CHECK(GetCursor() == nullptr);
    SetCursor(system_cursor(kArrow));
}

TEST_CASE("cursor_in_client and refresh_cursor follow the real cursor", "[presenter_window]") {
    FakeInput input;
    hr::platform::PresenterWindow window(input, {320, 240}, "test");
    window.show();
    SetWindowPos(window.hwnd(), HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    POINT saved{};
    GetCursorPos(&saved);
    POINT inside{160, 120};
    ClientToScreen(window.hwnd(), &inside);
    RECT frame{};
    GetWindowRect(window.hwnd(), &frame);
    const POINT outside{frame.left - 10, frame.top - 10};

    POINT now{};
    if (!SetCursorPos(inside.x, inside.y) || !GetCursorPos(&now) || now.x != inside.x ||
        now.y != inside.y) {
        SetCursorPos(saved.x, saved.y);
        SKIP("the real cursor cannot be moved on this desktop");
    }
    CHECK(hr::platform::cursor_in_client(window.hwnd()));
    input.shown = system_cursor(kCross);
    SetCursor(system_cursor(kArrow));
    window.refresh_cursor();
    CHECK(GetCursor() == system_cursor(kCross));

    SetCursorPos(outside.x, outside.y);
    CHECK_FALSE(hr::platform::cursor_in_client(window.hwnd()));
    SetCursor(system_cursor(kArrow));
    window.refresh_cursor();
    CHECK(GetCursor() == system_cursor(kArrow));
    SetCursorPos(saved.x, saved.y);
}
