#pragma once

#include "core/geometry.h"

#include <optional>

namespace hr::core {

// The region of the game's main-window client area that holds the 640×480 picture: the
// BitmapScrn child when it is non-empty and fully inside the client, else the whole client.
Rect choose_content_rect(Size client, std::optional<Rect> child);

} // namespace hr::core
