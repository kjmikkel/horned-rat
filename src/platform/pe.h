#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace hr::platform::pe {

// Names in the export directory of a module mapped as an image, in name-table order.
std::vector<std::string> export_names(HMODULE module);

// The IAT slot through which `module` calls `function` imported by name from `dll` (DLL name
// compared case-insensitively). nullptr if there is no such import, or if the module has no
// import name table to match names against.
void** find_import_slot(HMODULE module, const char* dll, const char* function);

} // namespace hr::platform::pe
