#include "platform/dpi_context.h"

#include <catch2/catch_test_macros.hpp>

using hr::platform::ScopedDpiContext;

namespace {
DPI_AWARENESS thread_awareness() {
    return GetAwarenessFromDpiAwarenessContext(GetThreadDpiAwarenessContext());
}
} // namespace

TEST_CASE("a scoped DPI context switches the thread and restores it", "[dpi_context]") {
    const DPI_AWARENESS before = thread_awareness();
    {
        ScopedDpiContext scope(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2,
                               DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
        REQUIRE(scope.applied());
        CHECK(thread_awareness() == DPI_AWARENESS_PER_MONITOR_AWARE);
        {
            ScopedDpiContext inner(DPI_AWARENESS_CONTEXT_UNAWARE);
            CHECK(thread_awareness() == DPI_AWARENESS_UNAWARE);
        }
        CHECK(thread_awareness() == DPI_AWARENESS_PER_MONITOR_AWARE);
    }
    CHECK(thread_awareness() == before);
}

TEST_CASE("a scoped DPI context for nullptr changes nothing", "[dpi_context]") {
    const DPI_AWARENESS before = thread_awareness();
    ScopedDpiContext scope(nullptr);
    CHECK_FALSE(scope.applied());
    CHECK(thread_awareness() == before);
}

TEST_CASE("current_dpi_context reports the thread's context", "[dpi_context]") {
    ScopedDpiContext scope(DPI_AWARENESS_CONTEXT_UNAWARE);
    CHECK(GetAwarenessFromDpiAwarenessContext(hr::platform::current_dpi_context()) ==
          DPI_AWARENESS_UNAWARE);
}