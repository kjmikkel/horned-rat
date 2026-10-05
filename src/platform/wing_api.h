#pragma once

#include <windows.h>

#include <array>
#include <string_view>

// The WinG 1.0 API as exported by wing32.dll. Signatures follow the WinG SDK (wing.h).
namespace hr::platform::wing {

using BitBlt_t = BOOL(WINAPI*)(HDC dest, int x, int y, int width, int height, HDC src, int src_x,
                               int src_y);
using CreateBitmap_t = HBITMAP(WINAPI*)(HDC dc, const BITMAPINFO* header, void** bits);
using CreateDC_t = HDC(WINAPI*)();
using CreateHalftoneBrush_t = HBRUSH(WINAPI*)(HDC dc, COLORREF color, int dither_type);
using CreateHalftonePalette_t = HPALETTE(WINAPI*)();
using GetDIBColorTable_t = UINT(WINAPI*)(HDC dc, UINT start, UINT count, RGBQUAD* colors);
using GetDIBPointer_t = void*(WINAPI*)(HBITMAP bitmap, BITMAPINFO* header);
using RecommendDIBFormat_t = BOOL(WINAPI*)(BITMAPINFO* format);
using SetDIBColorTable_t = UINT(WINAPI*)(HDC dc, UINT start, UINT count, const RGBQUAD* colors);
using StretchBlt_t = BOOL(WINAPI*)(HDC dest, int x, int y, int width, int height, HDC src,
                                   int src_x, int src_y, int src_width, int src_height);

// The ten exports of wing32.dll, sorted.
inline constexpr std::array<std::string_view, 10> export_names{"WinGBitBlt",
                                                               "WinGCreateBitmap",
                                                               "WinGCreateDC",
                                                               "WinGCreateHalftoneBrush",
                                                               "WinGCreateHalftonePalette",
                                                               "WinGGetDIBColorTable",
                                                               "WinGGetDIBPointer",
                                                               "WinGRecommendDIBFormat",
                                                               "WinGSetDIBColorTable",
                                                               "WinGStretchBlt"};

struct Functions {
    BitBlt_t bit_blt = nullptr;
    CreateBitmap_t create_bitmap = nullptr;
    CreateDC_t create_dc = nullptr;
    CreateHalftoneBrush_t create_halftone_brush = nullptr;
    CreateHalftonePalette_t create_halftone_palette = nullptr;
    GetDIBColorTable_t get_dib_color_table = nullptr;
    GetDIBPointer_t get_dib_pointer = nullptr;
    RecommendDIBFormat_t recommend_dib_format = nullptr;
    SetDIBColorTable_t set_dib_color_table = nullptr;
    StretchBlt_t stretch_blt = nullptr;
};

} // namespace hr::platform::wing
