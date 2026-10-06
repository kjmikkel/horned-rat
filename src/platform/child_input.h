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
// standard controls, A2 spec §5.2). WM_MOUSEWHEEL carries screen coordinates. Returns the window
// the message was posted to.
HWND post_mouse(HWND root, UINT msg, WPARAM wparam, POINT root_client);

// Windows sends WM_SETCURSOR to the window under the real cursor before each mouse message; under
// the presenter that is never a game window. This sends it asynchronously
// (SendMessageCallbackA) to the deepest child under `root_client`, with HTCLIENT and
// `mouse_msg`. Once root's thread has handled it, `done(window, WM_SETCURSOR, data, result)` runs
// on the calling thread while it pumps messages. Like Windows, it sends nothing while a window of
// root's thread has mouse capture. Returns the window it was sent to, else nullptr.
HWND send_set_cursor(HWND root, POINT root_client, UINT mouse_msg, SENDASYNCPROC done,
                     ULONG_PTR data);

// Posts a keyboard message to the focus window of root's thread, else to `root`.
void post_key(HWND root, UINT msg, WPARAM wparam, LPARAM lparam);

} // namespace hr::platform
