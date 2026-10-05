#pragma once

#include "platform/wing_api.h"

#include <filesystem>

namespace hr::wing32 {

// Directory containing our wing32.dll (the game's WARFB folder when installed).
std::filesystem::path module_dir();

// The original WinG, loaded from wing32_orig.dll on first use. If it cannot be loaded the user
// sees one message box and the process exits: the game cannot run without it (spec §6.2).
const platform::wing::Functions& original();

} // namespace hr::wing32
