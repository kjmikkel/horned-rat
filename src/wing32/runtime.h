#pragma once

#include "core/config.h"
#include "platform/wing_api.h"

#include <filesystem>
#include <string>

namespace hr::wing32 {

struct Settings {
    core::Config config;
    bool hooks_enabled = false;
    std::string game_dir; // directory of the host executable (ANSI)
};

// Directory containing our wing32.dll (the game's WARFB folder when installed).
std::filesystem::path module_dir();

// Called once from DllMain(PROCESS_ATTACH): reads/writes horned-rat.ini, opens horned-rat.log,
// and installs hooks if this is the game. Must not call LoadLibrary (loader lock).
void initialize();

const Settings& settings();

// The original WinG, loaded from wing32_orig.dll on first use; re-applies the hooks once at that
// point (spec §3). If it cannot be loaded the user sees one message box and the process exits.
const platform::wing::Functions& original();

} // namespace hr::wing32
