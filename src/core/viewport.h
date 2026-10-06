#pragma once

#include "core/geometry.h"

namespace hr::core {

// Where the content picture goes inside the presenter client area at 1:1: centred, and allowed to
// extend beyond a client area that is too small.
Rect centered_rect(Size client, Size content);

// Maps a presenter-client point to a content point through `dest` (any size), flooring and
// clamping to the picture so points in the bars land on the nearest edge.
Point to_content(Point client_point, Rect dest, Size content);

// The screen position GetCursorPos must report so that the game's own ScreenToClient on its
// (hidden) main window yields `content_point` inside `content_rect`.
Point fake_cursor_screen(Point glue_client_origin, Rect content_rect, Point content_point);

// Top-left position that puts `window` fully inside `area`, moving it as little as possible; a
// window larger than the area is aligned to the area's top-left.
Point keep_inside(Rect window, Rect area);

} // namespace hr::core
