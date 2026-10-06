#include "platform/message_spy.h"

#include "platform/guard.h"

#include <cstdio>
#include <utility>

namespace hr::platform {
namespace {

MessageSpy* g_spy = nullptr; // only touched on the spied thread

LRESULT CALLBACK call_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && g_spy != nullptr) {
        const auto* sent = reinterpret_cast<const CWPSTRUCT*>(lparam);
        g_spy->count(sent->hwnd, sent->message);
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

// Also called for PeekMessage(PM_NOREMOVE); only removed messages are counted.
LRESULT CALLBACK get_proc(int code, WPARAM wparam, LPARAM lparam) {
    if (code == HC_ACTION && wparam == PM_REMOVE && g_spy != nullptr) {
        const auto* posted = reinterpret_cast<const MSG*>(lparam);
        if (posted->hwnd != nullptr) {
            g_spy->count(posted->hwnd, posted->message);
        }
    }
    return CallNextHookEx(nullptr, code, wparam, lparam);
}

} // namespace

MessageSpy::MessageSpy(std::chrono::milliseconds period, Sink sink)
    : period_(period), sink_(std::move(sink)), last_(std::chrono::steady_clock::now()) {
    g_spy = this;
    const DWORD thread = GetCurrentThreadId();
    call_hook_ = SetWindowsHookExA(WH_CALLWNDPROC, &call_proc, nullptr, thread);
    get_hook_ = SetWindowsHookExA(WH_GETMESSAGE, &get_proc, nullptr, thread);
}

MessageSpy::~MessageSpy() {
    if (call_hook_ != nullptr) {
        UnhookWindowsHookEx(call_hook_);
    }
    if (get_hook_ != nullptr) {
        UnhookWindowsHookEx(get_hook_);
    }
    g_spy = nullptr;
}

void MessageSpy::count(HWND window, UINT msg) {
    guarded(
        "message spy",
        [&] {
            char name[64] = {};
            if (GetClassNameA(window, name, sizeof name) == 0) {
                std::snprintf(name, sizeof name, "?");
            }
            census_.add(name, msg);
            const auto now = std::chrono::steady_clock::now();
            if (now - last_ >= period_) {
                last_ = now;
                sink_(census_.take());
            }
        },
        [] {});
}

} // namespace hr::platform
