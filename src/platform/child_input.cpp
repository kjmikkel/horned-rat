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

} // namespace

ChildHit deepest_child_at(HWND root, POINT root_client) {
    ChildHit hit{root, root_client};
    for (;;) {
        const HWND child = ChildWindowFromPointEx(
            hit.window, hit.point, CWP_SKIPINVISIBLE | CWP_SKIPDISABLED | CWP_SKIPTRANSPARENT);
        if (child == nullptr || child == hit.window) {
            return hit;
        }
        MapWindowPoints(hit.window, child, &hit.point, 1);
        hit.window = child;
    }
}

void post_mouse(HWND root, UINT msg, WPARAM wparam, POINT root_client) {
    if (msg == WM_MOUSEWHEEL) {
        POINT screen = root_client;
        ClientToScreen(root, &screen);
        PostMessageA(deepest_child_at(root, root_client).window, msg, wparam, pack(screen));
        return;
    }
    if (const HWND captured = thread_window(root, true)) {
        POINT point = root_client;
        MapWindowPoints(root, captured, &point, 1);
        PostMessageA(captured, msg, wparam, pack(point));
        return;
    }
    const ChildHit hit = deepest_child_at(root, root_client);
    PostMessageA(hit.window, msg, wparam, pack(hit.point));
}

void post_key(HWND root, UINT msg, WPARAM wparam, LPARAM lparam) {
    const HWND focus = thread_window(root, false);
    PostMessageA(focus != nullptr ? focus : root, msg, wparam, lparam);
}

} // namespace hr::platform
