#include "platform/crash_log.h"

#include "platform/modules.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>

namespace hr::platform {
namespace {

std::atomic<int> g_left{0};
AccessViolationLog::Sink g_sink;

bool in_module(std::uintptr_t value) {
    HMODULE module = nullptr;
    return value >= 0x10000 &&
           GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                  GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCSTR>(value), &module) != FALSE;
}

// Values on the stack that point into a module: likely return addresses, i.e. the callers.
std::string stack_code_addresses(const CONTEXT& context) {
#if defined(_M_IX86)
    const auto* sp = reinterpret_cast<const std::uintptr_t*>(context.Esp);
#else
    const auto* sp = reinterpret_cast<const std::uintptr_t*>(context.Rsp);
#endif
    ULONG_PTR low = 0;
    ULONG_PTR high = 0;
    GetCurrentThreadStackLimits(&low, &high);
    std::string out;
    int found = 0;
    for (int i = 0; i < 256 && found < 10; ++i, ++sp) {
        const auto at = reinterpret_cast<ULONG_PTR>(sp);
        if (at < low || at + sizeof *sp > high) {
            break;
        }
        if (in_module(*sp)) {
            out += (found++ == 0 ? "" : " < ") + describe_address(reinterpret_cast<void*>(*sp));
        }
    }
    return out;
}

LONG CALLBACK on_exception(EXCEPTION_POINTERS* info) {
    const EXCEPTION_RECORD& record = *info->ExceptionRecord;
    if (record.ExceptionCode != EXCEPTION_ACCESS_VIOLATION || g_left.fetch_sub(1) <= 0) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    const char* kind = "reading";
    if (record.NumberParameters >= 1 && record.ExceptionInformation[0] == 1) {
        kind = "writing";
    } else if (record.NumberParameters >= 1 && record.ExceptionInformation[0] == 8) {
        kind = "executing";
    }
    char head[96];
    std::snprintf(
        head, sizeof head, "access violation %s 0x%08X at ", kind,
        record.NumberParameters >= 2 ? static_cast<unsigned>(record.ExceptionInformation[1]) : 0u);
    g_sink(head + describe_address(record.ExceptionAddress) + " thread " +
           std::to_string(GetCurrentThreadId()) +
           "; stack: " + stack_code_addresses(*info->ContextRecord));
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

AccessViolationLog::AccessViolationLog(int limit, Sink sink) {
    g_sink = std::move(sink);
    g_left = limit;
    handler_ = AddVectoredExceptionHandler(1, &on_exception);
}

AccessViolationLog::~AccessViolationLog() {
    if (handler_ != nullptr) {
        RemoveVectoredExceptionHandler(handler_);
    }
    g_left = 0;
}

} // namespace hr::platform
