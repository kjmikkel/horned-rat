#pragma once

#include <map>
#include <mutex>
#include <utility>

namespace hr::platform {

// Patches slots of COM objects' vtables and remembers each vtable's original functions, so
// objects of different classes (different vtables) each call on to their own original. Patches
// are never undone: the objects' code may run until the process ends. Thread-safe.
class VtablePatches {
  public:
    // Points `slot` of `object`'s vtable at `replacement`, unless it already does.
    void patch(void* object, int slot, void* replacement);
    // The original function of `slot` in `object`'s vtable, or nullptr if never patched.
    void* original(void* object, int slot) const;

  private:
    mutable std::mutex mutex_;
    std::map<std::pair<void**, int>, void*> originals_;
};

} // namespace hr::platform
