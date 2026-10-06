#pragma once

#include <cstdint>

namespace hr::core {

// Win32 values mirrored so core stays free of Windows headers. A platform test checks each one
// against <windows.h>.
namespace win {
inline constexpr unsigned kWmActivate = 0x0006;
inline constexpr unsigned kWmActivateApp = 0x001C;
inline constexpr unsigned kWmKeyDown = 0x0100;
inline constexpr unsigned kWmKeyUp = 0x0101;
inline constexpr unsigned kWmChar = 0x0102;
inline constexpr unsigned kWmSysKeyDown = 0x0104;
inline constexpr unsigned kWmSysKeyUp = 0x0105;
inline constexpr unsigned kWmSysChar = 0x0106;
inline constexpr unsigned kWmMouseMove = 0x0200;
inline constexpr unsigned kWmLButtonDown = 0x0201;
inline constexpr unsigned kWmLButtonUp = 0x0202;
inline constexpr unsigned kWmLButtonDblClk = 0x0203;
inline constexpr unsigned kWmRButtonDown = 0x0204;
inline constexpr unsigned kWmRButtonUp = 0x0205;
inline constexpr unsigned kWmRButtonDblClk = 0x0206;
inline constexpr unsigned kWmMButtonDown = 0x0207;
inline constexpr unsigned kWmMButtonUp = 0x0208;
inline constexpr unsigned kWmMButtonDblClk = 0x0209;
inline constexpr unsigned kWmMouseWheel = 0x020A;
inline constexpr unsigned kWmXButtonDown = 0x020B;
inline constexpr unsigned kWmXButtonUp = 0x020C;
inline constexpr unsigned kWmXButtonDblClk = 0x020D;
inline constexpr int kVkLButton = 0x01;
inline constexpr int kVkRButton = 0x02;
inline constexpr int kVkMButton = 0x04;
inline constexpr int kVkXButton1 = 0x05;
inline constexpr int kVkXButton2 = 0x06;
inline constexpr int kVkReturn = 0x0D;
inline constexpr std::uintptr_t kWaInactive = 0;
inline constexpr std::uintptr_t kWaActive = 1;
} // namespace win

enum class Forward { None, Mouse, Keyboard };

// Which presenter-window messages are passed on to the game (spec §5.2).
Forward classify(unsigned message, std::uintptr_t wparam);

bool is_mouse_button(int vk);

// GetAsyncKeyState is global: a physical mouse button is shown to the game only while the
// presenter is in the foreground and the real cursor is over its client area.
bool report_button(bool presenter_foreground, bool cursor_in_client);

// WM_ACTIVATE / WM_ACTIVATEAPP parameters rewritten so the game always believes it is active.
std::uintptr_t filter_activation(unsigned message, std::uintptr_t wparam);

} // namespace hr::core
