#include "core/input_map.h"

namespace hr::core {

using namespace win;

Forward classify(unsigned message, std::uintptr_t wparam) {
    if (message >= kWmMouseMove && message <= kWmXButtonDblClk) {
        return Forward::Mouse;
    }
    switch (message) {
    case kWmSysKeyDown:
    case kWmSysKeyUp:
        return wparam == static_cast<std::uintptr_t>(kVkReturn) ? Forward::None : Forward::Keyboard;
    case kWmSysChar:
        return wparam == '\r' ? Forward::None : Forward::Keyboard;
    case kWmKeyDown:
    case kWmKeyUp:
    case kWmChar:
        return Forward::Keyboard;
    default:
        return Forward::None;
    }
}

bool toggles_borderless(unsigned message, std::uintptr_t wparam, std::intptr_t lparam) {
    constexpr std::intptr_t kAltDown = std::intptr_t{1} << 29;
    constexpr std::intptr_t kWasDown = std::intptr_t{1} << 30;
    return message == kWmSysKeyDown && wparam == static_cast<std::uintptr_t>(kVkReturn) &&
           (lparam & kAltDown) != 0 && (lparam & kWasDown) == 0;
}

bool skip_translate(bool presenter_active, unsigned message) {
    return presenter_active && (message == kWmKeyDown || message == kWmSysKeyDown);
}

bool is_mouse_button(int vk) {
    return vk == kVkLButton || vk == kVkRButton || vk == kVkMButton || vk == kVkXButton1 ||
           vk == kVkXButton2;
}

bool report_button(bool presenter_foreground, bool cursor_in_client) {
    return presenter_foreground && cursor_in_client;
}

std::uintptr_t filter_activation(unsigned message, std::uintptr_t wparam) {
    if (message == kWmActivate && (wparam & 0xFFFF) == kWaInactive) {
        return (wparam & ~std::uintptr_t{0xFFFF}) | kWaActive;
    }
    if (message == kWmActivateApp && wparam == 0) {
        return 1;
    }
    return wparam;
}

std::uintptr_t game_activation(Background background, unsigned message, std::uintptr_t wparam,
                               bool app_active) {
    if (background == Background::Run || app_active) {
        return filter_activation(message, wparam);
    }
    if (message == kWmActivate) {
        return (wparam & ~std::uintptr_t{0xFFFF}) | kWaInactive;
    }
    if (message == kWmActivateApp) {
        return 0;
    }
    return wparam;
}

bool hold_game(Background background, bool presenter_active, bool app_active, bool told_inactive,
               bool closing) {
    return background == Background::Pause && presenter_active && !app_active && told_inactive &&
           !closing;
}

} // namespace hr::core
