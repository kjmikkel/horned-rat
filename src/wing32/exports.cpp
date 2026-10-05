// The ten WinG exports. Undecorated names come from wing32.def.
#include "wing32/hooks.h"
#include "wing32/runtime.h"

#include <intrin.h>

#pragma intrinsic(_ReturnAddress)

using hr::wing32::original;

extern "C" {

HDC WINAPI WinGCreateDC() { return original().create_dc(); }

BOOL WINAPI WinGRecommendDIBFormat(BITMAPINFO* format) {
    return original().recommend_dib_format(format);
}

HBITMAP WINAPI WinGCreateBitmap(HDC dc, const BITMAPINFO* header, void** bits) {
    return original().create_bitmap(dc, header, bits);
}

void* WINAPI WinGGetDIBPointer(HBITMAP bitmap, BITMAPINFO* header) {
    return original().get_dib_pointer(bitmap, header);
}

UINT WINAPI WinGGetDIBColorTable(HDC dc, UINT start, UINT count, RGBQUAD* colors) {
    return original().get_dib_color_table(dc, start, count, colors);
}

UINT WINAPI WinGSetDIBColorTable(HDC dc, UINT start, UINT count, const RGBQUAD* colors) {
    return original().set_dib_color_table(dc, start, count, colors);
}

HPALETTE WINAPI WinGCreateHalftonePalette() { return original().create_halftone_palette(); }

HBRUSH WINAPI WinGCreateHalftoneBrush(HDC dc, COLORREF color, int dither_type) {
    return original().create_halftone_brush(dc, color, dither_type);
}

BOOL WINAPI WinGBitBlt(HDC dest, int x, int y, int width, int height, HDC src, int src_x,
                       int src_y) {
    hr::wing32::log_wing_bit_blt(_ReturnAddress(), dest, x, y, width, height);
    return original().bit_blt(dest, x, y, width, height, src, src_x, src_y);
}

BOOL WINAPI WinGStretchBlt(HDC dest, int x, int y, int width, int height, HDC src, int src_x,
                           int src_y, int src_width, int src_height) {
    hr::wing32::log_wing_stretch_blt(_ReturnAddress(), dest, x, y, width, height);
    return original().stretch_blt(dest, x, y, width, height, src, src_x, src_y, src_width,
                                  src_height);
}

} // extern "C"
