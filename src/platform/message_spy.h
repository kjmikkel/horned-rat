#pragma once

#include "core/message_census.h"

#include <windows.h>

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace hr::platform {

// Diagnostics (A2.3). Counts every message the windows of the constructing thread receive: sent
// ones (WH_CALLWNDPROC) and posted ones (WH_GETMESSAGE). When a message arrives and `period` has
// passed since the last hand-over, it passes the census to `sink` on that thread. Only one spy
// may exist at a time.
class MessageSpy {
  public:
    using Sink = std::function<void(const std::vector<std::string>& lines)>;

    MessageSpy(std::chrono::milliseconds period, Sink sink);
    ~MessageSpy();
    MessageSpy(const MessageSpy&) = delete;
    MessageSpy& operator=(const MessageSpy&) = delete;

    bool installed() const { return call_hook_ != nullptr && get_hook_ != nullptr; }
    void count(HWND window, UINT msg); // called by the hook procedures

  private:
    std::chrono::milliseconds period_;
    Sink sink_;
    core::MessageCensus census_;
    std::chrono::steady_clock::time_point last_;
    HHOOK call_hook_ = nullptr;
    HHOOK get_hook_ = nullptr;
};

} // namespace hr::platform
