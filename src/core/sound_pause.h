#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>

namespace hr::core {

// The game's live sound buffers, so they can be silenced while background = pause holds the game:
// a held game cannot stop its own looping effects or video sound. Buffers and their DirectSound
// objects ("devices") are opaque ids. Safe to use from any thread. Control is called with the lock
// held, so a buffer cannot be freed while it is being stopped or played; Control must call the
// original DirectSound functions, never the hooks that call back into this class.
class SoundPause {
  public:
    class Control {
      public:
        virtual ~Control() = default;
        // Stops `buffer` if it is playing; returns whether it was looping, nullopt if silent.
        virtual std::optional<bool> stop_if_playing(std::uintptr_t buffer) = 0;
        virtual void play(std::uintptr_t buffer, bool looping) = 0;
    };

    void add(std::uintptr_t buffer, std::uintptr_t device);

    // Run the buffer's / device's own Release under the lock and forget what it freed when it
    // returns 0. Releasing a device frees all its buffers without their own Release (A2.4 crash).
    unsigned long release(std::uintptr_t buffer, const std::function<unsigned long()>& do_release);
    unsigned long release_device(std::uintptr_t device,
                                 const std::function<unsigned long()>& do_release);

    struct Counts {
        int stopped = 0;
        int known = 0;
    };
    // Stops every playing buffer and remembers it; resume plays exactly those again.
    Counts pause(Control& control);
    void resume(Control& control);

    // The game's sound thread runs on while its main thread is held. While paused, its Play on a
    // known buffer is held back until resume (false = do not call DirectSound), its Stop cancels
    // that buffer's restart, and a held-back or stopped buffer still reports itself as playing
    // (the looping flag; nullopt = ask DirectSound).
    bool allow_play(std::uintptr_t buffer, bool looping);
    void note_stop(std::uintptr_t buffer);
    std::optional<bool> reported_playing(std::uintptr_t buffer);

  private:
    // Recursive: DirectSound's own Release may call hooked buffer methods (Stop) on this thread.
    std::recursive_mutex mutex_;
    bool paused_ = false;
    std::map<std::uintptr_t, std::uintptr_t> live_; // buffer -> device
    std::map<std::uintptr_t, bool> stopped_;        // buffer -> looping, to play on resume
};

} // namespace hr::core
