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
    virtual HCURSOR cursor() = 0; // nullptr = standard arrow
};

// Our visible window. A2.1: fixed client size, no resizing.
class PresenterWindow {
  public:
    PresenterWindow(PresenterInput& input, core::Size client, const char* title);
    ~PresenterWindow();
    PresenterWindow(const PresenterWindow&) = delete;
    PresenterWindow& operator=(const PresenterWindow&) = delete;

    HWND hwnd() const { return hwnd_; }
    void show();

  private:
    static LRESULT CALLBACK proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam);

    PresenterInput& input_;
    HWND hwnd_ = nullptr;
};

} // namespace hr::platform
