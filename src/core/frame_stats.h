#pragma once

#include <string>

namespace hr::core {

// The presenter's performance line (spec §7), one per reporting period: frame count, mean
// capture and present times, and the slowest frame, which shows stutter the means hide.
class FrameStats {
  public:
    void add(double capture_ms, double present_ms);
    int frames() const { return frames_; }
    // "601 frames in 10 s, capture 2.13 ms, present 14.25 ms, slowest 64.70 ms (capture 60.10,
    // present 4.60)": the parts of the slowest frame tell which step stalled. Starts a new period.
    std::string take_line(int seconds);

  private:
    int frames_ = 0;
    double capture_ms_ = 0.0;
    double present_ms_ = 0.0;
    double slowest_ms_ = 0.0;
    double slowest_capture_ms_ = 0.0;
    double slowest_present_ms_ = 0.0;
};

} // namespace hr::core
