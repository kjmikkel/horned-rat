#include "platform/presenter_window.h"

#include <catch2/catch_test_macros.hpp>

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
    HCURSOR cursor() override { return nullptr; }
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
