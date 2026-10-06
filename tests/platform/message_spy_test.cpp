#include "platform/message_spy.h"
#include "test_window.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <string>
#include <vector>

using hr::platform::MessageSpy;

namespace {
bool contains(const std::vector<std::string>& lines, const std::string& line) {
    return std::find(lines.begin(), lines.end(), line) != lines.end();
}
} // namespace

TEST_CASE("the message spy counts sent and posted messages per window class", "[message_spy]") {
    std::vector<std::string> lines;
    {
        MessageSpy spy(std::chrono::milliseconds(0), [&](const std::vector<std::string>& census) {
            lines.insert(lines.end(), census.begin(), census.end());
        });
        REQUIRE(spy.installed());
        const HWND window = hr::test::make_window(0, 0, 50, 50);
        hr::test::pump();
        lines.clear();

        SendMessageA(window, WM_SETCURSOR, reinterpret_cast<WPARAM>(window),
                     MAKELPARAM(HTCLIENT, WM_MOUSEMOVE));
        PostMessageA(window, WM_USER + 5, 0, 0);
        hr::test::pump();
        DestroyWindow(window);
    }
    CHECK(contains(lines, "HrRecordingWindow WM_SETCURSOR x1"));
    CHECK(contains(lines, "HrRecordingWindow WM_USER+5 x1"));
}

TEST_CASE("the census is handed over only once its period has passed", "[message_spy]") {
    int calls = 0;
    MessageSpy spy(std::chrono::hours(1), [&](const std::vector<std::string>&) { ++calls; });
    REQUIRE(spy.installed());
    const HWND window = hr::test::make_window(0, 0, 50, 50);
    SendMessageA(window, WM_USER + 1, 0, 0);
    hr::test::pump();
    DestroyWindow(window);
    CHECK(calls == 0);
}
