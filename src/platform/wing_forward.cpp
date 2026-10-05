#include "platform/wing_forward.h"

#include <type_traits>

namespace hr::platform {

WingLoadResult load_original_wing(const std::filesystem::path& path) {
    const HMODULE module = LoadLibraryW(path.c_str());
    if (module == nullptr) {
        const DWORD error = GetLastError();
        return {std::nullopt,
                "could not load " + path.string() + " (error " + std::to_string(error) + ")"};
    }

    wing::Functions f;
    std::string missing;
    const auto resolve = [&](auto& target, const char* name) {
        target = reinterpret_cast<std::remove_reference_t<decltype(target)>>(
            GetProcAddress(module, name));
        if (target == nullptr && missing.empty()) {
            missing = name;
        }
    };
    resolve(f.bit_blt, "WinGBitBlt");
    resolve(f.create_bitmap, "WinGCreateBitmap");
    resolve(f.create_dc, "WinGCreateDC");
    resolve(f.create_halftone_brush, "WinGCreateHalftoneBrush");
    resolve(f.create_halftone_palette, "WinGCreateHalftonePalette");
    resolve(f.get_dib_color_table, "WinGGetDIBColorTable");
    resolve(f.get_dib_pointer, "WinGGetDIBPointer");
    resolve(f.recommend_dib_format, "WinGRecommendDIBFormat");
    resolve(f.set_dib_color_table, "WinGSetDIBColorTable");
    resolve(f.stretch_blt, "WinGStretchBlt");

    if (!missing.empty()) {
        FreeLibrary(module);
        return {std::nullopt, path.string() + " does not export " + missing};
    }
    return {f, {}};
}

} // namespace hr::platform
