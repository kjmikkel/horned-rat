#include "core/config.h"

#include "core/ini.h"
#include "core/text.h"

#include <charconv>
#include <initializer_list>
#include <optional>
#include <utility>

namespace hr::core {
namespace {

std::optional<bool> parse_bool(std::string_view v) {
    const auto s = to_lower(v);
    if (s == "true" || s == "yes" || s == "on" || s == "1") {
        return true;
    }
    if (s == "false" || s == "no" || s == "off" || s == "0") {
        return false;
    }
    return std::nullopt;
}

bool parse_int(std::string_view s, int& out) {
    const auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    return ec == std::errc{} && end == s.data() + s.size();
}

std::optional<Size> parse_size(std::string_view v) {
    const auto x = v.find_first_of("xX");
    if (x == std::string_view::npos) {
        return std::nullopt;
    }
    Size size;
    if (!parse_int(v.substr(0, x), size.width) || !parse_int(v.substr(x + 1), size.height)) {
        return std::nullopt;
    }
    if (size.width < 320 || size.height < 240 || size.width > 16384 || size.height > 16384) {
        return std::nullopt;
    }
    return size;
}

template <typename E>
std::optional<E> parse_choice(std::string_view v,
                              std::initializer_list<std::pair<std::string_view, E>> choices) {
    const auto s = to_lower(v);
    for (const auto& [name, value] : choices) {
        if (s == name) {
            return value;
        }
    }
    return std::nullopt;
}

template <typename T> bool assign(T& target, const std::optional<T>& value) {
    if (!value) {
        return false;
    }
    target = *value;
    return true;
}

struct Field {
    std::string_view name; // "section.key"
    bool (*apply)(Config&, std::string_view);
};

constexpr Field kFields[] = {
    {"general.enabled",
     [](Config& c, std::string_view v) { return assign(c.enabled, parse_bool(v)); }},
    {"display.mode",
     [](Config& c, std::string_view v) {
         return assign(c.mode,
                       parse_choice<DisplayMode>(v, {{"windowed", DisplayMode::Windowed},
                                                     {"borderless", DisplayMode::Borderless}}));
     }},
    {"display.window_size",
     [](Config& c, std::string_view v) { return assign(c.window_size, parse_size(v)); }},
    {"display.scale",
     [](Config& c, std::string_view v) {
         return assign(c.scale,
                       parse_choice<ScaleMode>(v, {{"integer", ScaleMode::Integer},
                                                   {"sharp", ScaleMode::Sharp},
                                                   {"nearest-fit", ScaleMode::NearestFit}}));
     }},
    {"display.aspect",
     [](Config& c, std::string_view v) {
         return assign(c.aspect, parse_choice<AspectMode>(v, {{"4:3", AspectMode::Keep4x3},
                                                              {"stretch", AspectMode::Stretch}}));
     }},
    {"display.vsync", [](Config& c, std::string_view v) { return assign(c.vsync, parse_bool(v)); }},
    {"display.park",
     [](Config& c, std::string_view v) {
         return assign(c.park, parse_choice<ParkMode>(v, {{"offscreen", ParkMode::Offscreen},
                                                          {"underneath", ParkMode::Underneath}}));
     }},
    {"debug.log", [](Config& c, std::string_view v) { return assign(c.log, parse_bool(v)); }},
    {"debug.game_debug",
     [](Config& c, std::string_view v) { return assign(c.game_debug, parse_bool(v)); }},
};

const Field* find_field(std::string_view name) {
    for (const auto& field : kFields) {
        if (field.name == name) {
            return &field;
        }
    }
    return nullptr;
}

} // namespace

ConfigLoadResult load_config(std::string_view ini_text) {
    ConfigLoadResult result;
    const auto doc = parse_ini(ini_text);
    for (const auto& w : doc.warnings) {
        result.warnings.push_back("line " + std::to_string(w.line) + ": " + w.message);
    }
    for (const auto& [section, keys] : doc.sections) {
        for (const auto& [key, value] : keys) {
            const auto name = section.empty() ? key : section + "." + key;
            const auto* field = find_field(name);
            if (field == nullptr) {
                result.warnings.push_back("unknown key '" + name + "'");
            } else if (!field->apply(result.config, value)) {
                result.warnings.push_back(name + ": invalid value '" + value +
                                          "', using the default");
            }
        }
    }
    return result;
}

std::string default_config_ini() {
    return R"ini([general]
enabled      = true        ; false = pure passthrough to the original WinG
[display]
mode         = windowed    ; windowed | borderless
window_size  = 1280x960    ; initial client size in windowed mode
scale        = sharp       ; integer | sharp | nearest-fit
aspect       = 4:3         ; 4:3 | stretch
vsync        = true
park         = offscreen   ; offscreen | underneath (fallback: hidden under the presenter)
[debug]
log          = true        ; horned-rat.log
game_debug   = true        ; capture the game's own OutputDebugStringA output
)ini";
}

} // namespace hr::core
