#pragma once

#include "core/geometry.h"

#include <string>
#include <string_view>
#include <vector>

namespace hr::core {

enum class DisplayMode { Windowed, Borderless };
enum class ScaleMode { Integer, Sharp, NearestFit };
enum class AspectMode { Keep4x3, Stretch };
enum class ParkMode { Offscreen, Underneath };

// Mirrors horned-rat.ini (spec §6.1).
struct Config {
    bool enabled = true;
    DisplayMode mode = DisplayMode::Windowed;
    Size window_size{1280, 960};
    ScaleMode scale = ScaleMode::Sharp;
    AspectMode aspect = AspectMode::Keep4x3;
    bool vsync = true;
    ParkMode park = ParkMode::Offscreen;
    bool log = true;
    bool game_debug = true;

    friend bool operator==(const Config&, const Config&) = default;
};

struct ConfigLoadResult {
    Config config;
    std::vector<std::string> warnings; // never fatal: bad entries fall back to defaults
};

ConfigLoadResult load_config(std::string_view ini_text);

// The commented file written on first run.
std::string default_config_ini();

} // namespace hr::core
