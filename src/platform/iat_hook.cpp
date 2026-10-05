#include "platform/iat_hook.h"

#include "platform/pe.h"

namespace hr::platform {

HookResult hook_import(HMODULE module, const char* dll, const char* function, void* replacement) {
    void** const slot = pe::find_import_slot(module, dll, function);
    if (slot == nullptr) {
        return HookResult::NotImported;
    }
    if (*slot == replacement) {
        return HookResult::AlreadyPatched;
    }
    DWORD old_protection = 0;
    if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_protection)) {
        return HookResult::Failed;
    }
    *slot = replacement; // pointer-sized aligned store: atomic on x86
    VirtualProtect(slot, sizeof(void*), old_protection, &old_protection);
    return HookResult::Patched;
}

const char* to_string(HookResult result) {
    switch (result) {
    case HookResult::Patched:
        return "patched";
    case HookResult::AlreadyPatched:
        return "already patched";
    case HookResult::NotImported:
        return "not imported";
    case HookResult::Failed:
        return "failed";
    }
    return "?";
}

} // namespace hr::platform
