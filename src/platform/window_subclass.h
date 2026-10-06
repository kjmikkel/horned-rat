#pragma once

#include <windows.h>

#include <functional>

namespace hr::platform {

// Replaces a window's procedure (any thread of the owning process may do this). The handler
// receives the original procedure and decides whether and how to call it. Exceptions from the
// handler are logged and the original procedure handles the message instead.
class Subclass {
  public:
    using Handler = std::function<LRESULT(HWND, UINT, WPARAM, LPARAM, WNDPROC original)>;

    Subclass(HWND window, Handler handler);
    ~Subclass();
    Subclass(const Subclass&) = delete;
    Subclass& operator=(const Subclass&) = delete;

  private:
    static LRESULT CALLBACK thunk(HWND window, UINT msg, WPARAM wparam, LPARAM lparam);

    HWND window_;
    Handler handler_;
    WNDPROC original_ = nullptr;
};

} // namespace hr::platform
