#include "wing32/runtime.h"

#include "core/game_modules.h"
#include "platform/log.h"
#include "platform/modules.h"
#include "platform/wing_forward.h"
#include "wing32/hooks.h"

#include <fstream>
#include <iterator>
#include <string>

namespace hr::wing32 {
namespace {

Settings g_settings;

HMODULE this_module() {
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       reinterpret_cast<LPCWSTR>(&this_module), &module);
    return module;
}

[[noreturn]] void fatal(const std::string& message) {
    platform::log("fatal: " + message);
    MessageBoxA(nullptr, message.c_str(), "horned-rat", MB_OK | MB_ICONERROR);
    ExitProcess(1);
}

std::string environment_variable(const char* name) {
    char buffer[64];
    const DWORD n = GetEnvironmentVariableA(name, buffer, sizeof buffer);
    return n > 0 && n < sizeof buffer ? std::string(buffer, n) : std::string();
}

std::string read_or_create_config(const std::filesystem::path& path) {
    if (std::ifstream in{path, std::ios::binary}) {
        return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    }
    std::ofstream(path, std::ios::binary) << core::default_config_ini();
    return {};
}

} // namespace

std::filesystem::path module_dir() {
    wchar_t buffer[MAX_PATH];
    const DWORD n = GetModuleFileNameW(this_module(), buffer, MAX_PATH);
    return std::filesystem::path(std::wstring(buffer, n)).parent_path();
}

void initialize() {
    const auto dir = module_dir();
    const auto loaded = core::load_config(read_or_create_config(dir / "horned-rat.ini"));
    g_settings.config = loaded.config;

    if (loaded.config.log) {
        static platform::Log log_file(dir / "horned-rat.log");
        platform::set_global_log(&log_file);
    }
    platform::log("horned-rat " HR_VERSION " loaded");
    for (const auto& warning : loaded.warnings) {
        platform::log("config: " + warning);
    }

    const auto exe = platform::module_path(nullptr);
    const auto slash = exe.find_last_of("\\/");
    g_settings.game_dir = slash == std::string::npos ? std::string() : exe.substr(0, slash);
    const auto disable = environment_variable("HORNED_RAT_DISABLE");
    g_settings.hooks_enabled = core::should_install_hooks(exe, loaded.config.enabled, disable);
    if (!g_settings.hooks_enabled) {
        platform::log("hooks disabled (host '" + exe +
                      "', enabled=" + (loaded.config.enabled ? "true" : "false") +
                      ", HORNED_RAT_DISABLE='" + disable + "')");
        return;
    }
    install_hooks();
}

const Settings& settings() { return g_settings; }

const platform::wing::Functions& original() {
    static const platform::wing::Functions functions = [] {
        auto result = platform::load_original_wing(module_dir() / "wing32_orig.dll");
        if (!result.functions) {
            fatal(result.error +
                  "\n\nThe original WinG library (wing32_orig.dll) is missing or damaged. Use GOG "
                  "Galaxy \"Verify / Repair\" on the game, then run tools/install.ps1 again.");
        }
        platform::log("original WinG loaded");
        install_hooks(); // catch modules whose imports were not yet bound at DllMain time
        return *result.functions;
    }();
    return functions;
}

} // namespace hr::wing32
