#include "core/luma.h"

#include <cmath>
#include <cstdio>

namespace hr::core {

bool BrightnessTrace::should_log(double t, double luma) {
    if (!running(t) || (logged_any_ && std::abs(luma - last_) < min_step_)) {
        return false;
    }
    logged_any_ = true;
    last_ = luma;
    return true;
}

double mean_luma(const std::uint32_t* pixels, int width, int height) {
    if (pixels == nullptr || width <= 0 || height <= 0) {
        return 0.0;
    }
    const long long count = static_cast<long long>(width) * height;
    double sum = 0.0;
    for (long long i = 0; i < count; ++i) {
        const std::uint32_t p = pixels[i];
        sum += 0.299 * ((p >> 16) & 0xFF) + 0.587 * ((p >> 8) & 0xFF) + 0.114 * (p & 0xFF);
    }
    return sum / static_cast<double>(count);
}

std::string luma_csv_row(std::string_view run, std::string_view mode, double t, double luma) {
    char numbers[64];
    std::snprintf(numbers, sizeof numbers, ",%.4f,%.2f", t, luma);
    std::string row;
    row.append(run).append(",").append(mode).append(numbers);
    return row;
}

} // namespace hr::core
