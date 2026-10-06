#include "platform/presenter_window.h"

#include "core/scaling.h"
#include "core/viewport.h"

#include <windowsx.h>

#include <algorithm>

namespace hr::platform {
namespace {
constexpr char kClassName[] = "HornedRatPresenter";
constexpr DWORD kWindowedStyle = WS_OVERLAPPEDWINDOW;
constexpr UINT_PTR kTickTimer = 1;
constexpr UINT kTickMs = 15;
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
    hwnd_ = CreateWindowExA(0, kClassName, title, kWindowedStyle, CW_USEDEFAULT, CW_USEDEFAULT,
                            kMinClient.width, kMinClient.height, nullptr, nullptr,
                            GetModuleHandleA(nullptr), this);
    if (hwnd_ != nullptr) {
        place(client);
    }
}

// Sizes the window for `client` (fitted to the work area, at least kMinClient) and centres it.
// The frame is measured on the real window, so this is right at any DPI.
void PresenterWindow::place(core::Size client) {
    RECT outer{};
    RECT inner{};
    GetWindowRect(hwnd_, &outer);
    GetClientRect(hwnd_, &inner);
    const core::Size frame{(outer.right - outer.left) - inner.right,
                           (outer.bottom - outer.top) - inner.bottom};
    MONITORINFO monitor{};
    monitor.cbSize = sizeof monitor;
    GetMonitorInfoA(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTOPRIMARY), &monitor);
    const RECT& work = monitor.rcWork;
    const core::Size area{work.right - work.left, work.bottom - work.top};
    core::Size fitted =
        core::fit_inside(client, {area.width - frame.width, area.height - frame.height});
    fitted = {std::max(fitted.width, kMinClient.width), std::max(fitted.height, kMinClient.height)};
    const core::Size size{fitted.width + frame.width, fitted.height + frame.height};
    const core::Rect placed = core::centered_rect(area, size);
    SetWindowPos(hwnd_, nullptr, work.left + placed.x, work.top + placed.y, size.width, size.height,
                 SWP_NOZORDER | SWP_NOACTIVATE);
}

core::Size PresenterWindow::client_size() const {
    RECT client{};
    GetClientRect(hwnd_, &client);
    return {client.right, client.bottom};
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
    case WM_GETMINMAXINFO: {
        RECT min{0, 0, kMinClient.width, kMinClient.height};
        AdjustWindowRect(&min, static_cast<DWORD>(GetWindowLongPtrA(window, GWL_STYLE)), FALSE);
        auto* const info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize = POINT{min.right - min.left, min.bottom - min.top};
        return 0;
    }
    case WM_ENTERSIZEMOVE:
        SetTimer(window, kTickTimer, kTickMs, nullptr);
        return 0;
    case WM_EXITSIZEMOVE:
        KillTimer(window, kTickTimer);
        return 0;
    case WM_TIMER:
        if (wparam == kTickTimer) {
            self->input_.on_tick();
            return 0;
        }
        break;
    default:
        break;
    }
    return DefWindowProcA(window, msg, wparam, lparam);
}

} // namespace hr::platform
