#include "platform/modules.h"

#include <cstdint>
#include <cstdio>

#include <psapi.h>

namespace hr::platform {

std::vector<LoadedModule> loaded_modules() {
    std::vector<HMODULE> handles(256);
    DWORD needed = 0;
    while (true) {
        const auto bytes = static_cast<DWORD>(handles.size() * sizeof(HMODULE));
        if (!K32EnumProcessModules(GetCurrentProcess(), handles.data(), bytes, &needed)) {
            return {};
        }
        if (needed <= bytes) {
            break;
        }
        handles.resize(needed / sizeof(HMODULE));
    }
    handles.resize(needed / sizeof(HMODULE));

    std::vector<LoadedModule> modules;
    modules.reserve(handles.size());
    for (const HMODULE handle : handles) {
        modules.push_back({handle, module_path(handle)});
    }
    return modules;
}

std::string module_path(HMODULE module) {
    char buffer[MAX_PATH];
    const DWORD n = GetModuleFileNameA(module, buffer, MAX_PATH);
    return std::string(buffer, n);
}

std::string module_file_name_from_address(const void* address) {
    HMODULE module = nullptr;
    if (address == nullptr || !GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                                      GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                  static_cast<LPCSTR>(address), &module)) {
        return "?";
    }
    const auto path = module_path(module);
    const auto slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

std::string describe_address(const void* address) {
    HMODULE module = nullptr;
    char text[MAX_PATH + 32];
    if (address != nullptr && GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                                     GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                                 static_cast<LPCSTR>(address), &module)) {
        const auto offset =
            reinterpret_cast<std::uintptr_t>(address) - reinterpret_cast<std::uintptr_t>(module);
        std::snprintf(text, sizeof text, "%s+0x%X", module_file_name_from_address(address).c_str(),
                      static_cast<unsigned>(offset));
    } else {
        std::snprintf(text, sizeof text, "0x%08X",
                      static_cast<unsigned>(reinterpret_cast<std::uintptr_t>(address)));
    }
    return text;
}

} // namespace hr::platform
