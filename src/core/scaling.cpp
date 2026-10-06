#include "core/scaling.h"

#include "core/viewport.h"

#include <algorithm>

namespace hr::core {

Rect dest_rect(Size client, Size content, ScaleMode scale, AspectMode aspect) {
    if (client.width <= 0 || client.height <= 0 || content.width <= 0 || content.height <= 0) {
        return {0, 0, 0, 0};
    }
    if (scale == ScaleMode::Integer) {
        int fx = std::max(1, client.width / content.width);
        int fy = std::max(1, client.height / content.height);
        if (aspect == AspectMode::Keep4x3) {
            fx = fy = std::min(fx, fy);
        }
        return centered_rect(client, {content.width * fx, content.height * fy});
    }
    if (aspect == AspectMode::Stretch) {
        return {0, 0, client.width, client.height};
    }
    // Width-limited when client.w / client.h <= content.w / content.h (compared without division).
    const long long width_limited = static_cast<long long>(client.width) * content.height;
    const long long height_limited = static_cast<long long>(client.height) * content.width;
    if (width_limited <= height_limited) {
        const int height = static_cast<int>(width_limited / content.width);
        return centered_rect(client, {client.width, height});
    }
    const int width = static_cast<int>(height_limited / content.height);
    return centered_rect(client, {width, client.height});
}

Size sharp_prescale(Size content, Rect dest) {
    if (content.width <= 0 || content.height <= 0) {
        return {1, 1};
    }
    return {std::max(1, dest.width / content.width), std::max(1, dest.height / content.height)};
}

Size fit_inside(Size wanted, Size available) {
    if (wanted.width <= available.width && wanted.height <= available.height) {
        return wanted;
    }
    if (wanted.width <= 0 || wanted.height <= 0) {
        return {std::min(wanted.width, available.width), std::min(wanted.height, available.height)};
    }
    const long long width_limited = static_cast<long long>(available.width) * wanted.height;
    const long long height_limited = static_cast<long long>(available.height) * wanted.width;
    if (width_limited <= height_limited) {
        return {available.width, static_cast<int>(width_limited / wanted.width)};
    }
    return {static_cast<int>(height_limited / wanted.height), available.height};
}

} // namespace hr::core
