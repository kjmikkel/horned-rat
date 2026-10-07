#pragma once

#include <functional>
#include <string>

namespace hr::platform {

// Diagnostics: logs the first `limit` access violations in the process, first chance, so even
// ones the game handles itself (it exits with the exception code) leave their place behind:
// faulting instruction as module+offset, the data address, the thread, and the code addresses
// found on the stack. One instance at a time; removed when destroyed.
class AccessViolationLog {
  public:
    using Sink = std::function<void(const std::string& line)>;
    AccessViolationLog(int limit, Sink sink);
    ~AccessViolationLog();
    AccessViolationLog(const AccessViolationLog&) = delete;
    AccessViolationLog& operator=(const AccessViolationLog&) = delete;

  private:
    void* handler_ = nullptr;
};

} // namespace hr::platform
