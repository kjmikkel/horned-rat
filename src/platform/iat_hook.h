#pragma once

#include <windows.h>

namespace hr::platform {

enum class HookResult { Patched, AlreadyPatched, NotImported, Failed };

// Points `module`'s import of `dll`!`function` at `replacement`. Idempotent: returns
// AlreadyPatched if the slot already holds `replacement`. Restoring is hooking with the original.
HookResult hook_import(HMODULE module, const char* dll, const char* function, void* replacement);

const char* to_string(HookResult result);

} // namespace hr::platform
