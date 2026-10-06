#include "core/sound_pause.h"

namespace hr::core {

void SoundPause::add(std::uintptr_t buffer) {
    const std::lock_guard lock(mutex_);
    live_.insert(buffer);
}

void SoundPause::remove(std::uintptr_t buffer) {
    const std::lock_guard lock(mutex_);
    live_.erase(buffer);
    stopped_.erase(buffer);
}

unsigned long SoundPause::release(std::uintptr_t buffer,
                                  const std::function<unsigned long()>& do_release) {
    const std::lock_guard lock(mutex_);
    const unsigned long references = do_release();
    if (references == 0) {
        live_.erase(buffer);
        stopped_.erase(buffer);
    }
    return references;
}

void SoundPause::pause(Control& control) {
    const std::lock_guard lock(mutex_);
    for (const std::uintptr_t buffer : live_) {
        if (const auto looping = control.stop_if_playing(buffer)) {
            stopped_[buffer] = *looping;
        }
    }
}

void SoundPause::resume(Control& control) {
    const std::lock_guard lock(mutex_);
    for (const auto& [buffer, looping] : stopped_) {
        control.play(buffer, looping);
    }
    stopped_.clear();
}

} // namespace hr::core
