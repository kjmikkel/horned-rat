#pragma once

#include <windows.h>

#include <string>
#include <vector>

namespace hr::platform::pe {

// Names in the export directory of a module mapped as an image, in name-table order.
std::vector<std::string> export_names(HMODULE module);

} // namespace hr::platform::pe
