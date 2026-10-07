#pragma once

#include "core/geometry.h"

namespace hr::core {

// Stage B1 render factor (spec 2026-10-07 §5.2). `setting` 0 = auto: the picture's on-screen
// scale (dest / content per axis) rounded up, the larger axis, clamped to 1..4. 1..4 = fixed.
int render_factor(int setting, Size content, Rect dest);

// Spec §7: over each 2 s window, a mean render time above 50 ms lowers the cap to one below the
// factor in use, down to 1. The cap never rises again in a session.
class RenderBudget {
  public:
    void add(double now_s, double ms, int factor);
    int cap() const { return cap_; }

  private:
    int cap_ = 4;
    double window_start_ = -1.0;
    double total_ms_ = 0.0;
    int count_ = 0;
};

} // namespace hr::core
