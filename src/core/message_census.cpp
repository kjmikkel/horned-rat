#include "core/message_census.h"

#include <cstdio>

namespace hr::core {
namespace {

struct Known {
    unsigned message;
    const char* name;
};

// Win32 message numbers (winuser.h).
constexpr Known kKnown[] = {
    {0x0006, "WM_ACTIVATE"},
    {0x0007, "WM_SETFOCUS"},
    {0x0008, "WM_KILLFOCUS"},
    {0x000C, "WM_SETTEXT"},
    {0x000F, "WM_PAINT"},
    {0x0014, "WM_ERASEBKGND"},
    {0x0018, "WM_SHOWWINDOW"},
    {0x001C, "WM_ACTIVATEAPP"},
    {0x0020, "WM_SETCURSOR"},
    {0x0021, "WM_MOUSEACTIVATE"},
    {0x0046, "WM_WINDOWPOSCHANGING"},
    {0x0047, "WM_WINDOWPOSCHANGED"},
    {0x0084, "WM_NCHITTEST"},
    {0x0100, "WM_KEYDOWN"},
    {0x0101, "WM_KEYUP"},
    {0x0102, "WM_CHAR"},
    {0x0111, "WM_COMMAND"},
    {0x0113, "WM_TIMER"},
    {0x0200, "WM_MOUSEMOVE"},
    {0x0201, "WM_LBUTTONDOWN"},
    {0x0202, "WM_LBUTTONUP"},
    {0x0203, "WM_LBUTTONDBLCLK"},
    {0x0204, "WM_RBUTTONDOWN"},
    {0x0205, "WM_RBUTTONUP"},
    {0x020A, "WM_MOUSEWHEEL"},
    {0x0215, "WM_CAPTURECHANGED"},
    {0x02A1, "WM_MOUSEHOVER"},
    {0x02A3, "WM_MOUSELEAVE"},
};
constexpr unsigned kWmUser = 0x0400;
constexpr unsigned kWmApp = 0x8000;
constexpr unsigned kFirstRegistered = 0xC000;

} // namespace

std::string message_name(unsigned message) {
    for (const auto& known : kKnown) {
        if (known.message == message) {
            return known.name;
        }
    }
    if (message >= kWmUser && message < kWmApp) {
        return "WM_USER+" + std::to_string(message - kWmUser);
    }
    if (message >= kWmApp && message < kFirstRegistered) {
        return "WM_APP+" + std::to_string(message - kWmApp);
    }
    char text[16];
    std::snprintf(text, sizeof text, "0x%04X", message);
    return text;
}

void MessageCensus::add(std::string_view window_class, unsigned message) {
    ++counts_[{std::string(window_class), message}];
}

std::vector<std::string> MessageCensus::take() {
    std::vector<std::string> lines;
    lines.reserve(counts_.size());
    for (const auto& [key, count] : counts_) {
        lines.push_back(key.first + " " + message_name(key.second) + " x" + std::to_string(count));
    }
    counts_.clear();
    return lines;
}

} // namespace hr::core
