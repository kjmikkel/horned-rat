#include "wing32/rl_shadow.h"

#include "core/gamef_layout.h"
#include "core/latest.h"
#include "core/overlay.h"
#include "core/render_scale.h"
#include "platform/log.h"
#include "platform/sha256.h"
#include "platform/window_subclass.h"
#include "wing32/a2.h"
#include "wing32/runtime.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

namespace hr::wing32::rl_shadow {
namespace {

using u32 = std::uint32_t;
using PaletteCallback = void(__cdecl*)(void*, void*, int, int, int, int);
using CreateDevice = u32(__cdecl*)(void*, int, int, int, PaletteCallback, void*);
using HandleInt = u32(__cdecl*)(u32, int);
using CreateViewport = u32(__cdecl*)(u32, u32, int, int, int, int);
using ConfigureViewport = int(__cdecl*)(u32, int, int, int, int);
using SetPlane = int(__cdecl*)(u32, float, float, float, float);
using SetFloat = int(__cdecl*)(u32, float);
using Handle = int(__cdecl*)(u32);
using Render = int(__cdecl*)(u32, u32);
using Lookup = int(__cdecl*)(int*, u32, const void*);

struct Device {
    std::array<std::uint32_t, 15> desc{};
    std::vector<std::uint8_t> bits;
    int width = 0; // 1× size of the view
    int height = 0;
    int factor = 0;
    int stride = 0; // align4(width)·factor
    u32 device = 0;
    u32 viewport = 0;
};

enum class State { Unresolved, Off, On };

// Game thread only.
State g_state = State::Unresolved;
std::optional<core::GamefLayout> g_layout;
std::uintptr_t g_base = 0;
HWND g_view = nullptr;
std::unique_ptr<platform::Subclass> g_subclass;
std::unique_ptr<Device> g_device;
core::RenderBudget g_budget;

// Shared with the presenter thread.
std::atomic<int> g_factor{1};
core::Latest<core::ShadowFrame> g_frames;
std::mutex g_stats_mutex;
Stats g_stats;

template <typename F> F fn(u32 rva) { return reinterpret_cast<F>(g_base + rva); }

template <typename T> T read(std::uintptr_t at) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof value);
    return value;
}

int align4(int v) { return ((v + 3) / 4) * 4; }

// The ramp palette is shared by all devices (B1.0), so no colour ever arrives here.
void __cdecl on_palette(void*, void*, int, int, int, int) {}

void off(const std::string& why) {
    g_state = State::Off;
    platform::log("b1: off, " + why);
}

void resolve() {
    const HMODULE gamef = GetModuleHandleA("GAMEF.DLL");
    char path[MAX_PATH] = {};
    if (gamef == nullptr || GetModuleFileNameA(gamef, path, MAX_PATH) == 0) {
        off("GAMEF.DLL not loaded");
        return;
    }
    const auto hash = platform::sha256_file(path);
    g_layout = hash ? core::gamef_layout(*hash) : std::nullopt;
    if (!g_layout) {
        off("unknown GAMEF.DLL build (sha256 " + hash.value_or("?") + ")");
        return;
    }
    g_base = reinterpret_cast<std::uintptr_t>(gamef);
    g_state = State::On;
    platform::log("b1: known GAMEF.DLL build, sharp 3D view available");
}

// SEH boundary around calls into the game: no objects with destructors in this function.
template <typename F> bool seh(F&& body) {
    __try {
        body();
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

void destroy_device() {
    if (!g_device) {
        return;
    }
    const auto& l = *g_layout;
    Device* d = g_device.get();
    const bool ok = seh([&] {
        if (d->viewport != 0) {
            fn<Handle>(l.destroy_object)(d->viewport);
        }
        if (d->device != 0) {
            fn<Handle>(l.destroy_object)(d->device);
        }
    });
    if (!ok) {
        off("exception while destroying the device");
    }
    g_device.reset();
}

bool create_device(std::uintptr_t w, int width, int height, int factor) {
    const auto& l = *g_layout;
    auto device = std::make_unique<Device>();
    device->width = width;
    device->height = height;
    device->factor = factor;
    device->stride = align4(width) * factor;
    device->bits.assign(
        static_cast<std::size_t>(device->stride) * static_cast<std::size_t>(height * factor), 0);
    std::memcpy(device->desc.data(), reinterpret_cast<const void*>(w + l.w_device_desc),
                sizeof device->desc);
    device->desc[0] = static_cast<std::uint32_t>(device->stride);
    device->desc[1] = static_cast<std::uint32_t>(height * factor);
    device->desc[6] = static_cast<std::uint32_t>(device->stride);
    device->desc[7] =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(device->bits.data()));
    const u32 camera = read<u32>(w + l.w_camera);
    Device* d = device.get();
    const bool ok = seh([&] {
        d->device = fn<CreateDevice>(l.create_device)(d->desc.data(), 0, 0, 0, &on_palette, d);
        if (d->device == 0) {
            return;
        }
        fn<HandleInt>(l.set_device_mode)(d->device, 0);
        fn<HandleInt>(l.set_device_dither)(d->device, 0);
        fn<HandleInt>(l.set_device_shades)(d->device, 8);
        d->viewport = fn<CreateViewport>(l.create_viewport)(d->device, camera, 0, 0, d->stride,
                                                            height * factor);
        if (d->viewport != 0) {
            fn<ConfigureViewport>(l.configure_viewport)(d->viewport, 0, 0, d->stride,
                                                        height * factor);
        }
    });
    g_device = std::move(device);
    if (!ok) {
        off("exception while creating the device");
        return false;
    }
    if (d->device == 0 || d->viewport == 0) {
        destroy_device();
        off("device or viewport creation failed");
        return false;
    }
    platform::log("b1: device " + std::to_string(d->stride) + "x" +
                  std::to_string(height * factor) + " (x" + std::to_string(factor) + ")");
    return true;
}

// Copies the game viewport's projection settings to ours (game thread, inside seh()).
bool copy_viewport(std::uintptr_t w) {
    const auto& l = *g_layout;
    int object = 0;
    if (fn<Lookup>(l.lookup_object)(&object, read<u32>(w + l.w_viewport),
                                    reinterpret_cast<const void*>(g_base + l.viewport_type)) != 0 ||
        object == 0) {
        return false;
    }
    const auto o = static_cast<std::uintptr_t>(object);
    const u32 vp = g_device->viewport;
    fn<SetPlane>(l.set_viewport_plane)(vp, read<float>(o + l.vp_plane_a),
                                       read<float>(o + l.vp_plane_b), read<float>(o + l.vp_plane_c),
                                       read<float>(o + l.vp_plane_d));
    fn<SetFloat>(l.set_viewport_back)(vp, read<float>(o + l.vp_back));
    fn<HandleInt>(l.set_viewport_flag_d8)(vp, read<int>(o + l.vp_flag_d8));
    fn<HandleInt>(l.set_viewport_flag_44)(vp, read<int>(o + l.vp_flag_44));
    return true;
}

void publish(std::uintptr_t w, HDC src, core::Size src_size) {
    const auto& l = *g_layout;
    const Device& d = *g_device;
    core::ShadowFrame frame;
    frame.hires = d.bits;
    frame.stride = d.stride;
    frame.size = {d.width, d.height};
    frame.factor = d.factor;
    const auto* game_bits =
        reinterpret_cast<const std::uint8_t*>(read<std::uintptr_t>(w + l.w_bits));
    const int game_stride = static_cast<int>(read<std::uint32_t>(w + l.w_device_desc));
    // As the screen shows it: the same pixels at detail 1, GDI's stretch of the half-size frame
    // at detail 2 (the presenter's mask compares the capture with this).
    frame.game = core::stretch_indices(game_bits, game_stride, src_size, {d.width, d.height});
    std::array<RGBQUAD, 256> quads{};
    GetDIBColorTable(src, 0, 256, quads.data());
    for (std::size_t i = 0; i < 256; ++i) {
        frame.palette[i] = (static_cast<std::uint32_t>(quads[i].rgbRed) << 16) |
                           (static_cast<std::uint32_t>(quads[i].rgbGreen) << 8) | quads[i].rgbBlue;
    }
    frame.view_window = reinterpret_cast<std::uintptr_t>(g_view);
    frame.time_s = now_s();
    g_frames.publish(std::move(frame));
}

void render(HDC src, core::Size src_size, core::Size dest_size) {
    const auto& l = *g_layout;
    const auto w = static_cast<std::uintptr_t>(GetWindowLongA(GetParent(g_view), 4));
    if (w == 0) {
        return;
    }
    const int factor = std::min(g_factor.load(), g_budget.cap());
    if (factor <= 1) {
        return;
    }
    const int width = read<int>(w + l.w_active_width);
    const int height = read<int>(w + l.w_active_height);
    // The game blits the active view size, from its full-size (detail 1) or half-size (detail 2)
    // frame at the top left of its bitmap. Anything else is not the 3D view's frame.
    const int game_stride = static_cast<int>(read<std::uint32_t>(w + l.w_device_desc));
    if (dest_size.width != width || dest_size.height != height || src_size.width <= 0 ||
        src_size.height <= 0 || src_size.width > game_stride || src_size.height > height) {
        return;
    }
    if (!g_device || g_device->width != width || g_device->height != height ||
        g_device->factor != factor) {
        destroy_device();
        if (g_state != State::On || !create_device(w, width, height, factor)) {
            return;
        }
    }
    const u32 scene = read<u32>(w + l.w_scene);
    const auto t0 = std::chrono::steady_clock::now();
    bool copied = false;
    Device* d = g_device.get();
    const bool ok = seh([&] {
        copied = copy_viewport(w);
        if (copied) {
            fn<Handle>(l.clear_viewport)(d->viewport);
            fn<Render>(l.render_viewport)(d->viewport, scene);
        }
    });
    const double ms =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    if (!ok) {
        off("exception while rendering");
        return;
    }
    if (!copied) {
        return;
    }
    const int cap_before = g_budget.cap();
    g_budget.add(now_s(), ms, factor);
    if (g_budget.cap() != cap_before) {
        platform::log("b1: second render too slow, factor limited to " +
                      std::to_string(g_budget.cap()));
    }
    {
        const std::lock_guard lock(g_stats_mutex);
        ++g_stats.frames;
        g_stats.total_ms += ms;
        g_stats.max_ms = ms > g_stats.max_ms ? ms : g_stats.max_ms;
        g_stats.factor = factor;
    }
    publish(w, src, src_size);
}

} // namespace

double now_s() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

void on_window_created(HWND window, const std::string& class_name) {
    if (settings().config.render_scale == 1 || class_name != "Map3DClass") {
        return;
    }
    if (g_state == State::Unresolved) {
        resolve();
    }
    if (g_state != State::On) {
        return;
    }
    g_view = window;
    g_subclass = std::make_unique<platform::Subclass>(
        window, [](HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, WNDPROC original) {
            if (msg == WM_DESTROY) {
                destroy_device();
                g_view = nullptr;
            }
            return CallWindowProcA(original, hwnd, msg, wparam, lparam);
        });
}

void before_blit(HDC dest, HDC src, core::Size src_size, core::Size dest_size) {
    if (g_state != State::On || g_view == nullptr || !a2::active() ||
        WindowFromDC(dest) != g_view) {
        return;
    }
    render(src, src_size, dest_size);
}

void set_factor(int factor) { g_factor = factor; }

bool take_frame(std::uint64_t& seen, core::ShadowFrame& out) {
    return g_frames.take_newer(seen, out);
}

Stats take_stats() {
    const std::lock_guard lock(g_stats_mutex);
    const Stats stats = g_stats;
    g_stats = Stats{};
    return stats;
}

} // namespace hr::wing32::rl_shadow
