#pragma once

#include "platform/log.h"

#include <exception>

namespace hr::platform {

// Runs `body`. If it throws, the exception is logged (tagged with `where`) and `fallback()` is
// returned instead. Wraps every hook body so that no C++ exception can unwind into game code
// (spec §6.2). `fallback` normally calls the original API function and must not throw.
template <typename Body, typename Fallback>
auto guarded(const char* where, Body&& body, Fallback&& fallback) noexcept -> decltype(body()) {
    try {
        return body();
    } catch (const std::exception& e) {
        log_exception(where, e.what());
    } catch (...) {
        log_exception(where, nullptr);
    }
    return fallback();
}

} // namespace hr::platform
