#include "core/cursor_math.h"

#include <algorithm>

namespace hr::core {

std::uintptr_t cursor_after_set_cursor(bool game_called_set_cursor, std::uintptr_t current,
                                       std::uintptr_t class_cursor) {
    if (game_called_set_cursor || class_cursor == 0) {
        return current;
    }
    return class_cursor;
}

Size cursor_scale(Size content, Rect dest) {
    if (content.width <= 0 || content.height <= 0) {
        return {1, 1};
    }
    return {std::max(1, dest.width / content.width), std::max(1, dest.height / content.height)};
}

Rect cursor_rect(Point g, Size content, Rect dest, Size cursor, Point hotspot) {
    const Size factor = cursor_scale(content, dest);
    const auto edge = [](int game, int dest_size, int content_size) {
        return content_size > 0
                   ? static_cast<int>(static_cast<long long>(game) * dest_size / content_size)
                   : 0;
    };
    return {dest.x + edge(g.x, dest.width, content.width) - hotspot.x * factor.width,
            dest.y + edge(g.y, dest.height, content.height) - hotspot.y * factor.height,
            cursor.width * factor.width, cursor.height * factor.height};
}

} // namespace hr::core
