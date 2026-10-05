#pragma once

#include <string_view>

namespace hr::core {

// Windows path semantics ('\\' or '/', case-insensitive), implemented without Windows headers.
// A game module lives in `game_dir` or a subfolder and is not wing32.dll / wing32_orig.dll.
bool is_game_module(std::string_view module_path, std::string_view game_dir);

// Hooks are installed only into WHSHR.EXE, when enabled in the config and not disabled by the
// HORNED_RAT_DISABLE=1 environment variable (spec §3, §6.2).
bool should_install_hooks(std::string_view exe_path, bool config_enabled,
                          std::string_view disable_env);

} // namespace hr::core
