#include "platform/hidden_window.h"

#include "core/viewport.h"

namespace hr::platform {

HiddenState hide_window(HWND window, RECT area) {
    HiddenState state;
    GetWindowRect(window, &state.window_rect);
    state.ex_style = GetWindowLongPtrA(window, GWL_EXSTYLE);
    state.hidden = true;

    SetWindowLongPtrA(window, GWL_EXSTYLE, state.ex_style | WS_EX_LAYERED | WS_EX_TRANSPARENT);
    SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA);
    const RECT& r = state.window_rect;
    const core::Point pos =
        core::keep_inside({r.left, r.top, r.right - r.left, r.bottom - r.top},
                          {area.left, area.top, area.right - area.left, area.bottom - area.top});
    SetWindowPos(window, HWND_BOTTOM, pos.x, pos.y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
    return state;
}

void unhide_window(HWND window, const HiddenState& state) {
    if (!state.hidden) {
        return;
    }
    SetWindowLongPtrA(window, GWL_EXSTYLE, state.ex_style);
    SetWindowPos(window, nullptr, 0, 0, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

} // namespace hr::platform
