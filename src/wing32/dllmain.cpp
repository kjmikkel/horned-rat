#include "platform/guard.h"
#include "wing32/runtime.h"

#include <windows.h>

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        hr::platform::guarded("initialize", [] { hr::wing32::initialize(); }, [] {});
    }
    return TRUE;
}
