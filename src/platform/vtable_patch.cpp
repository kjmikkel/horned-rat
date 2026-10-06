#include "platform/vtable_patch.h"

#include <windows.h>

namespace hr::platform {
namespace {
void** vtable_of(void* object) { return *static_cast<void***>(object); }
} // namespace

void VtablePatches::patch(void* object, int slot, void* replacement) {
    void** const vtable = vtable_of(object);
    const std::lock_guard lock(mutex_);
    if (vtable[slot] == replacement) {
        return;
    }
    originals_[{vtable, slot}] = vtable[slot];
    DWORD old = 0;
    if (VirtualProtect(&vtable[slot], sizeof(void*), PAGE_READWRITE, &old)) {
        vtable[slot] = replacement;
        VirtualProtect(&vtable[slot], sizeof(void*), old, &old);
    }
}

void* VtablePatches::original(void* object, int slot) const {
    const std::lock_guard lock(mutex_);
    const auto it = originals_.find({vtable_of(object), slot});
    return it == originals_.end() ? nullptr : it->second;
}

} // namespace hr::platform
