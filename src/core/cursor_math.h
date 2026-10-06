#pragma once

#include "core/geometry.h"

#include <cstdint>

namespace hr::core {

// The cursor the game wants after it has handled a forwarded WM_SETCURSOR (A2.3), with handles as
// integers. If the game called SetCursor meanwhile, that cursor is already recorded as `current`.
// Otherwise DefWindowProc applied the hit window's class cursor, if it has one. Otherwise nothing
// changed.
std::uintptr_t cursor_after_set_cursor(bool game_called_set_cursor, std::uintptr_t current,
                                       std::uintptr_t class_cursor);

// The whole factor per axis by which the picture is enlarged: dest / content, at least 1.
Size cursor_scale(Size content, Rect dest);

// Where the scaled cursor goes in the presenter's client area (spec §5.3): `cursor` enlarged by
// cursor_scale, with its hotspot on the top-left corner of game pixel `g`. May extend past the
// client area.
Rect cursor_rect(Point g, Size content, Rect dest, Size cursor, Point hotspot);

} // namespace hr::core
