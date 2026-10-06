#include "core/sound_pause.h"

namespace hr::core {

void SoundPause::add(std::uintptr_t buffer, std::uintptr_t device) {
    const std::lock_guard lock(mutex_);
    live_[buffer] = device;
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

unsigned long SoundPause::release_device(std::uintptr_t device,
                                         const std::function<unsigned long()>& do_release) {
    const std::lock_guard lock(mutex_);
    const unsigned long references = do_release();
    if (references == 0) {
        for (auto it = live_.begin(); it != live_.end();) {
            if (it->second == device) {
                stopped_.erase(it->first);
                it = live_.erase(it);
            } else {
                ++it;
            }
        }
    }
    return references;
}

SoundPause::Counts SoundPause::pause(Control& control) {
    const std::lock_guard lock(mutex_);
    paused_ = true;
    Counts counts;
    for (const auto& [buffer, device] : live_) {
        ++counts.known;
        if (const auto looping = control.stop_if_playing(buffer)) {
            stopped_[buffer] = *looping;
            ++counts.stopped;
        }
    }
    return counts;
}

void SoundPause::resume(Control& control) {
    const std::lock_guard lock(mutex_);
    paused_ = false;
    for (const auto& [buffer, looping] : stopped_) {
        control.play(buffer, looping);
    }
    stopped_.clear();
}

bool SoundPause::allow_play(std::uintptr_t buffer, bool looping) {
    const std::lock_guard lock(mutex_);
    if (!paused_ || live_.count(buffer) == 0) {
        return true;
    }
    stopped_[buffer] = looping;
    return false;
}

void SoundPause::note_stop(std::uintptr_t buffer) {
    const std::lock_guard lock(mutex_);
    if (paused_) {
        stopped_.erase(buffer);
    }
}

std::optional<bool> SoundPause::reported_playing(std::uintptr_t buffer) {
    const std::lock_guard lock(mutex_);
    if (!paused_) {
        return std::nullopt;
    }
    const auto it = stopped_.find(buffer);
    return it == stopped_.end() ? std::nullopt : std::optional<bool>{it->second};
}

} // namespace hr::core
