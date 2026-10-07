#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace hr::platform {

// SHA-256 of a file's contents as lowercase hex; nullopt if it cannot be read.
std::optional<std::string> sha256_file(const std::filesystem::path& path);

} // namespace hr::platform
