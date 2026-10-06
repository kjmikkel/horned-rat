#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hr::core {

// "WM_SETCURSOR" for the messages the cursor investigation cares about, "WM_USER+5" and
// "WM_APP+1" in those ranges, else the number as "0x0123".
std::string message_name(unsigned message);

// Counts messages per (window class, message) for the diagnostic message census (A2.3).
class MessageCensus {
  public:
    void add(std::string_view window_class, unsigned message);
    // One line per pair, sorted by class, then message number: "BitmapScrn WM_SETCURSOR x12".
    // Clears the counts.
    std::vector<std::string> take();
    bool empty() const { return counts_.empty(); }

  private:
    std::map<std::pair<std::string, unsigned>, std::uint64_t> counts_;
};

} // namespace hr::core
