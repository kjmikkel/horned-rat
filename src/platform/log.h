#pragma once

#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <string_view>

namespace hr::platform {

class Log {
  public:
    using Clock = std::function<double()>; // seconds; defaults to time since construction

    explicit Log(const std::filesystem::path& path, Clock clock = {});
    Log(const Log&) = delete;
    Log& operator=(const Log&) = delete;

    void write(std::string_view message) noexcept;

  private:
    std::mutex mutex_;
    std::ofstream file_;
    Clock clock_;
};

void set_global_log(Log* log) noexcept;
void log(std::string_view message) noexcept;
void log_exception(const char* where, const char* what) noexcept;

} // namespace hr::platform
