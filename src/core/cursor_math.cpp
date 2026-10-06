#include "core/cursor_math.h"

namespace hr::core {

std::uintptr_t cursor_after_set_cursor(bool game_called_set_cursor, std::uintptr_t current,
                                       std::uintptr_t class_cursor) {
    if (game_called_set_cursor || class_cursor == 0) {
        return current;
    }
    return class_cursor;
}

} // namespace hr::core
