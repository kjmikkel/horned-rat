#pragma once

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

namespace hr::test {

// Directory of the running test executable (= build/<preset>/bin, where all DLLs land too).
inline std::filesystem::path bin_dir() {
    wchar_t buffer[MAX_PATH];
    const DWORD n = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    return std::filesystem::path(std::wstring(buffer, n)).parent_path();
}

// A fresh path in the temp directory; any existing file is removed.
inline std::filesystem::path temp_file(std::string_view name) {
    auto path = std::filesystem::temp_directory_path() / ("horned_rat_" + std::string(name));
    std::filesystem::remove(path);
    return path;
}

inline std::string read_file(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}

} // namespace hr::test
