#include "platform/child_input.h"

namespace hr::platform {
namespace {

HWND thread_window(HWND root, bool capture) {
    GUITHREADINFO info{};
    info.cbSize = sizeof info;
    if (!GetGUIThreadInfo(GetWindowThreadProcessId(root, nullptr), &info)) {
        return nullptr;
    }
    return capture ? info.hwndCapture : info.hwndFocus;
}

LPARAM pack(POINT p) { return MAKELPARAM(static_cast<WORD>(p.x), static_cast<WORD>(p.y)); }

// The topmost visible, enabled, non-transparent child of `parent` whose rectangle contains `point`
// (parent client coordinates). Pure geometry on purpose: ChildWindowFromPointEx hit-tests the
// parent first and finds nothing under a layered alpha-0 parent such as the hidden game window.
HWND child_at(HWND parent, POINT point) {
    for (HWND child = GetWindow(parent, GW_CHILD); child != nullptr;
         child = GetWindow(child, GW_HWNDNEXT)) {
        if (!IsWindowVisible(child) || !IsWindowEnabled(child) ||
            (GetWindowLongPtrA(child, GWL_EXSTYLE) & WS_EX_TRANSPARENT) != 0) {
            continue;
        }
        RECT r{};
        GetWindowRect(child, &r);
        MapWindowPoints(HWND_DESKTOP, parent, reinterpret_cast<POINT*>(&r), 2);
        if (PtInRect(&r, point)) {
            return child;
        }
    }
    return nullptr;
}

} // namespace

ChildHit deepest_child_at(HWND root, POINT root_client) {
    ChildHit hit{root, root_client};
    for (;;) {
        const HWND child = child_at(hit.window, hit.point);
        if (child == nullptr) {
            return hit;
        }
        MapWindowPoints(hit.window, child, &hit.point, 1);
        hit.window = child;
    }
}

HWND post_mouse(HWND root, UINT msg, WPARAM wparam, POINT root_client) {
    if (msg == WM_MOUSEWHEEL) {
        POINT screen = root_client;
        ClientToScreen(root, &screen);
        const HWND target = deepest_child_at(root, root_client).window;
        PostMessageA(target, msg, wparam, pack(screen));
        return target;
    }
    if (const HWND captured = thread_window(root, true)) {
        POINT point = root_client;
        MapWindowPoints(root, captured, &point, 1);
        PostMessageA(captured, msg, wparam, pack(point));
        return captured;
    }
    const ChildHit hit = deepest_child_at(root, root_client);
    PostMessageA(hit.window, msg, wparam, pack(hit.point));
    return hit.window;
}

HWND send_set_cursor(HWND root, POINT root_client, UINT mouse_msg, SENDASYNCPROC done,
                     ULONG_PTR data) {
    if (thread_window(root, true) != nullptr) {
        return nullptr;
    }
    const HWND target = deepest_child_at(root, root_client).window;
    if (!SendMessageCallbackA(target, WM_SETCURSOR, reinterpret_cast<WPARAM>(target),
                              MAKELPARAM(HTCLIENT, mouse_msg), done, data)) {
        return nullptr;
    }
    return target;
}

void post_key(HWND root, UINT msg, WPARAM wparam, LPARAM lparam) {
    const HWND focus = thread_window(root, false);
    PostMessageA(focus != nullptr ? focus : root, msg, wparam, lparam);
}

} // namespace hr::platform
