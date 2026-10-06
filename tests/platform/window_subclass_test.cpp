#include "core/input_map.h"
#include "platform/window_subclass.h"
#include "test_window.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

// The core mirror of Win32 constants must match <windows.h>.
static_assert(hr::core::win::kWmActivate ==
              static_cast<decltype(hr::core::win::kWmActivate)>(WM_ACTIVATE));
static_assert(hr::core::win::kWmActivateApp ==
              static_cast<decltype(hr::core::win::kWmActivateApp)>(WM_ACTIVATEAPP));
static_assert(hr::core::win::kWmKeyDown ==
              static_cast<decltype(hr::core::win::kWmKeyDown)>(WM_KEYDOWN));
static_assert(hr::core::win::kWmKeyUp == static_cast<decltype(hr::core::win::kWmKeyUp)>(WM_KEYUP));
static_assert(hr::core::win::kWmChar == static_cast<decltype(hr::core::win::kWmChar)>(WM_CHAR));
static_assert(hr::core::win::kWmSysKeyDown ==
              static_cast<decltype(hr::core::win::kWmSysKeyDown)>(WM_SYSKEYDOWN));
static_assert(hr::core::win::kWmSysKeyUp ==
              static_cast<decltype(hr::core::win::kWmSysKeyUp)>(WM_SYSKEYUP));
static_assert(hr::core::win::kWmSysChar ==
              static_cast<decltype(hr::core::win::kWmSysChar)>(WM_SYSCHAR));
static_assert(hr::core::win::kWmMouseMove ==
              static_cast<decltype(hr::core::win::kWmMouseMove)>(WM_MOUSEMOVE));
static_assert(hr::core::win::kWmLButtonDown ==
              static_cast<decltype(hr::core::win::kWmLButtonDown)>(WM_LBUTTONDOWN));
static_assert(hr::core::win::kWmMouseWheel ==
              static_cast<decltype(hr::core::win::kWmMouseWheel)>(WM_MOUSEWHEEL));
static_assert(hr::core::win::kWmXButtonDblClk ==
              static_cast<decltype(hr::core::win::kWmXButtonDblClk)>(WM_XBUTTONDBLCLK));
static_assert(hr::core::win::kVkLButton ==
              static_cast<decltype(hr::core::win::kVkLButton)>(VK_LBUTTON));
static_assert(hr::core::win::kVkRButton ==
              static_cast<decltype(hr::core::win::kVkRButton)>(VK_RBUTTON));
static_assert(hr::core::win::kVkMButton ==
              static_cast<decltype(hr::core::win::kVkMButton)>(VK_MBUTTON));
static_assert(hr::core::win::kVkXButton1 ==
              static_cast<decltype(hr::core::win::kVkXButton1)>(VK_XBUTTON1));
static_assert(hr::core::win::kVkXButton2 ==
              static_cast<decltype(hr::core::win::kVkXButton2)>(VK_XBUTTON2));
static_assert(hr::core::win::kVkReturn ==
              static_cast<decltype(hr::core::win::kVkReturn)>(VK_RETURN));
static_assert(hr::core::win::kWaInactive ==
              static_cast<decltype(hr::core::win::kWaInactive)>(WA_INACTIVE));
static_assert(hr::core::win::kWaActive ==
              static_cast<decltype(hr::core::win::kWaActive)>(WA_ACTIVE));

namespace {
WPARAM last_activate(HWND window) {
    const auto& messages = hr::test::received()[window];
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if (it->msg == WM_ACTIVATE) {
            return it->wparam;
        }
    }
    return static_cast<WPARAM>(-1);
}
} // namespace

TEST_CASE("a subclass can rewrite messages and is removed again", "[window_subclass]") {
    const HWND window = hr::test::make_window(100, 100, 50, 50);
    {
        hr::platform::Subclass subclass(
            window, [](HWND w, UINT m, WPARAM wp, LPARAM lp, WNDPROC original) {
                return CallWindowProcA(original, w, m, hr::core::filter_activation(m, wp), lp);
            });
        SendMessageA(window, WM_ACTIVATE, WA_INACTIVE, 0);
        CHECK(last_activate(window) == WA_ACTIVE);
    }
    SendMessageA(window, WM_ACTIVATE, WA_INACTIVE, 0);
    CHECK(last_activate(window) == WA_INACTIVE);
    DestroyWindow(window);
}

TEST_CASE("an exception in the handler falls back to the original procedure", "[window_subclass]") {
    const HWND window = hr::test::make_window(100, 100, 50, 50);
    hr::platform::Subclass subclass(window, [](HWND, UINT, WPARAM, LPARAM, WNDPROC) -> LRESULT {
        throw std::runtime_error("handler failed");
    });
    SendMessageA(window, WM_ACTIVATE, WA_CLICKACTIVE, 0);
    CHECK(last_activate(window) == WA_CLICKACTIVE);
    DestroyWindow(window);
}
