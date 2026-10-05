#include "platform/log.h"

#include "core/log_format.h"

#include <atomic>
#include <chrono>
#include <string>

namespace hr::platform {
namespace {

std::atomic<Log*> g_log{nullptr};

Log::Clock steady_since_now() {
    return [start = std::chrono::steady_clock::now()] {
        return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    };
}

} // namespace

Log::Log(const std::filesystem::path& path, Clock clock)
    : file_(path, std::ios::binary | std::ios::trunc),
      clock_(clock ? std::move(clock) : steady_since_now()) {}

void Log::write(std::string_view message) noexcept {
    try {
        const std::lock_guard lock(mutex_);
        file_ << core::format_log_line(clock_(), message);
        file_.flush();
    } catch (...) {
        // Logging must never take the game down.
    }
}

void set_global_log(Log* log) noexcept { g_log.store(log); }

void log(std::string_view message) noexcept {
    if (Log* const target = g_log.load()) {
        target->write(message);
    }
}

void log_exception(const char* where, const char* what) noexcept {
    try {
        if (what != nullptr) {
            log(std::string("exception in ") + where + ": " + what);
        } else {
            log(std::string("unknown exception in ") + where);
        }
    } catch (...) {
    }
}

} // namespace hr::platform
