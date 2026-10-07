#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace hr::platform {

struct LoadedModule {
    HMODULE handle;
    std::string path; // ANSI, as returned by GetModuleFileNameA
};

std::vector<LoadedModule> loaded_modules();

// Full path of `module`; nullptr means the main executable.
std::string module_path(HMODULE module);

// File name of the module that contains `address`, e.g. "GAMEF.DLL"; "?" if none does.
std::string module_file_name_from_address(const void* address);

// "GAMEF.DLL+0x1A2B" for an address inside a module, else the bare "0x0000ABCD".
std::string describe_address(const void* address);

} // namespace hr::platform
