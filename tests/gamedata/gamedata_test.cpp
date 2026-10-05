// Opt-in checks against the user's own game install. Skipped unless SOTHR_GAME_DIR points at the
// GOG game folder. Never runs in CI.
#include "platform/pe.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

namespace {

std::filesystem::path game_dir_or_skip() {
    wchar_t buffer[1024];
    const DWORD n = GetEnvironmentVariableW(L"SOTHR_GAME_DIR", buffer, 1024);
    if (n == 0 || n >= 1024) {
        SKIP("SOTHR_GAME_DIR is not set");
    }
    return std::filesystem::path(std::wstring(buffer, n));
}

std::filesystem::path bin_dir() {
    wchar_t buffer[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(std::wstring(buffer, n)).parent_path();
}

std::vector<std::string> sorted_exports(const std::filesystem::path& dll) {
    const HMODULE module = LoadLibraryExW(dll.c_str(), nullptr, DONT_RESOLVE_DLL_REFERENCES);
    REQUIRE(module != nullptr);
    auto names = hr::platform::pe::export_names(module);
    FreeLibrary(module);
    std::ranges::sort(names);
    return names;
}

} // namespace

TEST_CASE("our proxy exports exactly the names of the real WinG", "[gamedata]") {
    const auto warfb = game_dir_or_skip() / "WARFB";
    const auto real = std::filesystem::exists(warfb / "wing32_orig.dll") ? warfb / "wing32_orig.dll"
                                                                         : warfb / "wing32.dll";
    REQUIRE(std::filesystem::exists(real));
    CHECK(sorted_exports(bin_dir() / "wing32.dll") == sorted_exports(real));
}
