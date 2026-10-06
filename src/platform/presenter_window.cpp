#include "platform/presenter_window.h"

#include <windowsx.h>

namespace hr::platform {
namespace {
constexpr char kClassName[] = "HornedRatPresenter";
constexpr DWORD kStyle = WS_OVERLAPPEDWINDOW & ~(WS_THICKFRAME | WS_MAXIMIZEBOX);
} // namespace

PresenterWindow::PresenterWindow(PresenterInput& input, core::Size client, const char* title)
    : input_(input) {
    static const bool registered = [] {
        WNDCLASSA wc{};
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = &PresenterWindow::proc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.lpszClassName = kClassName;
        return RegisterClassA(&wc) != 0;
    }();
    (void)registered;
    RECT frame{0, 0, client.width, client.height};
    AdjustWindowRect(&frame, kStyle, FALSE);
    hwnd_ = CreateWindowExA(0, kClassName, title, kStyle, CW_USEDEFAULT, CW_USEDEFAULT,
                            frame.right - frame.left, frame.bottom - frame.top, nullptr, nullptr,
                            GetModuleHandleA(nullptr), this);
}

PresenterWindow::~PresenterWindow() {
    if (hwnd_ != nullptr) {
        DestroyWindow(hwnd_);
    }
}

void PresenterWindow::show() {
    ShowWindow(hwnd_, SW_SHOW);
    SetForegroundWindow(hwnd_);
}

LRESULT CALLBACK PresenterWindow::proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
    if (msg == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTA*>(lparam);
        SetWindowLongPtrA(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(create->lpCreateParams));
    }
    auto* const self = reinterpret_cast<PresenterWindow*>(GetWindowLongPtrA(window, GWLP_USERDATA));
    if (self == nullptr) {
        return DefWindowProcA(window, msg, wparam, lparam);
    }
    switch (msg) {
    case WM_MOUSEWHEEL: {
        POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        ScreenToClient(window, &point);
        self->input_.on_mouse(msg, wparam, point);
        return 0;
    }
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_LBUTTONDBLCLK:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_RBUTTONDBLCLK:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_MBUTTONDBLCLK:
    case WM_XBUTTONDOWN:
    case WM_XBUTTONUP:
    case WM_XBUTTONDBLCLK:
        self->input_.on_mouse(msg, wparam, POINT{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)});
        return (msg >= WM_XBUTTONDOWN && msg <= WM_XBUTTONDBLCLK) ? TRUE : 0;
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_CHAR:
        self->input_.on_key(msg, wparam, lparam);
        return 0;
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
    case WM_SYSCHAR:
        self->input_.on_key(msg, wparam, lparam);
        break; // DefWindowProc keeps Alt+F4 and the system menu working
    case WM_CLOSE:
        self->input_.on_close();
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT) {
            const HCURSOR cursor = self->input_.cursor();
            SetCursor(cursor != nullptr ? cursor : LoadCursorA(nullptr, MAKEINTRESOURCEA(32512)));
            return TRUE;
        }
        break;
    case WM_ERASEBKGND:
        return 1;
    default:
        break;
    }
    return DefWindowProcA(window, msg, wparam, lparam);
}

} // namespace hr::platform
