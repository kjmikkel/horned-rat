#include "core/display_mode.h"

namespace hr::core {

bool defer_display_change(bool presenter_owns_display, std::uint32_t flags) {
    return presenter_owns_display && (flags & kCdsTest) == 0;
}

} // namespace hr::core