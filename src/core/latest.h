#pragma once

#include <cstdint>
#include <mutex>
#include <utility>

namespace hr::core {

// The newest value from one thread for readers on others (Stage B1: the game thread's second
// render for the presenter). Readers copy the value only when it is newer than what they saw.
template <typename T> class Latest {
  public:
    void publish(T value) {
        const std::lock_guard lock(mutex_);
        value_ = std::move(value);
        ++sequence_;
    }

    // Copies the value into `out` and returns true if it is newer than `seen` (updated).
    bool take_newer(std::uint64_t& seen, T& out) const {
        const std::lock_guard lock(mutex_);
        if (sequence_ == seen) {
            return false;
        }
        out = value_;
        seen = sequence_;
        return true;
    }

  private:
    mutable std::mutex mutex_;
    T value_{};
    std::uint64_t sequence_ = 0;
};

} // namespace hr::core
