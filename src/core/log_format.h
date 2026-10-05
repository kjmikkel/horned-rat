#pragma once

#include <string>
#include <string_view>

namespace hr::core {

// "[    12.346] message\n": one record per line, whatever line breaks `message` contains.
std::string format_log_line(double seconds, std::string_view message);

} // namespace hr::core
