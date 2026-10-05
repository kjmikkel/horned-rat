#include "wing32/runtime.h"

#include "platform/wing_forward.h"

#include <string>

namespace hr::wing32 {
namespace {

HMODULE this_module() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&this_module), &module);
    return module;
}

[[noreturn]] void fatal(const std::string& message) {
    MessageBoxA(nullptr, message.c_str(), "horned-rat", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

} // namespace

std::filesystem::path module_dir() {
    wchar_t buffer[MAX_PATH];
    const DWORD n = GetModuleFileNameW(this_module(), buffer, MAX_PATH);
    return std::filesystem::path(std::wstring(buffer, n)).parent_path();
}

const platform::wing::Functions& original() {
    static const platform::wing::Functions functions = [] {
        auto result = platform::load_original_wing(module_dir() / "wing32_orig.dll");
        if (!result.functions) {
            fatal(result.error +
                  "\n\nThe original WinG library (wing32_orig.dll) is missing or damaged. Use GOG "
                  "Galaxy \"Verify / Repair\" on the game, then run tools/install.ps1 again.");
        }
        return *result.functions;
    }();
    return functions;
}

} // namespace hr::wing32
