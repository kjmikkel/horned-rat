#include "core/audio.h"

namespace hr::core {

std::uint32_t with_global_focus(std::uint32_t buffer_flags) {
    if ((buffer_flags & kDsbcapsPrimaryBuffer) != 0) {
        return buffer_flags;
    }
    return buffer_flags | kDsbcapsGlobalFocus;
}

} // namespace hr::core
