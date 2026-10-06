#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace hr::core {

// Mean Rec.601 brightness (0..255) of tightly packed 0x??RRGGBB pixels. Used by the fade check.
double mean_luma(const std::uint32_t* pixels, int width, int height);

// luma_probe CSV. Runs are appended to one file, each row tagged with its run (start time) and
// mode ("vanilla" or "a2"), so the two runs of the fade check end up side by side.
inline constexpr const char* kLumaCsvHeader = "run,mode,t,luma";
std::string luma_csv_row(std::string_view run, std::string_view mode, double t, double luma);

} // namespace hr::core
