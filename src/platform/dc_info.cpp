#include "platform/dc_info.h"

#include <cstdio>

namespace hr::platform {

std::string describe_dc(HDC dc) {
    if (dc == nullptr) {
        return "null";
    }
    if (const HWND window = WindowFromDC(dc)) {
        char class_name[128] = {};
        GetClassNameA(window, class_name, sizeof class_name);
        char text[200];
        std::snprintf(text, sizeof text, "window 0x%p class '%s'", static_cast<void*>(window),
                      class_name);
        return text;
    }
    const DWORD type = GetObjectType(dc);
    if (type == OBJ_MEMDC) {
        return "memory";
    }
    return "type " + std::to_string(type);
}

} // namespace hr::platform
