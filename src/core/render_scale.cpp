#include "core/render_scale.h"

#include <algorithm>

namespace hr::core {

int render_factor(int setting, Size content, Rect dest) {
    if (setting >= 1) {
        return std::min(setting, 4);
    }
    if (content.width <= 0 || content.height <= 0 || dest.width <= 0 || dest.height <= 0) {
        return 1;
    }
    const int kx = (dest.width + content.width - 1) / content.width;
    const int ky = (dest.height + content.height - 1) / content.height;
    return std::clamp(std::max(kx, ky), 1, 4);
}

void RenderBudget::add(double now_s, double ms, int factor) {
    if (window_start_ < 0.0) {
        window_start_ = now_s;
    }
    total_ms_ += ms;
    ++count_;
    if (now_s - window_start_ >= 2.0) {
        if (total_ms_ / count_ > 50.0) {
            cap_ = std::max(1, std::min(cap_, factor - 1));
        }
        window_start_ = now_s;
        total_ms_ = 0.0;
        count_ = 0;
    }
}

} // namespace hr::core
