#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <set>

namespace hr::core {

// The game's live sound buffers, so they can be silenced while background = pause holds the game:
// a held game cannot stop its own looping effects or video sound. Buffers are opaque ids. Safe to
// use from any thread; Control is called with the lock held, so a buffer cannot be released
// (remove) while it is being stopped or played.
class SoundPause {
  public:
    class Control {
      public:
        virtual ~Control() = default;
        // Stops `buffer` if it is playing; returns whether it was looping, nullopt if silent.
        virtual std::optional<bool> stop_if_playing(std::uintptr_t buffer) = 0;
        virtual void play(std::uintptr_t buffer, bool looping) = 0;
    };

    void add(std::uintptr_t buffer);
    void remove(std::uintptr_t buffer);
    // Runs `do_release` (the buffer's own Release) under the lock and forgets the buffer when it
    // returns 0, so a pause can never touch a buffer that is being freed. Returns its result.
    unsigned long release(std::uintptr_t buffer, const std::function<unsigned long()>& do_release);
    struct Counts {
        int stopped = 0;
        int known = 0;
    };
    // Stops every playing buffer and remembers it; resume plays exactly those again.
    Counts pause(Control& control);
    void resume(Control& control);

  private:
    std::mutex mutex_;
    std::set<std::uintptr_t> live_;
    std::map<std::uintptr_t, bool> stopped_; // buffer -> was looping
};

} // namespace hr::core
