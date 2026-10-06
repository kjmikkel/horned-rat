#include "platform/window_capture.h"

namespace hr::platform {

WindowCapture::~WindowCapture() { release(); }

void WindowCapture::release() {
    if (dc_ != nullptr) {
        SelectObject(dc_, previous_);
        DeleteDC(dc_);
    }
    if (bitmap_ != nullptr) {
        DeleteObject(bitmap_);
    }
    dc_ = nullptr;
    bitmap_ = nullptr;
    previous_ = nullptr;
    bits_ = nullptr;
    size_ = {};
}

bool WindowCapture::ensure(core::Size size) {
    if (dc_ != nullptr && size == size_) {
        return true;
    }
    release();
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof info.bmiHeader;
    info.bmiHeader.biWidth = size.width;
    info.bmiHeader.biHeight = -size.height; // top-down
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    dc_ = CreateCompatibleDC(nullptr);
    void* bits = nullptr;
    bitmap_ = CreateDIBSection(dc_, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (dc_ == nullptr || bitmap_ == nullptr) {
        release();
        return false;
    }
    previous_ = SelectObject(dc_, bitmap_);
    bits_ = static_cast<std::uint32_t*>(bits);
    size_ = size;
    return true;
}

bool WindowCapture::capture(HWND window, core::Rect source) {
    if (source.width <= 0 || source.height <= 0 || !ensure({source.width, source.height})) {
        return false;
    }
    const HDC window_dc = GetDC(window);
    if (window_dc == nullptr) {
        return false;
    }
    const BOOL copied =
        BitBlt(dc_, 0, 0, source.width, source.height, window_dc, source.x, source.y, SRCCOPY);
    ReleaseDC(window, window_dc);
    GdiFlush();
    return copied != FALSE;
}

} // namespace hr::platform
