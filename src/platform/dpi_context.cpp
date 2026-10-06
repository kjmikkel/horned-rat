#include "platform/dpi_context.h"

namespace hr::platform {
namespace {

using SetThreadContext = DPI_AWARENESS_CONTEXT(WINAPI*)(DPI_AWARENESS_CONTEXT);
using GetThreadContext = DPI_AWARENESS_CONTEXT(WINAPI*)();

template <class Function> Function user32_function(const char* name) {
    return reinterpret_cast<Function>(
        reinterpret_cast<void*>(GetProcAddress(GetModuleHandleA("user32.dll"), name)));
}

SetThreadContext set_thread_context() {
    static const auto function = user32_function<SetThreadContext>("SetThreadDpiAwarenessContext");
    return function;
}

} // namespace

DPI_AWARENESS_CONTEXT current_dpi_context() {
    static const auto function = user32_function<GetThreadContext>("GetThreadDpiAwarenessContext");
    return function != nullptr ? function() : nullptr;
}

ScopedDpiContext::ScopedDpiContext(DPI_AWARENESS_CONTEXT preferred,
                                   DPI_AWARENESS_CONTEXT fallback) {
    const SetThreadContext set = set_thread_context();
    if (set == nullptr) {
        return;
    }
    if (preferred != nullptr) {
        previous_ = set(preferred);
    }
    if (previous_ == nullptr && fallback != nullptr) {
        previous_ = set(fallback);
    }
}

ScopedDpiContext::~ScopedDpiContext() {
    if (previous_ != nullptr) {
        set_thread_context()(previous_);
    }
}

} // namespace hr::platform