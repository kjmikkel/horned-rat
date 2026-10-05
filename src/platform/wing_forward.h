#pragma once

#include "platform/wing_api.h"

#include <filesystem>
#include <optional>
#include <string>

namespace hr::platform {

struct WingLoadResult {
    std::optional<wing::Functions> functions;
    std::string error; // empty on success
};

// Loads the original WinG DLL at `path` and resolves all ten exports. On success the module is
// intentionally never freed: it must live as long as the process.
WingLoadResult load_original_wing(const std::filesystem::path& path);

} // namespace hr::platform
