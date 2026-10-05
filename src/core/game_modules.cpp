#include "core/game_modules.h"

#include "core/text.h"

#include <string>

namespace hr::core {
namespace {

std::string normalize(std::string_view path) {
    auto s = to_lower(path);
    for (auto& c : s) {
        if (c == '/') {
            c = '\\';
        }
    }
    while (!s.empty() && s.back() == '\\') {
        s.pop_back();
    }
    return s;
}

std::string_view file_name(std::string_view path) {
    const auto slash = path.find_last_of("\\/");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

} // namespace

bool is_game_module(std::string_view module_path, std::string_view game_dir) {
    const auto module = normalize(module_path);
    const auto prefix = normalize(game_dir) + '\\';
    if (module.size() <= prefix.size() || module.compare(0, prefix.size(), prefix) != 0) {
        return false;
    }
    const auto name = file_name(module);
    return name != "wing32.dll" && name != "wing32_orig.dll";
}

bool should_install_hooks(std::string_view exe_path, bool config_enabled,
                          std::string_view disable_env) {
    return config_enabled && disable_env != "1" && to_lower(file_name(exe_path)) == "whshr.exe";
}

} // namespace hr::core
