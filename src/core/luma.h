#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace hr::core {

// Mean Rec.601 brightness (0..255) of tightly packed 0x??RRGGBB pixels. Used by the fade check.
double mean_luma(const std::uint32_t* pixels, int width, int height);

// Which captured frames' brightness is logged at start-up (A2.4 diagnostics for the brightness
// dip): within `duration_s` of the start, the first frame and every change of at least
// `min_step` since the last logged value.
class BrightnessTrace {
  public:
    BrightnessTrace(double duration_s, double min_step)
        : duration_s_(duration_s), min_step_(min_step) {}
    bool running(double t) const { return t < duration_s_; }
    bool should_log(double t, double luma);

  private:
    double duration_s_;
    double min_step_;
    bool logged_any_ = false;
    double last_ = 0.0;
};

// luma_probe CSV. Runs are appended to one file, each row tagged with its run (start time) and
// mode ("vanilla" or "a2"), so the two runs of the fade check end up side by side.
inline constexpr const char* kLumaCsvHeader = "run,mode,t,luma";
std::string luma_csv_row(std::string_view run, std::string_view mode, double t, double luma);

} // namespace hr::core
