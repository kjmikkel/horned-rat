#include "platform/message_wait.h"

namespace hr::platform {

void wait_handling_sent_messages(HANDLE wake, const std::function<bool()>& keep_waiting) {
    while (keep_waiting()) {
        MsgWaitForMultipleObjectsEx(1, &wake, 100, QS_SENDMESSAGE, 0);
        // Peeking for sent messages calls their window procedures; nothing is removed.
        MSG msg;
        PeekMessageA(&msg, nullptr, 0, 0, PM_NOREMOVE | PM_QS_SENDMESSAGE);
    }
}

} // namespace hr::platform
