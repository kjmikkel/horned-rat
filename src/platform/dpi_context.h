#pragma once

#include <windows.h>

namespace hr::platform {

// The calling thread's DPI awareness context; nullptr on Windows without per-thread DPI
// awareness (before Windows 10 1607). The functions are looked up at runtime.
DPI_AWARENESS_CONTEXT current_dpi_context();

// Switches the calling thread's DPI awareness for its lifetime. Tries `fallback` when `preferred`
// is refused (per-monitor V2 needs Windows 10 1703). Does nothing for nullptr or where
// unsupported; applied() tells.
class ScopedDpiContext {
  public:
    explicit ScopedDpiContext(DPI_AWARENESS_CONTEXT preferred,
                              DPI_AWARENESS_CONTEXT fallback = nullptr);
    ~ScopedDpiContext();
    ScopedDpiContext(const ScopedDpiContext&) = delete;
    ScopedDpiContext& operator=(const ScopedDpiContext&) = delete;

    bool applied() const { return previous_ != nullptr; }

  private:
    DPI_AWARENESS_CONTEXT previous_ = nullptr;
};

} // namespace hr::platform