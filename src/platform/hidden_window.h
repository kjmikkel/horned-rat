#pragma once

#include <windows.h>

namespace hr::platform {

// What hide_window changed, so unhide_window can undo it.
struct HiddenState {
    RECT window_rect{};
    LONG_PTR ex_style = 0;
    bool hidden = false;
};

// Makes `window` invisible (layered, alpha 0) and click-through, puts it at the bottom of the
// z-order and moves it (if needed) fully inside `area` (screen coordinates). It must stay
// on-screen: GDI clips drawing to the on-screen part of a window (A2 spec §2 correction).
HiddenState hide_window(HWND window, RECT area);

// The "never worse than vanilla" exit (spec §7): original extended style, window at (0,0), shown.
void unhide_window(HWND window, const HiddenState& state);

} // namespace hr::platform
