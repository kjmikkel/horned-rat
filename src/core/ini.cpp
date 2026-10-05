#include "core/ini.h"

#include "core/text.h"

namespace hr::core {
namespace {

std::string_view strip_comment(std::string_view line) {
    const auto pos = line.find_first_of(";#");
    return pos == std::string_view::npos ? line : line.substr(0, pos);
}

} // namespace

const std::string* IniDocument::find(std::string_view section, std::string_view key) const {
    const auto s = sections.find(section);
    if (s == sections.end()) {
        return nullptr;
    }
    const auto k = s->second.find(key);
    return k == s->second.end() ? nullptr : &k->second;
}

IniDocument parse_ini(std::string_view text) {
    IniDocument doc;
    std::string current;
    int line_number = 0;
    std::size_t start = 0;
    while (start <= text.size()) {
        const auto end = text.find('\n', start);
        const auto raw =
            text.substr(start, end == std::string_view::npos ? text.npos : end - start);
        ++line_number;
        const auto line = trim(strip_comment(raw));
        if (!line.empty()) {
            if (line.front() == '[') {
                if (line.back() != ']') {
                    doc.warnings.push_back({line_number, "unterminated section header"});
                } else {
                    current = to_lower(trim(line.substr(1, line.size() - 2)));
                    doc.sections[current];
                }
            } else if (const auto eq = line.find('='); eq != std::string_view::npos) {
                auto key = to_lower(trim(line.substr(0, eq)));
                if (key.empty()) {
                    doc.warnings.push_back({line_number, "missing key name before '='"});
                } else {
                    doc.sections[current][key] = std::string(trim(line.substr(eq + 1)));
                }
            } else {
                doc.warnings.push_back({line_number, "expected 'key = value'"});
            }
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return doc;
}

} // namespace hr::core
