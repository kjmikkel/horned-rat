#include "core/frame_stats.h"

#include <algorithm>
#include <cstdio>

namespace hr::core {

void FrameStats::add(double capture_ms, double present_ms) {
    ++frames_;
    capture_ms_ += capture_ms;
    present_ms_ += present_ms;
    slowest_ms_ = std::max(slowest_ms_, capture_ms + present_ms);
}

std::string FrameStats::take_line(int seconds) {
    char line[160];
    if (frames_ == 0) {
        std::snprintf(line, sizeof line, "0 frames in %d s", seconds);
    } else {
        std::snprintf(line, sizeof line,
                      "%d frames in %d s, capture %.2f ms, present %.2f ms, slowest %.2f ms",
                      frames_, seconds, capture_ms_ / frames_, present_ms_ / frames_, slowest_ms_);
    }
    *this = FrameStats{};
    return line;
}

} // namespace hr::core
