#include "core/pause_clock.h"

namespace hr::core {

void PauseClock::hold(std::uint32_t now) {
    if (!held_) {
        held_ = true;
        held_since_ = now;
    }
}

void PauseClock::release(std::uint32_t now) {
    if (held_) {
        held_ = false;
        held_total_ += now - held_since_;
    }
}

std::uint32_t PauseClock::game_time(std::uint32_t now) const {
    return (held_ ? held_since_ : now) - held_total_;
}

} // namespace hr::core
