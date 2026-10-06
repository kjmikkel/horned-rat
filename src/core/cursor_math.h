#pragma once

#include <cstdint>

namespace hr::core {

// The cursor the game wants after it has handled a forwarded WM_SETCURSOR (A2.3), with handles as
// integers. If the game called SetCursor meanwhile, that cursor is already recorded as `current`.
// Otherwise DefWindowProc applied the hit window's class cursor, if it has one. Otherwise nothing
// changed.
std::uintptr_t cursor_after_set_cursor(bool game_called_set_cursor, std::uintptr_t current,
                                       std::uintptr_t class_cursor);

} // namespace hr::core
