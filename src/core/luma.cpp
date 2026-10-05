#include "core/luma.h"

namespace hr::core {

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

} // namespace hr::core
