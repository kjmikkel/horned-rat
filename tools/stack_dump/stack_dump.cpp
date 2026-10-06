// Dev tool for diagnosing hangs: prints the call stack of every thread of a running 32-bit
// process, with module+offset and the nearest exported name.
//   stack_dump <pid>
// Each thread is suspended only while its stack is read. Never commit its output.
#include <windows.h>

#include <dbghelp.h>
#include <tlhelp32.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace {

std::string describe(HANDLE process, DWORD64 address) {
    char module_name[MAX_PATH] = "?";
    IMAGEHLP_MODULE64 module{};
    module.SizeOfStruct = sizeof module;
    DWORD64 base = 0;
    if (SymGetModuleInfo64(process, address, &module)) {
        std::snprintf(module_name, sizeof module_name, "%s", module.ModuleName);
        base = module.BaseOfImage;
    }
    char buffer[sizeof(SYMBOL_INFO) + 256] = {};
    auto* const symbol = reinterpret_cast<SYMBOL_INFO*>(buffer);
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 255;
    DWORD64 displacement = 0;
    char line[512];
    if (SymFromAddr(process, address, &displacement, symbol)) {
        std::snprintf(line, sizeof line, "%s!%s+0x%llx", module_name, symbol->Name,
                      static_cast<unsigned long long>(displacement));
    } else {
        std::snprintf(line, sizeof line, "%s+0x%llx", module_name,
                      static_cast<unsigned long long>(address - base));
    }
    return line;
}

void dump_thread(HANDLE process, DWORD thread_id) {
    const HANDLE thread = OpenThread(
        THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, thread_id);
    if (thread == nullptr) {
        std::printf("thread %lu: cannot open\n", thread_id);
        return;
    }
    std::printf("thread %lu\n", thread_id);
    SuspendThread(thread);
    CONTEXT context{};
    context.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(thread, &context)) {
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Eip;
        frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Ebp;
        frame.AddrFrame.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Esp;
        frame.AddrStack.Mode = AddrModeFlat;
        for (int i = 0; i < 40; ++i) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_I386, process, thread, &frame, &context, nullptr,
                             SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
                frame.AddrPC.Offset == 0) {
                break;
            }
            std::printf("  %2d %s\n", i, describe(process, frame.AddrPC.Offset).c_str());
        }
    }
    ResumeThread(thread);
    CloseHandle(thread);
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fputs("usage: stack_dump <pid>\n", stderr);
        return 2;
    }
    const DWORD pid = static_cast<DWORD>(std::strtoul(argv[1], nullptr, 10));
    const HANDLE process = OpenProcess(PROCESS_QUERY_INFORMATION | PROCESS_VM_READ, FALSE, pid);
    if (process == nullptr) {
        std::fputs("cannot open the process\n", stderr);
        return 1;
    }
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    if (!SymInitialize(process, nullptr, TRUE)) {
        std::fputs("SymInitialize failed\n", stderr);
        return 1;
    }
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    THREADENTRY32 entry{};
    entry.dwSize = sizeof entry;
    for (BOOL ok = Thread32First(snapshot, &entry); ok; ok = Thread32Next(snapshot, &entry)) {
        if (entry.th32OwnerProcessID == pid) {
            dump_thread(process, entry.th32ThreadID);
        }
    }
    CloseHandle(snapshot);
    SymCleanup(process);
    CloseHandle(process);
    return 0;
}
