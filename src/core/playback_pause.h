#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>

namespace hr::core {

// The game's MIDI players (MIDI streams, MCI sequencer devices), so background = pause can pause
// them while it holds the game: they play on in Windows' own threads (A2.4). Players are opaque
// ids; one instance per kind. Safe from any thread; Control runs with the lock held and must call
// the original Windows functions, never the hooks that report back here.
class PlaybackPause {
  public:
    class Control {
      public:
        virtual ~Control() = default;
        // Pauses `id` if it is playing; true if it did.
        virtual bool pause_if_playing(std::uintptr_t id) = 0;
        virtual void resume(std::uintptr_t id) = 0;
    };

    // `running`: whether the player plays now, nullopt if unknown (the pause then asks Control).
    void opened(std::uintptr_t id, std::optional<bool> running = std::nullopt);
    void closed(std::uintptr_t id);
    // The game started or stopped the player. A stop while paused cancels its resume.
    void set_running(std::uintptr_t id, bool running);

    // Pauses every player that may be playing; returns how many were paused.
    int pause(Control& control);
    void resume(Control& control);

  private:
    std::recursive_mutex mutex_;
    std::map<std::uintptr_t, std::optional<bool>> open_; // id -> running
    std::set<std::uintptr_t> paused_;
};

} // namespace hr::core
