// A1 instrumentation (spec §9): log-only hooks that answer which APIs the game uses to draw,
// create windows and read the mouse, and from which module. Every hook calls the original.
#include "wing32/hooks.h"

#include "core/audio.h"
#include "core/display_mode.h"
#include "core/game_modules.h"
#include "core/input_map.h"
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
#include <set>
#include <string>
#include <string_view>
#include <type_traits>

#include <mmsystem.h>

#include <dsound.h>

static_assert(hr::core::kDsbcapsPrimaryBuffer == DSBCAPS_PRIMARYBUFFER);
static_assert(hr::core::kDsbcapsGlobalFocus == DSBCAPS_GLOBALFOCUS);

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
        begin_paint{0}, set_cursor{0};
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
    decltype(&::ChangeDisplaySettingsA) ChangeDisplaySettingsA;
    decltype(&::SetWindowPos) SetWindowPos;
    decltype(&::ShowWindow) ShowWindow;
    decltype(&::GetCursorPos) GetCursorPos;
    decltype(&::SetCursorPos) SetCursorPos;
    decltype(&::ClipCursor) ClipCursor;
    decltype(&::ScreenToClient) ScreenToClient;
    decltype(&::ClientToScreen) ClientToScreen;
    decltype(&::GetDC) GetDC;
    decltype(&::GetWindowDC) GetWindowDC;
    decltype(&::BeginPaint) BeginPaint;
    decltype(&::GetAsyncKeyState) GetAsyncKeyState;
    decltype(&::TranslateMessage) TranslateMessage;
    decltype(&::SetCursor) SetCursor;
    decltype(&::LoadCursorA) LoadCursorA;
    decltype(&::SetClassLongA) SetClassLongA;
    decltype(&::RegisterClassA) RegisterClassA;
    decltype(&::RegisterClassExA) RegisterClassExA;
    decltype(&::MessageBoxA) MessageBoxA;
    decltype(&::ExitProcess) ExitProcess;
    HRESULT(WINAPI* DirectSoundCreate)(LPCGUID, LPDIRECTSOUND*, LPUNKNOWN);
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
        get(user32, "ChangeDisplaySettingsA", r.ChangeDisplaySettingsA);
        get(user32, "SetWindowPos", r.SetWindowPos);
        get(user32, "ShowWindow", r.ShowWindow);
        get(user32, "GetCursorPos", r.GetCursorPos);
        get(user32, "SetCursorPos", r.SetCursorPos);
        get(user32, "ClipCursor", r.ClipCursor);
        get(user32, "ScreenToClient", r.ScreenToClient);
        get(user32, "ClientToScreen", r.ClientToScreen);
        get(user32, "GetDC", r.GetDC);
        get(user32, "GetWindowDC", r.GetWindowDC);
        get(user32, "BeginPaint", r.BeginPaint);
        get(user32, "GetAsyncKeyState", r.GetAsyncKeyState);
        get(user32, "TranslateMessage", r.TranslateMessage);
        get(user32, "SetCursor", r.SetCursor);
        get(user32, "LoadCursorA", r.LoadCursorA);
        get(user32, "SetClassLongA", r.SetClassLongA);
        get(user32, "RegisterClassA", r.RegisterClassA);
        get(user32, "RegisterClassExA", r.RegisterClassExA);
        get(user32, "MessageBoxA", r.MessageBoxA);
        get(kernel32, "ExitProcess", r.ExitProcess);
        if (const HMODULE dsound = GetModuleHandleW(L"dsound.dll")) {
            get(dsound, "DirectSoundCreate", r.DirectSoundCreate);
        }
        return r;
    }();
    return o;
}

// ---- DSOUND (diagnostics) ----

using SetCooperativeLevel_t = HRESULT(STDMETHODCALLTYPE*)(IDirectSound*, HWND, DWORD);
using CreateSoundBuffer_t = HRESULT(STDMETHODCALLTYPE*)(IDirectSound*, LPCDSBUFFERDESC,
                                                        LPDIRECTSOUNDBUFFER*, LPUNKNOWN);
std::atomic<SetCooperativeLevel_t> g_set_cooperative_level{nullptr};
std::atomic<CreateSoundBuffer_t> g_create_sound_buffer{nullptr};
std::atomic<std::uint64_t> g_sound_buffers{0};

std::string window_class(HWND window) {
    char name[64] = {};
    if (window != nullptr) {
        GetClassNameA(window, name, sizeof name);
    }
    return name;
}

HRESULT STDMETHODCALLTYPE hook_SetCooperativeLevel(IDirectSound* self, HWND window, DWORD level) {
    platform::guarded(
        "SetCooperativeLevel",
        [&] {
            platform::log("diag: DirectSound SetCooperativeLevel hwnd=" + hex(window) + " class '" +
                          window_class(window) + "' level=" + std::to_string(level));
        },
        [] {});
    return g_set_cooperative_level.load()(self, window, level);
}

HRESULT STDMETHODCALLTYPE hook_CreateSoundBuffer(IDirectSound* self, LPCDSBUFFERDESC desc,
                                                 LPDIRECTSOUNDBUFFER* buffer, LPUNKNOWN outer) {
    platform::guarded(
        "CreateSoundBuffer",
        [&] {
            if (const auto n = sample(g_sound_buffers); n && desc != nullptr) {
                char flags[32];
                std::snprintf(flags, sizeof flags, "0x%08lx",
                              static_cast<unsigned long>(desc->dwFlags));
                platform::log(std::string("diag: DirectSound CreateSoundBuffer #") +
                              std::to_string(n) + " flags=" + flags +
                              ((desc->dwFlags & DSBCAPS_GLOBALFOCUS) ? " GLOBALFOCUS" : "") +
                              ((desc->dwFlags & DSBCAPS_STICKYFOCUS) ? " STICKYFOCUS" : "") +
                              ((desc->dwFlags & DSBCAPS_PRIMARYBUFFER) ? " PRIMARY" : ""));
            }
        },
        [] {});
    if (desc == nullptr) {
        return g_create_sound_buffer.load()(self, desc, buffer, outer);
    }
    DSBUFFERDESC copy = *desc;
    copy.dwFlags = core::with_global_focus(copy.dwFlags);
    return g_create_sound_buffer.load()(self, &copy, buffer, outer);
}

void patch_vtable_slot(void** vtable, int index, void* replacement) {
    DWORD old = 0;
    if (VirtualProtect(&vtable[index], sizeof(void*), PAGE_READWRITE, &old)) {
        vtable[index] = replacement;
        VirtualProtect(&vtable[index], sizeof(void*), old, &old);
    }
}

HRESULT WINAPI hook_DirectSoundCreate(LPCGUID guid, LPDIRECTSOUND* sound, LPUNKNOWN outer) {
    const void* caller = _ReturnAddress();
    const HRESULT result = originals().DirectSoundCreate(guid, sound, outer);
    platform::guarded(
        "DirectSoundCreate",
        [&] {
            char text[16];
            std::snprintf(text, sizeof text, "0x%08lx", static_cast<unsigned long>(result));
            platform::log(std::string("diag: DirectSoundCreate from ") +
                          platform::module_file_name_from_address(caller) + " -> " + text);
            if (SUCCEEDED(result) && sound != nullptr && *sound != nullptr) {
                auto** const vtable = *reinterpret_cast<void***>(*sound);
                // IDirectSound: 3 = CreateSoundBuffer, 6 = SetCooperativeLevel.
                if (vtable[3] != reinterpret_cast<void*>(&hook_CreateSoundBuffer)) {
                    g_create_sound_buffer = reinterpret_cast<CreateSoundBuffer_t>(vtable[3]);
                    patch_vtable_slot(vtable, 3, reinterpret_cast<void*>(&hook_CreateSoundBuffer));
                }
                if (vtable[6] != reinterpret_cast<void*>(&hook_SetCooperativeLevel)) {
                    g_set_cooperative_level = reinterpret_cast<SetCooperativeLevel_t>(vtable[6]);
                    patch_vtable_slot(vtable, 6,
                                      reinterpret_cast<void*>(&hook_SetCooperativeLevel));
                }
            }
        },
        [] {});
    return result;
}

// ---- Dialogs and exit ----

int WINAPI hook_MessageBoxA(HWND owner, LPCSTR text, LPCSTR caption, UINT type) {
    const HWND presenter = a2::active() ? a2::presenter_window() : nullptr;
    platform::guarded(
        "MessageBoxA",
        [&] {
            platform::log(std::string("MessageBoxA owner ") + hex(owner) +
                          (presenter != nullptr ? " -> presenter" : ""));
        },
        [] {});
    // Owned by the presenter, the box appears on top of it and disables it while open (A2.1).
    return originals().MessageBoxA(presenter != nullptr ? presenter : owner, text, caption, type);
}

void WINAPI hook_ExitProcess(UINT code) {
    platform::log("diag: ExitProcess(" + std::to_string(code) + ")");
    originals().ExitProcess(code);
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

static_assert(hr::core::kCdsTest == CDS_TEST);
static_assert(hr::core::kCdsFullscreen == CDS_FULLSCREEN);

// The game looks ChangeDisplaySettingsA up at runtime (GetProcAddress), so the IAT hooks miss it.
// Its 640x480 switch is deferred while the presenter owns the display (core::defer_display_change).
LONG WINAPI hook_ChangeDisplaySettingsA(DEVMODEA* mode, DWORD flags) {
    const bool defer = core::defer_display_change(a2::owns_display(), flags);
    LONG result = DISP_CHANGE_SUCCESSFUL;
    if (defer) {
        a2::defer_display_mode(mode, flags);
    } else {
        result = originals().ChangeDisplaySettingsA(mode, flags);
    }
    platform::guarded(
        "ChangeDisplaySettingsA",
        [&] {
            char line[200];
            if (mode == nullptr) {
                std::snprintf(
                    line, sizeof line,
                    "display: ChangeDisplaySettingsA(restore registry mode, flags 0x%lx) -> %ld%s",
                    static_cast<unsigned long>(flags), result, defer ? " (deferred)" : "");
            } else {
                std::snprintf(line, sizeof line,
                              "display: ChangeDisplaySettingsA(%lux%lu %lu bpp, fields 0x%lx, "
                              "flags 0x%lx) -> %ld%s",
                              mode->dmPelsWidth, mode->dmPelsHeight, mode->dmBitsPerPel,
                              mode->dmFields, static_cast<unsigned long>(flags), result,
                              defer ? " (deferred)" : "");
            }
            platform::log(line);
        },
        [] {});
    return result;
}

FARPROC WINAPI hook_GetProcAddress(HMODULE module, LPCSTR name) {
    const void* caller = _ReturnAddress();
    FARPROC result = originals().GetProcAddress(module, name);
    if (result != nullptr && !IS_INTRESOURCE(name) &&
        std::string_view(name) == "ChangeDisplaySettingsA") {
        result = reinterpret_cast<FARPROC>(&hook_ChangeDisplaySettingsA);
    }
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

// Diagnostics (A2.4): how the game hides, minimises or restores its top-level windows, for
// example when it loses activation. Child windows are shown constantly and are not logged.
BOOL WINAPI hook_ShowWindow(HWND window, int command) {
    const void* caller = _ReturnAddress();
    const BOOL result = originals().ShowWindow(window, command);
    const DWORD error = GetLastError();
    platform::guarded(
        "ShowWindow",
        [&] {
            if ((GetWindowLongPtrA(window, GWL_STYLE) & WS_CHILD) == 0) {
                platform::log("diag: ShowWindow(class '" + window_class(window) + "', " +
                              std::to_string(command) + ") -> " + std::to_string(result) +
                              " from " + platform::module_file_name_from_address(caller));
            }
        },
        [] {});
    SetLastError(error);
    return result;
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

BOOL WINAPI hook_TranslateMessage(const MSG* msg) {
    const bool skip = platform::guarded(
        "TranslateMessage",
        [&] { return msg != nullptr && core::skip_translate(a2::active(), msg->message); },
        [] { return false; });
    return skip ? FALSE : originals().TranslateMessage(msg);
}

SHORT WINAPI hook_GetAsyncKeyState(int vk) {
    const SHORT state = originals().GetAsyncKeyState(vk);
    return platform::guarded(
        "GetAsyncKeyState",
        [&]() -> SHORT {
            const bool masked = a2::mask_button(vk);
            if (a2::active() && core::is_mouse_button(vk)) {
                a2::note_button(vk, (state & 0x8000) != 0, masked);
            }
            return masked ? SHORT{0} : state;
        },
        [&] { return state; });
}

HCURSOR WINAPI hook_SetCursor(HCURSOR cursor) {
    const void* caller = _ReturnAddress();
    platform::guarded(
        "SetCursor",
        [&] {
            a2::set_game_cursor(cursor);
            static std::atomic<HCURSOR> last{nullptr};
            if (last.exchange(cursor) != cursor) {
                if (const auto n = sample(g_counters.set_cursor)) {
                    trace("SetCursor", n, caller, "cursor=" + hex(cursor));
                }
            }
        },
        [] {});
    return originals().SetCursor(cursor);
}

// ---- Cursor sources (diagnostics for the A2.3 investigation) ----

std::string resource_name(LPCSTR name) {
    if (IS_INTRESOURCE(name)) {
        return "#" + std::to_string(reinterpret_cast<std::uintptr_t>(name));
    }
    return std::string("\"") + name + "\"";
}

std::string instance_name(HINSTANCE instance) {
    if (instance == nullptr) {
        return "system";
    }
    const std::string path = platform::module_path(instance);
    const auto slash = path.find_last_of("\\/");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}

HCURSOR WINAPI hook_LoadCursorA(HINSTANCE instance, LPCSTR name) {
    const void* caller = _ReturnAddress();
    const HCURSOR result = originals().LoadCursorA(instance, name);
    const DWORD error = GetLastError();
    platform::guarded(
        "LoadCursorA",
        [&] {
            // Once per handle: some games load their cursor on every WM_SETCURSOR.
            static std::mutex mutex;
            static std::set<HCURSOR> seen;
            const std::lock_guard lock(mutex);
            if (seen.insert(result).second) {
                platform::log("diag: LoadCursorA(" + instance_name(instance) + ", " +
                              resource_name(name) + ") from " +
                              platform::module_file_name_from_address(caller) + " -> " +
                              hex(result));
            }
        },
        [] {});
    SetLastError(error);
    return result;
}

DWORD WINAPI hook_SetClassLongA(HWND window, int index, LONG value) {
    const void* caller = _ReturnAddress();
    const DWORD result = originals().SetClassLongA(window, index, value);
    const DWORD error = GetLastError();
    platform::guarded(
        "SetClassLongA",
        [&] {
            platform::log("diag: SetClassLongA(class '" + window_class(window) + "', " +
                          std::to_string(index) + (index == GCL_HCURSOR ? " GCL_HCURSOR" : "") +
                          ", " +
                          hex(reinterpret_cast<const void*>(
                              static_cast<std::uintptr_t>(static_cast<DWORD>(value)))) +
                          ") from " + platform::module_file_name_from_address(caller));
        },
        [] {});
    SetLastError(error);
    return result;
}

void log_class(const char* function, LPCSTR name, HCURSOR cursor, const void* caller) {
    platform::log(std::string("diag: ") + function + "(" + resource_name(name) + ", cursor " +
                  hex(cursor) + ") from " + platform::module_file_name_from_address(caller));
}

ATOM WINAPI hook_RegisterClassA(const WNDCLASSA* wc) {
    const void* caller = _ReturnAddress();
    const ATOM result = originals().RegisterClassA(wc);
    const DWORD error = GetLastError();
    platform::guarded(
        "RegisterClassA",
        [&] {
            if (wc != nullptr) {
                log_class("RegisterClassA", wc->lpszClassName, wc->hCursor, caller);
            }
        },
        [] {});
    SetLastError(error);
    return result;
}

ATOM WINAPI hook_RegisterClassExA(const WNDCLASSEXA* wc) {
    const void* caller = _ReturnAddress();
    const ATOM result = originals().RegisterClassExA(wc);
    const DWORD error = GetLastError();
    platform::guarded(
        "RegisterClassExA",
        [&] {
            if (wc != nullptr) {
                log_class("RegisterClassExA", wc->lpszClassName, wc->hCursor, caller);
            }
        },
        [] {});
    SetLastError(error);
    return result;
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
    {"USER32.dll", "ShowWindow", reinterpret_cast<void*>(&hook_ShowWindow)},
    {"USER32.dll", "GetCursorPos", reinterpret_cast<void*>(&hook_GetCursorPos)},
    {"USER32.dll", "SetCursorPos", reinterpret_cast<void*>(&hook_SetCursorPos)},
    {"USER32.dll", "ClipCursor", reinterpret_cast<void*>(&hook_ClipCursor)},
    {"USER32.dll", "ScreenToClient", reinterpret_cast<void*>(&hook_ScreenToClient)},
    {"USER32.dll", "ClientToScreen", reinterpret_cast<void*>(&hook_ClientToScreen)},
    {"USER32.dll", "GetDC", reinterpret_cast<void*>(&hook_GetDC)},
    {"USER32.dll", "GetWindowDC", reinterpret_cast<void*>(&hook_GetWindowDC)},
    {"USER32.dll", "BeginPaint", reinterpret_cast<void*>(&hook_BeginPaint)},
    {"USER32.dll", "GetAsyncKeyState", reinterpret_cast<void*>(&hook_GetAsyncKeyState)},
    {"USER32.dll", "TranslateMessage", reinterpret_cast<void*>(&hook_TranslateMessage)},
    {"USER32.dll", "SetCursor", reinterpret_cast<void*>(&hook_SetCursor)},
    {"USER32.dll", "LoadCursorA", reinterpret_cast<void*>(&hook_LoadCursorA)},
    {"USER32.dll", "SetClassLongA", reinterpret_cast<void*>(&hook_SetClassLongA)},
    {"USER32.dll", "RegisterClassA", reinterpret_cast<void*>(&hook_RegisterClassA)},
    {"USER32.dll", "RegisterClassExA", reinterpret_cast<void*>(&hook_RegisterClassExA)},
    {"DSOUND.dll", "DirectSoundCreate", reinterpret_cast<void*>(&hook_DirectSoundCreate)},
    {"USER32.dll", "MessageBoxA", reinterpret_cast<void*>(&hook_MessageBoxA)},
    {"KERNEL32.dll", "ExitProcess", reinterpret_cast<void*>(&hook_ExitProcess)},
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
