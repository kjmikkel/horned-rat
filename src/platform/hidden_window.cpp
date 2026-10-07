#include "platform/hidden_window.h"

#include "core/viewport.h"

#include <dwmapi.h>

namespace hr::platform {

HiddenState hide_window(HWND window, RECT area) {
    HiddenState state;
    GetWindowRect(window, &state.window_rect);
    state.ex_style = GetWindowLongPtrA(window, GWL_EXSTYLE);
    state.hidden = true;

    // A tool window has no taskbar button and no alt-tab entry, so the user cannot switch to the
    // invisible window (A2.4 run B).
    SetWindowLongPtrA(window, GWL_EXSTYLE,
                      (state.ex_style & ~WS_EX_APPWINDOW) | WS_EX_LAYERED | WS_EX_TRANSPARENT |
                          WS_EX_TOOLWINDOW);
    SetLayeredWindowAttributes(window, 0, 0, LWA_ALPHA);
    // Windows animates minimise/maximise/restore even for an invisible window; the game restores
    // its maximised window on its first focus loss, which showed as a small ghost (A2.4).
    const BOOL off = TRUE;
    state.animations_off =
        SUCCEEDED(DwmSetWindowAttribute(window, DWMWA_TRANSITIONS_FORCEDISABLED, &off, sizeof off));
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
    if (state.animations_off) {
        const BOOL off = FALSE;
        DwmSetWindowAttribute(window, DWMWA_TRANSITIONS_FORCEDISABLED, &off, sizeof off);
    }
    SetWindowPos(window, nullptr, 0, 0, 0, 0,
                 SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

} // namespace hr::platform
