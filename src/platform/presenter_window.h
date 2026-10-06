#pragma once

#include "core/geometry.h"

#include <windows.h>

namespace hr::platform {

// What the presenter window reports to its owner (all calls on the window's thread).
class PresenterInput {
  public:
    virtual ~PresenterInput() = default;
    virtual void on_mouse(UINT msg, WPARAM wparam, POINT client) = 0;
    virtual void on_key(UINT msg, WPARAM wparam, LPARAM lparam) = 0;
    virtual void on_close() = 0;
    // Every 15 ms while the user moves or resizes the window: Windows runs a modal loop then, so
    // the owner's own loop does not run.
    virtual void on_tick() = 0;
    // The cursor shown over the client area; nullptr = none (the owner draws its own).
    virtual HCURSOR cursor() = 0;
    // On every WM_ACTIVATEAPP: true while one of our process's windows is in the foreground
    // (the presenter or a game dialog), false once another application has it.
    virtual void on_app_activation(bool active) = 0;
};

inline constexpr core::Size kMinClient{320, 240};

// Our visible window: resizable, centred on the work area when created.
class PresenterWindow {
  public:
    // The client area is `client`, shrunk (keeping its aspect) to fit the work area of the
    // primary monitor, and at least kMinClient.
    PresenterWindow(PresenterInput& input, core::Size client, const char* title);
    ~PresenterWindow();
    PresenterWindow(const PresenterWindow&) = delete;
    PresenterWindow& operator=(const PresenterWindow&) = delete;

    HWND hwnd() const { return hwnd_; }
    void show();
    core::Size client_size() const;

    // Borderless: a popup covering the window's monitor. Leaving it restores the previous windowed
    // size, position and state. Alt+Enter in the window toggles it.
    void set_borderless(bool on);
    bool borderless() const { return borderless_; }

    // Shows input.cursor() at once if the real cursor is over the client area. Windows asks with
    // WM_SETCURSOR only when the mouse moves, so a changed cursor would otherwise wait for that.
    void refresh_cursor();

  private:
    static LRESULT CALLBACK proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam);
    void place(core::Size client);

    PresenterInput& input_;
    HWND hwnd_ = nullptr;
    WINDOWPLACEMENT windowed_{};
    bool borderless_ = false;
};

// True if the real cursor is over `window`'s client area and no other window covers that point.
// Any thread; coordinates in the calling thread's DPI context.
bool cursor_in_client(HWND window);

// True if `thread_id` is a running thread of this process.
bool thread_in_this_process(DWORD thread_id);

} // namespace hr::platform
