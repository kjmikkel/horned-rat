#include "core/playback_pause.h"

namespace hr::core {

void PlaybackPause::opened(std::uintptr_t id, std::optional<bool> running) {
    const std::lock_guard lock(mutex_);
    open_[id] = running;
}

void PlaybackPause::closed(std::uintptr_t id) {
    const std::lock_guard lock(mutex_);
    open_.erase(id);
    paused_.erase(id);
}

void PlaybackPause::set_running(std::uintptr_t id, bool running) {
    const std::lock_guard lock(mutex_);
    if (const auto it = open_.find(id); it != open_.end()) {
        it->second = running;
    }
    if (!running) {
        paused_.erase(id);
    }
}

int PlaybackPause::pause(Control& control) {
    const std::lock_guard lock(mutex_);
    int count = 0;
    for (const auto& [id, running] : open_) {
        if (running != false && control.pause_if_playing(id)) {
            paused_.insert(id);
            ++count;
        }
    }
    return count;
}

void PlaybackPause::resume(Control& control) {
    const std::lock_guard lock(mutex_);
    for (const std::uintptr_t id : paused_) {
        control.resume(id);
    }
    paused_.clear();
}

} // namespace hr::core
