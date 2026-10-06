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
    virtual HCURSOR cursor() = 0; // nullptr = standard arrow
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

  private:
    static LRESULT CALLBACK proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam);
    void place(core::Size client);

    PresenterInput& input_;
    HWND hwnd_ = nullptr;
};

} // namespace hr::platform
