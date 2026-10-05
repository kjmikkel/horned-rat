#pragma once

#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace hr::core {

struct IniWarning {
    int line; // 1-based
    std::string message;
};

// Section and key names are lower-cased, values are trimmed, comments start at ';' or '#'
// (anywhere on a line), and a later duplicate key replaces an earlier one.
struct IniDocument {
    std::map<std::string, std::map<std::string, std::string, std::less<>>, std::less<>> sections;
    std::vector<IniWarning> warnings;

    const std::string* find(std::string_view section, std::string_view key) const;
};

IniDocument parse_ini(std::string_view text);

} // namespace hr::core
