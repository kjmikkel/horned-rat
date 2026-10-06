#pragma once

#include <windows.h>

namespace hr::platform {

struct ChildHit {
    HWND window = nullptr;
    POINT point{}; // in `window` client coordinates
};

// The innermost visible, enabled, non-transparent window under `root_client` (root client
// coordinates), which may be `root` itself.
ChildHit deepest_child_at(HWND root, POINT root_client);

// Posts a mouse message as if the real cursor were at `root_client`: to the window holding mouse
// capture on root's thread if there is one, else to the deepest child (needed by USER32's
// standard controls, A2 spec §5.2). WM_MOUSEWHEEL carries screen coordinates.
void post_mouse(HWND root, UINT msg, WPARAM wparam, POINT root_client);

// Posts a keyboard message to the focus window of root's thread, else to `root`.
void post_key(HWND root, UINT msg, WPARAM wparam, LPARAM lparam);

} // namespace hr::platform
