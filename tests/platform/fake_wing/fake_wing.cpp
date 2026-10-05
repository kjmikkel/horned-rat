// A stand-in for the original wing32.dll: records each call and returns fixed values,
// so CI can test forwarding without any Mindscape/Microsoft binary.
#include "fake_wing.h"

#include <windows.h>

#include <cstring>
#include <initializer_list>
#include <type_traits>

namespace {

FakeWingCall g_last{};

void record(const char* function, std::initializer_list<std::uintptr_t> args) {
    g_last = {};
    strncpy_s(g_last.function, function, _TRUNCATE);
    for (const auto a : args) {
        g_last.args[g_last.argc++] = a;
    }
}

template <typename T> std::uintptr_t u(T v) {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<std::uintptr_t>(v);
    } else {
        return static_cast<std::uintptr_t>(v);
    }
}

template <typename H> H handle(std::uintptr_t value) { return reinterpret_cast<H>(value); }

} // namespace

extern "C" {

const FakeWingCall* WINAPI FakeWing_LastCall() { return &g_last; }

HDC WINAPI WinGCreateDC() {
    record("WinGCreateDC", {});
    return handle<HDC>(0x1001);
}

BOOL WINAPI WinGRecommendDIBFormat(BITMAPINFO* format) {
    record("WinGRecommendDIBFormat", {u(format)});
    return TRUE;
}

HBITMAP WINAPI WinGCreateBitmap(HDC dc, const BITMAPINFO* header, void** bits) {
    record("WinGCreateBitmap", {u(dc), u(header), u(bits)});
    if (bits != nullptr) {
        *bits = handle<void*>(0x1003);
    }
    return handle<HBITMAP>(0x1002);
}

void* WINAPI WinGGetDIBPointer(HBITMAP bitmap, BITMAPINFO* header) {
    record("WinGGetDIBPointer", {u(bitmap), u(header)});
    return handle<void*>(0x1004);
}

UINT WINAPI WinGGetDIBColorTable(HDC dc, UINT start, UINT count, RGBQUAD* colors) {
    record("WinGGetDIBColorTable", {u(dc), u(start), u(count), u(colors)});
    return count;
}

UINT WINAPI WinGSetDIBColorTable(HDC dc, UINT start, UINT count, const RGBQUAD* colors) {
    record("WinGSetDIBColorTable", {u(dc), u(start), u(count), u(colors)});
    return count;
}

HPALETTE WINAPI WinGCreateHalftonePalette() {
    record("WinGCreateHalftonePalette", {});
    return handle<HPALETTE>(0x1005);
}

HBRUSH WINAPI WinGCreateHalftoneBrush(HDC dc, COLORREF color, int dither_type) {
    record("WinGCreateHalftoneBrush", {u(dc), u(color), u(dither_type)});
    return handle<HBRUSH>(0x1006);
}

BOOL WINAPI WinGBitBlt(HDC dest, int x, int y, int width, int height, HDC src, int src_x,
                       int src_y) {
    record("WinGBitBlt", {u(dest), u(x), u(y), u(width), u(height), u(src), u(src_x), u(src_y)});
    return TRUE;
}

BOOL WINAPI WinGStretchBlt(HDC dest, int x, int y, int width, int height, HDC src, int src_x,
                           int src_y, int src_width, int src_height) {
    record("WinGStretchBlt", {u(dest), u(x), u(y), u(width), u(height), u(src), u(src_x), u(src_y),
                              u(src_width), u(src_height)});
    return TRUE;
}

} // extern "C"
