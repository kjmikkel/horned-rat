#include "platform/window_subclass.h"

#include "platform/guard.h"

namespace hr::platform {
namespace {
constexpr char kProperty[] = "hr.subclass";
} // namespace

Subclass::Subclass(HWND window, Handler handler) : window_(window), handler_(std::move(handler)) {
    // Property first, so the thunk can always find us once it is installed.
    SetPropA(window_, kProperty, this);
    original_ = reinterpret_cast<WNDPROC>(
        SetWindowLongPtrA(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&Subclass::thunk)));
}

Subclass::~Subclass() {
    if (IsWindow(window_)) {
        SetWindowLongPtrA(window_, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(original_));
        RemovePropA(window_, kProperty);
    }
}

LRESULT CALLBACK Subclass::thunk(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
    auto* const self = static_cast<Subclass*>(GetPropA(window, kProperty));
    if (self == nullptr) {
        return DefWindowProcA(window, msg, wparam, lparam);
    }
    return guarded(
        "subclass", [&] { return self->handler_(window, msg, wparam, lparam, self->original_); },
        [&] { return CallWindowProcA(self->original_, window, msg, wparam, lparam); });
}

} // namespace hr::platform
