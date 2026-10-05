#include "core/viewport.h"

#include <algorithm>

namespace hr::core {
namespace {

int map_axis(int p, int start, int length, int content) {
    if (length <= 0 || content <= 0) {
        return 0;
    }
    const long long scaled = static_cast<long long>(p - start) * content;
    long long q = scaled / length;
    if (scaled % length != 0 && scaled < 0) {
        --q; // floor, not truncation
    }
    return static_cast<int>(std::clamp<long long>(q, 0, content - 1));
}

} // namespace

Rect centered_rect(Size client, Size content) {
    return {(client.width - content.width) / 2, (client.height - content.height) / 2, content.width,
            content.height};
}

Point to_content(Point client_point, Rect dest, Size content) {
    return {map_axis(client_point.x, dest.x, dest.width, content.width),
            map_axis(client_point.y, dest.y, dest.height, content.height)};
}

Point fake_cursor_screen(Point glue_client_origin, Rect content_rect, Point content_point) {
    return {glue_client_origin.x + content_rect.x + content_point.x,
            glue_client_origin.y + content_rect.y + content_point.y};
}

} // namespace hr::core
