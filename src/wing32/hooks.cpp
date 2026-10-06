// A1 instrumentation (spec §9): log-only hooks that answer which APIs the game uses to draw,
// create windows and read the mouse, and from which module. Every hook calls the original.
#include "wing32/hooks.h"

#include "core/game_modules.h"
#include "core/sample_limiter.h"
#include "platform/dc_info.h"
#include "platform/guard.h"
#include "platform/iat_hook.h"
#include "platform/log.h"
#include "platform/modules.h"
#include "wing32/a2.h"
#include "wing32/runtime.h"

#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <string>
#include <type_traits>

#pragma intrinsic(_ReturnAddress)

namespace hr::wing32 {
namespace {

constexpr core::SampleLimiter kLimiter{};

// Calls that target a window DC are counted separately, so rare window draws are never drowned
// out by frequent memory-DC draws.
struct DcCounters {
    std::atomic<std::uint64_t> window{0};
    std::atomic<std::uint64_t> other{0};
};

struct Counters {
    std::atomic<std::uint64_t> set_window_pos{0}, get_cursor_pos{0}, set_cursor_pos{0},
        clip_cursor{0}, screen_to_client{0}, client_to_screen{0}, get_dc{0}, get_window_dc{0},
        begin_paint{0};
    DcCounters stretch_dibits, text_out, pat_blt, bit_blt, set_dibits_to_device, wing_bit_blt,
        wing_stretch_blt;
};

Counters g_counters;

// 1-based call number if this call should be logged, else 0.
std::uint64_t sample(std::atomic<std::uint64_t>& calls) {
    const auto n = ++calls;
    return kLimiter.should_log(n) ? n : 0;
}

std::uint64_t sample_dc(DcCounters& counters, bool targets_window) {
    return sample(targets_window ? counters.window : counters.other);
}

void trace(const char* function, std::uint64_t n, const void* caller, const std::string& details) {
    const DWORD error = GetLastError();
    platform::log(std::string(function) + " #" + std::to_string(n) + " from " +
                  platform::module_file_name_from_address(caller) + ": " + details);
    SetLastError(error);
}

std::string rect(int x, int y, int w, int h) {
    return "(" + std::to_string(x) + "," + std::to_string(y) + " " + std::to_string(w) + "x" +
           std::to_string(h) + ")";
}

std::string hex(const void* p) {
    char text[24];
    std::snprintf(text, sizeof text, "0x%p", p);
    return text;
}

std::string dc_text(HDC dc) { return platform::describe_dc(dc); }

struct Originals {
    decltype(&::OutputDebugStringA) OutputDebugStringA;
    decltype(&::LoadLibraryA) LoadLibraryA;
    decltype(&::GetProcAddress) GetProcAddress;
    decltype(&::StretchDIBits) StretchDIBits;
    decltype(&::TextOutA) TextOutA;
    decltype(&::PatBlt) PatBlt;
    decltype(&::BitBlt) BitBlt;
    decltype(&::SetDIBitsToDevice) SetDIBitsToDevice;
    decltype(&::CreateWindowExA) CreateWindowExA;
    decltype(&::SetWindowPos) SetWindowPos;
    decltype(&::GetCursorPos) GetCursorPos;
    decltype(&::SetCursorPos) SetCursorPos;
    decltype(&::ClipCursor) ClipCursor;
    decltype(&::ScreenToClient) ScreenToClient;
    decltype(&::ClientToScreen) ClientToScreen;
    decltype(&::GetDC) GetDC;
    decltype(&::GetWindowDC) GetWindowDC;
    decltype(&::BeginPaint) BeginPaint;
    decltype(&::GetAsyncKeyState) GetAsyncKeyState;
    decltype(&::SetCursor) SetCursor;
};

const Originals& originals() {
    static const Originals o = [] {
        const HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
        const HMODULE gdi32 = GetModuleHandleW(L"gdi32.dll");
        const HMODULE user32 = GetModuleHandleW(L"user32.dll");
        Originals r{};
        const auto get = [](HMODULE module, const char* name, auto& target) {
            target = reinterpret_cast<std::remove_reference_t<decltype(target)>>(
                ::GetProcAddress(module, name));
        };
        get(kernel32, "OutputDebugStringA", r.OutputDebugStringA);
        get(kernel32, "LoadLibraryA", r.LoadLibraryA);
        get(kernel32, "GetProcAddress", r.GetProcAddress);
        get(gdi32, "StretchDIBits", r.StretchDIBits);
        get(gdi32, "TextOutA", r.TextOutA);
        get(gdi32, "PatBlt", r.PatBlt);
        get(gdi32, "BitBlt", r.BitBlt);
        get(gdi32, "SetDIBitsToDevice", r.SetDIBitsToDevice);
        get(user32, "CreateWindowExA", r.CreateWindowExA);
        get(user32, "SetWindowPos", r.SetWindowPos);
        get(user32, "GetCursorPos", r.GetCursorPos);
        get(user32, "SetCursorPos", r.SetCursorPos);
        get(user32, "ClipCursor", r.ClipCursor);
        get(user32, "ScreenToClient", r.ScreenToClient);
        get(user32, "ClientToScreen", r.ClientToScreen);
        get(user32, "GetDC", r.GetDC);
        get(user32, "GetWindowDC", r.GetWindowDC);
        get(user32, "BeginPaint", r.BeginPaint);
        get(user32, "GetAsyncKeyState", r.GetAsyncKeyState);
        get(user32, "SetCursor", r.SetCursor);
        return r;
    }();
    return o;
}

// ---- KERNEL32 ----

void WINAPI hook_OutputDebugStringA(LPCSTR text) {
    const auto call = [&] { originals().OutputDebugStringA(text); };
    platform::guarded(
        "OutputDebugStringA",
        [&] {
            if (settings().config.game_debug && text != nullptr) {
                platform::log(std::string("game: ") + text);
            }
            call();
        },
        call);
}

HMODULE WINAPI hook_LoadLibraryA(LPCSTR name) {
    const void* caller = _ReturnAddress();
    const HMODULE result = originals().LoadLibraryA(name);
    const DWORD error = GetLastError();
    platform::guarded(
        "LoadLibraryA",
        [&] {
            platform::log(std::string("LoadLibraryA(\"") + (name ? name : "(null)") + "\") from " +
                          platform::module_file_name_from_address(caller) + " -> " + hex(result));
            if (result != nullptr) {
                install_hooks();
            }
        },
        [] {});
    SetLastError(error);
    return result;
}

FARPROC WINAPI hook_GetProcAddress(HMODULE module, LPCSTR name) {
    const void* caller = _ReturnAddress();
    const FARPROC result = originals().GetProcAddress(module, name);
    const DWORD error = GetLastError();
    platform::guarded(
        "GetProcAddress",
        [&] {
            const std::string what =
                IS_INTRESOURCE(name) ? "#" + std::to_string(reinterpret_cast<std::uintptr_t>(name))
                                     : std::string(name);
            platform::log("GetProcAddress(" + platform::module_file_name_from_address(module) +
                          "!" + what + ") from " + platform::module_file_name_from_address(caller) +
                          (result ? "" : " -> not found"));
        },
        [] {});
    SetLastError(error);
    return result;
}

// ---- GDI32 ----

int WINAPI hook_StretchDIBits(HDC dc, int xd, int yd, int wd, int hd, int xs, int ys, int ws,
                              int hs, const VOID* bits, const BITMAPINFO* info, UINT usage,
                              DWORD rop) {
    const void* caller = _ReturnAddress();
    const auto call = [&] {
        return originals().StretchDIBits(dc, xd, yd, wd, hd, xs, ys, ws, hs, bits, info, usage,
                                         rop);
    };
    return platform::guarded(
        "StretchDIBits",
        [&] {
            if (const auto n = sample_dc(g_counters.stretch_dibits, WindowFromDC(dc) != nullptr)) {
                trace("StretchDIBits", n, caller,
                      "dc=" + dc_text(dc) + " dst=" + rect(xd, yd, wd, hd) +
                          " src=" + rect(xs, ys, ws, hs) +
                          " bpp=" + std::to_string(info ? info->bmiHeader.biBitCount : 0));
            }
            return call();
        },
        call);
}

BOOL WINAPI hook_TextOutA(HDC dc, int x, int y, LPCSTR text, int length) {
    const void* caller = _ReturnAddress();
    const auto call = [&] { return originals().TextOutA(dc, x, y, text, length); };
    return platform::guarded(
        "TextOutA",
        [&] {
            if (const auto n = sample_dc(g_counters.text_out, WindowFromDC(dc) != nullptr)) {
                trace("TextOutA", n, caller,
                      "dc=" + dc_text(dc) + " at (" + std::to_string(x) + "," + std::to_string(y) +
                          ") len=" + std::to_string(length));
            }
            return call();
        },
        call);
}

BOOL WINAPI hook_PatBlt(HDC dc, int x, int y, int w, int h, DWORD rop) {
    const void* caller = _ReturnAddress();
    const auto call = [&] { return originals().PatBlt(dc, x, y, w, h, rop); };
    return platform::guarded(
        "PatBlt",
        [&] {
            if (const auto n = sample_dc(g_counters.pat_blt, WindowFromDC(dc) != nullptr)) {
                trace("PatBlt", n, caller, "dc=" + dc_text(dc) + " " + rect(x, y, w, h));
            }
            return call();
        },
        call);
}

BOOL WINAPI hook_BitBlt(HDC dst, int x, int y, int w, int h, HDC src, int sx, int sy, DWORD rop) {
    const void* caller = _ReturnAddress();
    const auto call = [&] { return originals().BitBlt(dst, x, y, w, h, src, sx, sy, rop); };
    return platform::guarded(
        "BitBlt",
        [&] {
            const bool window = WindowFromDC(dst) != nullptr || WindowFromDC(src) != nullptr;
            if (const auto n = sample_dc(g_counters.bit_blt, window)) {
                trace("BitBlt", n, caller,
                      "dst=" + dc_text(dst) + " " + rect(x, y, w, h) + " src=" + dc_text(src) +
                          " at (" + std::to_string(sx) + "," + std::to_string(sy) + ")");
            }
            return call();
        },
        call);
}

int WINAPI hook_SetDIBitsToDevice(HDC dc, int xd, int yd, DWORD w, DWORD h, int xs, int ys,
                                  UINT start_scan, UINT lines, const VOID* bits,
                                  const BITMAPINFO* info, UINT usage) {
    const void* caller = _ReturnAddress();
    const auto call = [&] {
        return originals().SetDIBitsToDevice(dc, xd, yd, w, h, xs, ys, start_scan, lines, bits,
                                             info, usage);
    };
    return platform::guarded(
        "SetDIBitsToDevice",
        [&] {
            if (const auto n =
                    sample_dc(g_counters.set_dibits_to_device, WindowFromDC(dc) != nullptr)) {
                trace("SetDIBitsToDevice", n, caller,
                      "dc=" + dc_text(dc) +
                          " dst=" + rect(xd, yd, static_cast<int>(w), static_cast<int>(h)) +
                          " bpp=" + std::to_string(info ? info->bmiHeader.biBitCount : 0));
            }
            return call();
        },
        call);
}

// ---- USER32 ----

HWND WINAPI hook_CreateWindowExA(DWORD ex_style, LPCSTR class_name, LPCSTR window_name, DWORD style,
                                 int x, int y, int w, int h, HWND parent, HMENU menu,
                                 HINSTANCE instance, LPVOID param) {
    const void* caller = _ReturnAddress();
    const HWND result = originals().CreateWindowExA(ex_style, class_name, window_name, style, x, y,
                                                    w, h, parent, menu, instance, param);
    const DWORD error = GetLastError();
    platform::guarded(
        "CreateWindowExA",
        [&] {
            char styles[64];
            std::snprintf(styles, sizeof styles, "style=0x%08lx ex=0x%08lx",
                          static_cast<unsigned long>(style), static_cast<unsigned long>(ex_style));
            const std::string cls =
                IS_INTRESOURCE(class_name)
                    ? "#" + std::to_string(reinterpret_cast<std::uintptr_t>(class_name))
                    : std::string(class_name);
            platform::log("CreateWindowExA from " +
                          platform::module_file_name_from_address(caller) + ": class '" + cls +
                          "' title '" + (window_name ? window_name : "") + "' " + styles + " " +
                          rect(x, y, w, h) + " parent=" + hex(parent) + " -> " + hex(result));
            if (result != nullptr) {
                a2::on_window_created(result, parent, style);
            }
        },
        [] {});
    SetLastError(error);
    return result;
}

BOOL WINAPI hook_SetWindowPos(HWND window, HWND after, int x, int y, int w, int h, UINT flags) {
    const void* caller = _ReturnAddress();
    const auto call = [&] { return originals().SetWindowPos(window, after, x, y, w, h, flags); };
    return platform::guarded(
        "SetWindowPos",
        [&] {
            if (const auto n = sample(g_counters.set_window_pos)) {
                trace("SetWindowPos", n, caller,
                      "hwnd=" + hex(window) + " " + rect(x, y, w, h) +
                          " flags=" + std::to_string(flags));
            }
            return call();
        },
        call);
}

BOOL WINAPI hook_GetCursorPos(LPPOINT point) {
    const void* caller = _ReturnAddress();
    BOOL result = TRUE;
    if (point != nullptr && a2::active()) {
        platform::guarded(
            "GetCursorPos", [&] { a2::fake_cursor_pos(*point); },
            [&] { result = originals().GetCursorPos(point); });
    } else {
        result = originals().GetCursorPos(point);
    }
    platform::guarded(
        "GetCursorPos log",
        [&] {
            if (const auto n = sample(g_counters.get_cursor_pos); n && point) {
                trace("GetCursorPos", n, caller,
                      "-> (" + std::to_string(point->x) + "," + std::to_string(point->y) + ")" +
                          (a2::active() ? " virtual" : ""));
            }
        },
        [] {});
    return result;
}

SHORT WINAPI hook_GetAsyncKeyState(int vk) {
    const SHORT state = originals().GetAsyncKeyState(vk);
    return platform::guarded(
        "GetAsyncKeyState", [&]() -> SHORT { return a2::mask_button(vk) ? SHORT{0} : state; },
        [&] { return state; });
}

HCURSOR WINAPI hook_SetCursor(HCURSOR cursor) {
    platform::guarded("SetCursor", [&] { a2::set_game_cursor(cursor); }, [] {});
    return originals().SetCursor(cursor);
}

BOOL WINAPI hook_SetCursorPos(int x, int y) {
    const void* caller = _ReturnAddress();
    const auto call = [&] { return originals().SetCursorPos(x, y); };
    return platform::guarded(
        "SetCursorPos",
        [&] {
            if (const auto n = sample(g_counters.set_cursor_pos)) {
                trace("SetCursorPos", n, caller,
                      "(" + std::to_string(x) + "," + std::to_string(y) + ")");
            }
            return call();
        },
        call);
}

BOOL WINAPI hook_ClipCursor(const RECT* r) {
    const void* caller = _ReturnAddress();
    const auto call = [&] { return originals().ClipCursor(r); };
    return platform::guarded(
        "ClipCursor",
        [&] {
            if (const auto n = sample(g_counters.clip_cursor)) {
                trace("ClipCursor", n, caller,
                      r ? rect(r->left, r->top, r->right - r->left, r->bottom - r->top)
                        : std::string("(null)"));
            }
            return call();
        },
        call);
}

BOOL WINAPI hook_ScreenToClient(HWND window, LPPOINT point) {
    const void* caller = _ReturnAddress();
    const POINT before = point ? *point : POINT{};
    const BOOL result = originals().ScreenToClient(window, point);
    platform::guarded(
        "ScreenToClient",
        [&] {
            if (const auto n = sample(g_counters.screen_to_client); n && point) {
                trace("ScreenToClient", n, caller,
                      "hwnd=" + hex(window) + " (" + std::to_string(before.x) + "," +
                          std::to_string(before.y) + ") -> (" + std::to_string(point->x) + "," +
                          std::to_string(point->y) + ")");
            }
        },
        [] {});
    return result;
}

BOOL WINAPI hook_ClientToScreen(HWND window, LPPOINT point) {
    const void* caller = _ReturnAddress();
    const POINT before = point ? *point : POINT{};
    const BOOL result = originals().ClientToScreen(window, point);
    platform::guarded(
        "ClientToScreen",
        [&] {
            if (const auto n = sample(g_counters.client_to_screen); n && point) {
                trace("ClientToScreen", n, caller,
                      "hwnd=" + hex(window) + " (" + std::to_string(before.x) + "," +
                          std::to_string(before.y) + ") -> (" + std::to_string(point->x) + "," +
                          std::to_string(point->y) + ")");
            }
        },
        [] {});
    return result;
}

HDC WINAPI hook_GetDC(HWND window) {
    const void* caller = _ReturnAddress();
    const HDC result = originals().GetDC(window);
    platform::guarded(
        "GetDC",
        [&] {
            if (const auto n = sample(g_counters.get_dc)) {
                trace("GetDC", n, caller, "hwnd=" + hex(window) + " -> " + hex(result));
            }
        },
        [] {});
    return result;
}

HDC WINAPI hook_GetWindowDC(HWND window) {
    const void* caller = _ReturnAddress();
    const HDC result = originals().GetWindowDC(window);
    platform::guarded(
        "GetWindowDC",
        [&] {
            if (const auto n = sample(g_counters.get_window_dc)) {
                trace("GetWindowDC", n, caller, "hwnd=" + hex(window) + " -> " + hex(result));
            }
        },
        [] {});
    return result;
}

HDC WINAPI hook_BeginPaint(HWND window, LPPAINTSTRUCT paint) {
    const void* caller = _ReturnAddress();
    const HDC result = originals().BeginPaint(window, paint);
    platform::guarded(
        "BeginPaint",
        [&] {
            if (const auto n = sample(g_counters.begin_paint); n && paint) {
                const RECT& r = paint->rcPaint;
                trace("BeginPaint", n, caller,
                      "hwnd=" + hex(window) + " " +
                          rect(r.left, r.top, r.right - r.left, r.bottom - r.top));
            }
        },
        [] {});
    return result;
}

struct HookSpec {
    const char* dll;
    const char* function;
    void* replacement;
};

const HookSpec kHooks[] = {
    {"KERNEL32.dll", "OutputDebugStringA", reinterpret_cast<void*>(&hook_OutputDebugStringA)},
    {"KERNEL32.dll", "LoadLibraryA", reinterpret_cast<void*>(&hook_LoadLibraryA)},
    {"KERNEL32.dll", "GetProcAddress", reinterpret_cast<void*>(&hook_GetProcAddress)},
    {"GDI32.dll", "StretchDIBits", reinterpret_cast<void*>(&hook_StretchDIBits)},
    {"GDI32.dll", "TextOutA", reinterpret_cast<void*>(&hook_TextOutA)},
    {"GDI32.dll", "PatBlt", reinterpret_cast<void*>(&hook_PatBlt)},
    {"GDI32.dll", "BitBlt", reinterpret_cast<void*>(&hook_BitBlt)},
    {"GDI32.dll", "SetDIBitsToDevice", reinterpret_cast<void*>(&hook_SetDIBitsToDevice)},
    {"USER32.dll", "CreateWindowExA", reinterpret_cast<void*>(&hook_CreateWindowExA)},
    {"USER32.dll", "SetWindowPos", reinterpret_cast<void*>(&hook_SetWindowPos)},
    {"USER32.dll", "GetCursorPos", reinterpret_cast<void*>(&hook_GetCursorPos)},
    {"USER32.dll", "SetCursorPos", reinterpret_cast<void*>(&hook_SetCursorPos)},
    {"USER32.dll", "ClipCursor", reinterpret_cast<void*>(&hook_ClipCursor)},
    {"USER32.dll", "ScreenToClient", reinterpret_cast<void*>(&hook_ScreenToClient)},
    {"USER32.dll", "ClientToScreen", reinterpret_cast<void*>(&hook_ClientToScreen)},
    {"USER32.dll", "GetDC", reinterpret_cast<void*>(&hook_GetDC)},
    {"USER32.dll", "GetWindowDC", reinterpret_cast<void*>(&hook_GetWindowDC)},
    {"USER32.dll", "BeginPaint", reinterpret_cast<void*>(&hook_BeginPaint)},
    {"USER32.dll", "GetAsyncKeyState", reinterpret_cast<void*>(&hook_GetAsyncKeyState)},
    {"USER32.dll", "SetCursor", reinterpret_cast<void*>(&hook_SetCursor)},
};

void log_wing_blit(const char* function, DcCounters& counters, const void* caller, HDC dest, int x,
                   int y, int width, int height) {
    platform::guarded(
        function,
        [&] {
            if (const auto n = sample_dc(counters, WindowFromDC(dest) != nullptr)) {
                trace(function, n, caller,
                      "dst=" + dc_text(dest) + " " + rect(x, y, width, height));
            }
        },
        [] {});
}

} // namespace

void install_hooks() {
    if (!settings().hooks_enabled) {
        return;
    }
    static std::mutex mutex;
    const std::lock_guard lock(mutex);
    originals(); // resolve before any slot points at a hook
    for (const auto& module : platform::loaded_modules()) {
        if (!core::is_game_module(module.path, settings().game_dir)) {
            continue;
        }
        for (const auto& hook : kHooks) {
            const auto result =
                platform::hook_import(module.handle, hook.dll, hook.function, hook.replacement);
            if (result == platform::HookResult::Patched || result == platform::HookResult::Failed) {
                platform::log("hook " + module.path + " " + hook.dll + "!" + hook.function + ": " +
                              platform::to_string(result));
            }
        }
    }
}

void log_wing_bit_blt(const void* caller, HDC dest, int x, int y, int width, int height) {
    log_wing_blit("WinGBitBlt", g_counters.wing_bit_blt, caller, dest, x, y, width, height);
}

void log_wing_stretch_blt(const void* caller, HDC dest, int x, int y, int width, int height) {
    log_wing_blit("WinGStretchBlt", g_counters.wing_stretch_blt, caller, dest, x, y, width, height);
}

} // namespace hr::wing32
