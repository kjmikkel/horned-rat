#pragma once

#include "core/geometry.h"

#include <windows.h>

#include <cstdint>

namespace hr::platform {

// Copies a region of a window's composed client area (via its window DC) into a 32-bit
// top-down DIB. Works for windows covered by others or hidden on-screen (A2 spec §2); NOT for
// windows off-screen, whose drawing GDI clips. Pixels are 0x??RRGGBB.
class WindowCapture {
  public:
    WindowCapture() = default;
    ~WindowCapture();
    WindowCapture(const WindowCapture&) = delete;
    WindowCapture& operator=(const WindowCapture&) = delete;

    // `source` is in client coordinates of `window`. Returns false for an empty source or when
    // the DC or the copy fails; the previous pixels are then kept.
    bool capture(HWND window, core::Rect source);

    const std::uint32_t* pixels() const { return bits_; }
    core::Size size() const { return size_; }

  private:
    bool ensure(core::Size size);
    void release();

    HDC dc_ = nullptr;
    HBITMAP bitmap_ = nullptr;
    HGDIOBJ previous_ = nullptr;
    std::uint32_t* bits_ = nullptr;
    core::Size size_{};
};

} // namespace hr::platform
