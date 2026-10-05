#include "core/log_format.h"

#include <cstdio>

namespace hr::core {

std::string format_log_line(double seconds, std::string_view message) {
    while (!message.empty() && (message.back() == '\n' || message.back() == '\r')) {
        message.remove_suffix(1);
    }
    char stamp[32];
    std::snprintf(stamp, sizeof stamp, "[%9.3f] ", seconds);
    std::string line = stamp;
    for (const char c : message) {
        line += (c == '\n' || c == '\r') ? ' ' : c;
    }
    line += '\n';
    return line;
}

} // namespace hr::core
