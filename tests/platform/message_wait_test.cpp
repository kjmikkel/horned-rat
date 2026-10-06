#include "platform/message_wait.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <atomic>
#include <chrono>
#include <thread>

namespace {
LRESULT CALLBACK answer_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_APP) {
        return 42;
    }
    return DefWindowProcA(window, msg, wparam, lparam);
}
} // namespace

TEST_CASE("a held thread still handles sent messages and returns once woken", "[message_wait]") {
    WNDCLASSA wc{};
    wc.lpfnWndProc = &answer_proc;
    wc.hInstance = GetModuleHandleA(nullptr);
    wc.lpszClassName = "HornedRatMessageWaitTest";
    RegisterClassA(&wc);

    const HANDLE wake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    REQUIRE(wake != nullptr);
    std::atomic<HWND> window{nullptr};
    std::atomic<bool> hold{true};
    std::atomic<bool> returned{false};
    std::thread held([&] {
        window = CreateWindowExA(0, wc.lpszClassName, "", 0, 0, 0, 10, 10, HWND_MESSAGE, nullptr,
                                 wc.hInstance, nullptr);
        hr::platform::wait_handling_sent_messages(wake, [&] { return hold.load(); });
        returned = true;
        DestroyWindow(window);
    });
    while (window.load() == nullptr) {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    DWORD_PTR answer = 0;
    CHECK(SendMessageTimeoutA(window, WM_APP, 0, 0, SMTO_ABORTIFHUNG, 2000, &answer) != 0);
    CHECK(answer == 42);
    CHECK_FALSE(returned.load());

    hold = false;
    SetEvent(wake);
    held.join();
    CHECK(returned.load());
    CloseHandle(wake);
}

TEST_CASE("the wait returns at once when there is nothing to wait for", "[message_wait]") {
    const HANDLE wake = CreateEventA(nullptr, FALSE, FALSE, nullptr);
    hr::platform::wait_handling_sent_messages(wake, [] { return false; });
    CloseHandle(wake);
}
