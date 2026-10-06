#pragma once

#include <windows.h>

#include <map>
#include <vector>

namespace hr::test {

struct ReceivedMessage {
    UINT msg;
    WPARAM wparam;
    LPARAM lparam;
};

// Every message received by windows made with make_window (test thread only).
inline std::map<HWND, std::vector<ReceivedMessage>>& received() {
    static std::map<HWND, std::vector<ReceivedMessage>> messages;
    return messages;
}

inline LRESULT CALLBACK recording_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
    received()[window].push_back({msg, wparam, lparam});
    if (msg == WM_PAINT) {
        PAINTSTRUCT ps;
        const HDC dc = BeginPaint(window, &ps);
        RECT client{};
        GetClientRect(window, &client);
        FillRect(dc, &client, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        const RECT square{20, 30, 30, 40};
        const HBRUSH red = CreateSolidBrush(RGB(255, 0, 0));
        FillRect(dc, &square, red);
        DeleteObject(red);
        EndPaint(window, &ps);
        return 0;
    }
    return DefWindowProcA(window, msg, wparam, lparam);
}

// A test window, visible unless `visible` is false: top-level popup when `parent` is null, else a
// child. Paints black with a red 10×10 square at client (20,30).
inline HWND make_window(int x, int y, int width, int height, HWND parent = nullptr,
                        bool visible = true) {
    static const bool registered = [] {
        WNDCLASSA wc{};
        wc.lpfnWndProc = recording_proc;
        wc.hInstance = GetModuleHandleA(nullptr);
        wc.hCursor = LoadCursorA(nullptr, MAKEINTRESOURCEA(32512)); // IDC_ARROW
        wc.lpszClassName = "HrRecordingWindow";
        return RegisterClassA(&wc) != 0;
    }();
    (void)registered;
    const DWORD style =
        (parent != nullptr ? WS_CHILD : WS_POPUP) | (visible ? WS_VISIBLE : DWORD{0});
    const HWND window = CreateWindowExA(0, "HrRecordingWindow", "", style, x, y, width, height,
                                        parent, nullptr, GetModuleHandleA(nullptr), nullptr);
    UpdateWindow(window);
    return window;
}

inline void pump() {
    MSG msg;
    while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

} // namespace hr::test
