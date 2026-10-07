#pragma once

#include <windows.h>

#include <functional>

namespace hr::platform {

// Holds the calling thread while keep_waiting() is true. Messages sent to the thread's windows
// are still handled, so other threads never wait on it; posted messages stay queued. Re-checks
// keep_waiting() whenever `wake` is signalled, and at least every 100 ms.
void wait_handling_sent_messages(HANDLE wake, const std::function<bool()>& keep_waiting);

} // namespace hr::platform
