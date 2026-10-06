#include "core/content_rect.h"

namespace hr::core {

Rect choose_content_rect(Size client, std::optional<Rect> child) {
    if (child && child->width > 0 && child->height > 0 && child->x >= 0 && child->y >= 0 &&
        child->x + child->width <= client.width && child->y + child->height <= client.height) {
        return *child;
    }
    return {0, 0, client.width, client.height};
}

} // namespace hr::core
