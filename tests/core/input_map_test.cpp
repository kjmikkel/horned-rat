#include "core/input_map.h"

#include <catch2/catch_test_macros.hpp>

using namespace hr::core;
using namespace hr::core::win;

TEST_CASE("mouse messages are forwarded as mouse input", "[input_map]") {
    for (unsigned m = kWmMouseMove; m <= kWmXButtonDblClk; ++m) {
        INFO(m);
        CHECK(classify(m, 0) == Forward::Mouse);
    }
}

TEST_CASE("keyboard messages are forwarded, except our Alt+Enter", "[input_map]") {
    CHECK(classify(kWmKeyDown, 'A') == Forward::Keyboard);
    CHECK(classify(kWmKeyUp, 'A') == Forward::Keyboard);
    CHECK(classify(kWmChar, 'a') == Forward::Keyboard);
    CHECK(classify(kWmSysKeyDown, 'A') == Forward::Keyboard);
    CHECK(classify(kWmSysKeyUp, 'A') == Forward::Keyboard);
    CHECK(classify(kWmSysChar, 'a') == Forward::Keyboard);
    CHECK(classify(kWmSysKeyDown, kVkReturn) == Forward::None);
    CHECK(classify(kWmSysKeyUp, kVkReturn) == Forward::None);
    CHECK(classify(kWmSysChar, '\r') == Forward::None);
}

TEST_CASE("other messages are not forwarded", "[input_map]") {
    CHECK(classify(0x000F /* WM_PAINT */, 0) == Forward::None);
    CHECK(classify(kWmActivate, 0) == Forward::None);
}

TEST_CASE("mouse buttons are recognised by virtual-key code", "[input_map]") {
    for (int vk : {kVkLButton, kVkRButton, kVkMButton, kVkXButton1, kVkXButton2}) {
        CHECK(is_mouse_button(vk));
    }
    CHECK_FALSE(is_mouse_button(0x03)); // VK_CANCEL
    CHECK_FALSE(is_mouse_button('A'));
}

TEST_CASE("physical mouse buttons are reported only over the focused presenter", "[input_map]") {
    CHECK(report_button(true, true));
    CHECK_FALSE(report_button(true, false));
    CHECK_FALSE(report_button(false, true));
    CHECK_FALSE(report_button(false, false));
}

TEST_CASE("deactivation is reported to the game as activation", "[input_map]") {
    CHECK(filter_activation(kWmActivate, kWaInactive) == kWaActive);
    CHECK(filter_activation(kWmActivate, 0x00010000u) == 0x00010001u); // inactive + minimised
    CHECK(filter_activation(kWmActivate, 2) == 2u);                    // WA_CLICKACTIVE
    CHECK(filter_activation(kWmActivateApp, 0) == 1u);
    CHECK(filter_activation(kWmActivateApp, 1) == 1u);
    CHECK(filter_activation(kWmKeyDown, 0) == 0u);
}

TEST_CASE("only a fresh Alt+Enter press toggles borderless", "[input_map]") {
    using namespace hr::core::win;
    const std::intptr_t alt = std::intptr_t{1} << 29;
    const std::intptr_t repeat = std::intptr_t{1} << 30;
    CHECK(hr::core::toggles_borderless(kWmSysKeyDown, kVkReturn, alt));
    CHECK_FALSE(hr::core::toggles_borderless(kWmSysKeyDown, kVkReturn, alt | repeat));
    CHECK_FALSE(hr::core::toggles_borderless(kWmSysKeyUp, kVkReturn, alt));
    CHECK_FALSE(hr::core::toggles_borderless(kWmKeyDown, kVkReturn, 0));
    CHECK_FALSE(hr::core::toggles_borderless(kWmSysKeyDown, 'A', alt));
    CHECK_FALSE(hr::core::toggles_borderless(kWmSysKeyDown, kVkReturn, 0));
}

TEST_CASE("the game does not translate forwarded key presses while the presenter runs",
          "[input_map]") {
    // The presenter already forwards the WM_CHAR/WM_SYSCHAR it made from the real keyboard
    // state; a second translation in the game doubled every typed character (A2.3 smoke test).
    CHECK(skip_translate(true, kWmKeyDown));
    CHECK(skip_translate(true, kWmSysKeyDown));
    CHECK_FALSE(skip_translate(true, kWmKeyUp));
    CHECK_FALSE(skip_translate(true, kWmMouseMove));
    CHECK_FALSE(skip_translate(false, kWmKeyDown));
    CHECK_FALSE(skip_translate(false, kWmSysKeyDown));
}

TEST_CASE("background = run: the game is always told it is active", "[input_map]") {
    for (bool app_active : {true, false}) {
        INFO(app_active);
        CHECK(game_activation(Background::Run, kWmActivate, kWaInactive, app_active) == kWaActive);
        CHECK(game_activation(Background::Run, kWmActivateApp, 0, app_active) == 1u);
        CHECK(game_activation(Background::Run, kWmActivate, 2, app_active) == 2u);
    }
}

TEST_CASE("background = pause: the game follows the application's activation", "[input_map]") {
    // Active: deactivations caused inside our process (a game dialog) are hidden from the game.
    CHECK(game_activation(Background::Pause, kWmActivate, kWaInactive, true) == kWaActive);
    CHECK(game_activation(Background::Pause, kWmActivateApp, 0, true) == 1u);
    CHECK(game_activation(Background::Pause, kWmActivate, 2, true) == 2u); // WA_CLICKACTIVE
    // Inactive: every activation message says inactive; the minimised flag (high word) stays.
    CHECK(game_activation(Background::Pause, kWmActivate, kWaActive, false) == kWaInactive);
    CHECK(game_activation(Background::Pause, kWmActivate, 0x00010002u, false) == 0x00010000u);
    CHECK(game_activation(Background::Pause, kWmActivateApp, 1, false) == 0u);
}

TEST_CASE("other messages pass through game_activation unchanged", "[input_map]") {
    for (auto background : {Background::Run, Background::Pause}) {
        CHECK(game_activation(background, kWmKeyDown, 7, false) == 7u);
        CHECK(game_activation(background, kWmKeyDown, 7, true) == 7u);
    }
}
