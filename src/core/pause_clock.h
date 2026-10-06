#pragma once

#include <cstdint>

namespace hr::core {

// The game's clock under background = pause: real time minus the time the game was held, so it
// does not catch up on return. Times are 32-bit milliseconds that wrap like GetTickCount.
class PauseClock {
  public:
    void hold(std::uint32_t now);    // ignored while held
    void release(std::uint32_t now); // ignored while not held
    bool held() const { return held_; }
    std::uint32_t game_time(std::uint32_t now) const;

  private:
    bool held_ = false;
    std::uint32_t held_since_ = 0;
    std::uint32_t held_total_ = 0;
};

} // namespace hr::core
