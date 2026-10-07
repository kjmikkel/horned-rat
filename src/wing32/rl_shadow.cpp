#include "wing32/rl_shadow.h"

#include "core/gamef_layout.h"
#include "core/indexed_frame.h"
#include "platform/log.h"
#include "platform/sha256.h"
#include "platform/window_subclass.h"
#include "wing32/runtime.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
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

struct Shadow {
    std::array<std::uint32_t, 15> desc{};
    std::vector<std::uint8_t> bits;
    std::array<std::uint32_t, 256> palette{}; // 0x00RRGGBB, filled by the palette callback
    int width = 0;                            // 1× size of the active 3D view
    int height = 0;
    int k = 0;
    int stride = 0;
    u32 device = 0;
    u32 viewport = 0;
};

enum class State { Unresolved, Off, On };

State g_state = State::Unresolved;
std::optional<core::GamefLayout> g_layout;
std::uintptr_t g_base = 0;
HWND g_map3d = nullptr;
std::unique_ptr<platform::Subclass> g_subclass;
std::unique_ptr<Shadow> g_shadow;
int g_frames = 0;
double g_ms_total = 0;
double g_ms_max = 0;
bool g_dumped = false;
bool g_logged_half = false;
bool g_logged_front = false;
int g_palette_calls = 0;

template <typename F> F fn(u32 rva) { return reinterpret_cast<F>(g_base + rva); }

template <typename T> T read(std::uintptr_t at) {
    T value;
    std::memcpy(&value, reinterpret_cast<const void*>(at), sizeof value);
    return value;
}

void __cdecl on_palette(void*, void* arg, int index, int r, int g, int b) {
    auto* shadow = static_cast<Shadow*>(arg);
    ++g_palette_calls;
    if (index >= 0 && index < 256) {
        shadow->palette[static_cast<std::size_t>(index)] =
            (static_cast<std::uint32_t>(r & 0xFF) << 16) |
            (static_cast<std::uint32_t>(g & 0xFF) << 8) | static_cast<std::uint32_t>(b & 0xFF);
    }
}

void off(const std::string& why) {
    g_state = State::Off;
    platform::log("b1 spike: off, " + why);
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
    platform::log("b1 spike: GAMEF.DLL sha256 " + hash.value_or("?") +
                  (g_layout ? " (known build)" : " (unknown build)"));
    if (!g_layout) {
        off("unknown GAMEF.DLL build");
        return;
    }
    g_base = reinterpret_cast<std::uintptr_t>(gamef);
    g_state = State::On;
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

void destroy_shadow() {
    if (!g_shadow) {
        return;
    }
    const auto& l = *g_layout;
    Shadow* s = g_shadow.get();
    const bool ok = seh([&] {
        if (s->viewport != 0) {
            fn<Handle>(l.destroy_object)(s->viewport);
        }
        if (s->device != 0) {
            fn<Handle>(l.destroy_object)(s->device);
        }
    });
    platform::log(std::string("b1 spike: device destroyed") + (ok ? "" : " (exception)"));
    g_shadow.reset();
}

bool create_shadow(std::uintptr_t w, int width, int height, int k) {
    const auto& l = *g_layout;
    auto shadow = std::make_unique<Shadow>();
    shadow->width = width;
    shadow->height = height;
    shadow->k = k;
    shadow->stride = ((width * k + 3) / 4) * 4;
    shadow->bits.assign(
        static_cast<std::size_t>(shadow->stride) * static_cast<std::size_t>(height * k), 0);
    std::memcpy(shadow->desc.data(), reinterpret_cast<const void*>(w + l.w_device_desc),
                sizeof shadow->desc);
    shadow->desc[0] = static_cast<std::uint32_t>(shadow->stride);
    shadow->desc[1] = static_cast<std::uint32_t>(height * k);
    shadow->desc[6] = static_cast<std::uint32_t>(shadow->stride);
    shadow->desc[7] =
        static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(shadow->bits.data()));
    const u32 camera = read<u32>(w + l.w_camera);
    Shadow* s = shadow.get();
    const bool ok = seh([&] {
        s->device = fn<CreateDevice>(l.create_device)(s->desc.data(), 0, 0, 0, &on_palette, s);
        if (s->device == 0) {
            return;
        }
        fn<HandleInt>(l.set_device_mode)(s->device, 0);
        fn<HandleInt>(l.set_device_dither)(s->device, 0);
        fn<HandleInt>(l.set_device_shades)(s->device, 8);
        s->viewport =
            fn<CreateViewport>(l.create_viewport)(s->device, camera, 0, 0, s->stride, height * k);
        if (s->viewport != 0) {
            fn<ConfigureViewport>(l.configure_viewport)(s->viewport, 0, 0, width * k, height * k);
        }
    });
    g_shadow = std::move(shadow);
    platform::log("b1 spike: device " + std::to_string(s->stride) + "x" +
                  std::to_string(height * k) + " (k=" + std::to_string(k) +
                  ") device=" + std::to_string(s->device) +
                  " viewport=" + std::to_string(s->viewport) + (ok ? "" : " (exception)"));
    if (!ok) {
        off("exception while creating the device");
        return false;
    }
    if (s->device == 0 || s->viewport == 0) {
        destroy_shadow();
        off("device or viewport creation failed");
        return false;
    }
    return true;
}

// Copies the game viewport's projection settings to ours.
bool copy_viewport(std::uintptr_t w) {
    const auto& l = *g_layout;
    const u32 game_vp = read<u32>(w + l.w_viewport);
    int object = 0;
    if (fn<Lookup>(l.lookup_object)(&object, game_vp,
                                    reinterpret_cast<const void*>(g_base + l.viewport_type)) != 0 ||
        object == 0) {
        return false;
    }
    const auto o = static_cast<std::uintptr_t>(object);
    const u32 vp = g_shadow->viewport;
    fn<SetPlane>(l.set_viewport_plane)(vp, read<float>(o + l.vp_plane_a),
                                       read<float>(o + l.vp_plane_b), read<float>(o + l.vp_plane_c),
                                       read<float>(o + l.vp_plane_d));
    fn<SetFloat>(l.set_viewport_back)(vp, read<float>(o + l.vp_back));
    fn<HandleInt>(l.set_viewport_flag_d8)(vp, read<int>(o + l.vp_flag_d8));
    fn<HandleInt>(l.set_viewport_flag_44)(vp, read<int>(o + l.vp_flag_44));
    return true;
}

void log_viewport_once(std::uintptr_t w) {
    if (g_logged_front) {
        return;
    }
    g_logged_front = true;
    const auto& l = *g_layout;
    int object = 0;
    if (fn<Lookup>(l.lookup_object)(&object, read<u32>(w + l.w_viewport),
                                    reinterpret_cast<const void*>(g_base + l.viewport_type)) == 0) {
        const auto o = static_cast<std::uintptr_t>(object);
        platform::log("b1 spike: game viewport front " +
                      std::to_string(read<float>(o + l.vp_front)) + " back " +
                      std::to_string(read<float>(o + l.vp_back)) + " planes " +
                      std::to_string(read<float>(o + l.vp_plane_a)) + " " +
                      std::to_string(read<float>(o + l.vp_plane_b)) + " " +
                      std::to_string(read<float>(o + l.vp_plane_c)) + " " +
                      std::to_string(read<float>(o + l.vp_plane_d)));
    }
}

void write_bmp(const std::string& name, const std::uint8_t* bits, int stride, int width, int height,
               const std::array<std::uint32_t, 256>& palette) {
    const auto path = module_dir() / ("horned-rat-b1-" + name + ".bmp");
    BITMAPINFOHEADER info{};
    info.biSize = sizeof info;
    info.biWidth = stride;
    info.biHeight = -height; // top-down
    info.biPlanes = 1;
    info.biBitCount = 8;
    info.biClrUsed = 256;
    const DWORD palette_bytes = 256 * 4;
    const DWORD pixel_bytes = static_cast<DWORD>(stride) * static_cast<DWORD>(height);
    BITMAPFILEHEADER file{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof file + sizeof info + palette_bytes;
    file.bfSize = file.bfOffBits + pixel_bytes;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof file);
    out.write(reinterpret_cast<const char*>(&info), sizeof info);
    for (const std::uint32_t c : palette) {
        const RGBQUAD q{static_cast<BYTE>(c & 0xFF), static_cast<BYTE>((c >> 8) & 0xFF),
                        static_cast<BYTE>((c >> 16) & 0xFF), 0};
        out.write(reinterpret_cast<const char*>(&q), sizeof q);
    }
    out.write(reinterpret_cast<const char*>(bits), pixel_bytes);
    platform::log("b1 spike: wrote " + path.string() + " (" + std::to_string(width) + "x" +
                  std::to_string(height) + ")");
}

std::array<std::uint32_t, 256> game_palette(HDC src) {
    std::array<RGBQUAD, 256> quads{};
    GetDIBColorTable(src, 0, 256, quads.data());
    std::array<std::uint32_t, 256> palette{};
    for (std::size_t i = 0; i < 256; ++i) {
        palette[i] = (static_cast<std::uint32_t>(quads[i].rgbRed) << 16) |
                     (static_cast<std::uint32_t>(quads[i].rgbGreen) << 8) | quads[i].rgbBlue;
    }
    return palette;
}

std::array<std::uint32_t, 256> identity_palette() {
    std::array<std::uint32_t, 256> palette{};
    for (std::size_t i = 0; i < 256; ++i) {
        palette[i] = static_cast<std::uint32_t>(i);
    }
    return palette;
}

// Diagnostics: compares our frame with the game's by our palette, by the game's palette (shared
// colour allocation) and by index.
void compare_with_game(std::uintptr_t w, HDC src) {
    const auto& l = *g_layout;
    const auto game_pal = game_palette(src);
    static const auto identity = identity_palette();
    const auto* game_bits =
        reinterpret_cast<const std::uint8_t*>(read<std::uintptr_t>(w + l.w_bits));
    const int game_stride = static_cast<int>(read<std::uint32_t>(w + l.w_device_desc));
    const Shadow& s = *g_shadow;
    const core::IndexedFrame game{game_bits, game_stride, &game_pal};
    const int by_own = core::count_differing_pixels(game, {s.bits.data(), s.stride, &s.palette},
                                                    s.width, s.height);
    const int by_game =
        core::count_differing_pixels(game, {s.bits.data(), s.stride, &game_pal}, s.width, s.height);
    const int by_index =
        core::count_differing_pixels({game_bits, game_stride, &identity},
                                     {s.bits.data(), s.stride, &identity}, s.width, s.height);
    if (g_frames <= 3 || g_frames % 50 == 0) {
        platform::log("b1 spike: frame " + std::to_string(g_frames) + ", of " +
                      std::to_string(s.width * s.height) +
                      " pixels differ: " + std::to_string(by_own) + " (our palette), " +
                      std::to_string(by_game) + " (game palette), " + std::to_string(by_index) +
                      " (index); " + std::to_string(g_palette_calls) + " palette callbacks");
    }
}

void dump_frames(std::uintptr_t w, HDC src) {
    const auto& l = *g_layout;
    const Shadow& s = *g_shadow;
    const auto game_pal = game_palette(src);
    const std::string k = "k" + std::to_string(s.k);
    write_bmp(k, s.bits.data(), s.stride, s.width * s.k, s.height * s.k, s.palette);
    write_bmp(k + "-game-palette", s.bits.data(), s.stride, s.width * s.k, s.height * s.k,
              game_pal);
    const auto* game_bits =
        reinterpret_cast<const std::uint8_t*>(read<std::uintptr_t>(w + l.w_bits));
    const int game_stride = static_cast<int>(read<std::uint32_t>(w + l.w_device_desc));
    write_bmp("game", game_bits, game_stride, s.width, s.height, game_pal);
}

void render(HDC src) {
    const auto& l = *g_layout;
    const auto w = static_cast<std::uintptr_t>(GetWindowLongA(GetParent(g_map3d), 4));
    if (w == 0) {
        return;
    }
    if (read<int>(w + l.w_divisor) != 1) {
        if (!g_logged_half) {
            g_logged_half = true;
            platform::log("b1 spike: half detail, not rendered");
        }
        return;
    }
    const int width = read<int>(w + l.w_active_width);
    const int height = read<int>(w + l.w_active_height);
    const int k = settings().config.b1_spike;
    if (!g_shadow || g_shadow->width != width || g_shadow->height != height || g_shadow->k != k) {
        destroy_shadow();
        if (!create_shadow(w, width, height, k)) {
            return;
        }
    }
    const u32 scene = read<u32>(w + l.w_scene);
    LARGE_INTEGER t0;
    LARGE_INTEGER t1;
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&t0);
    bool copied = false;
    Shadow* s = g_shadow.get();
    const bool ok = seh([&] {
        copied = copy_viewport(w);
        if (copied) {
            fn<Handle>(l.clear_viewport)(s->viewport);
            fn<Render>(l.render_viewport)(s->viewport, scene);
        }
    });
    QueryPerformanceCounter(&t1);
    if (!ok) {
        off("exception while rendering");
        return;
    }
    if (!copied) {
        return;
    }
    log_viewport_once(w);
    ++g_frames;
    const double ms = 1000.0 * static_cast<double>(t1.QuadPart - t0.QuadPart) /
                      static_cast<double>(freq.QuadPart);
    g_ms_total += ms;
    g_ms_max = ms > g_ms_max ? ms : g_ms_max;
    if (g_frames % 50 == 0) {
        platform::log("b1 spike: " + std::to_string(g_frames) + " frames, render avg " +
                      std::to_string(g_ms_total / g_frames) + " ms, max " +
                      std::to_string(g_ms_max) + " ms (k=" + std::to_string(k) + ")");
    }
    if (k == 1) {
        compare_with_game(w, src);
    }
    if (!g_dumped && g_frames == 20) {
        g_dumped = true;
        dump_frames(w, src);
    }
}

} // namespace

void on_window_created(HWND window, const std::string& class_name) {
    if (settings().config.b1_spike == 0 || class_name != "Map3DClass") {
        return;
    }
    if (g_state == State::Unresolved) {
        resolve();
    }
    if (g_state != State::On) {
        return;
    }
    g_map3d = window;
    g_subclass = std::make_unique<platform::Subclass>(
        window, [](HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam, WNDPROC original) {
            if (msg == WM_DESTROY) {
                destroy_shadow();
                g_map3d = nullptr;
            }
            return CallWindowProcA(original, hwnd, msg, wparam, lparam);
        });
    platform::log("b1 spike: watching Map3DClass window");
}

void before_blit(HDC dest, HDC src) {
    if (g_state != State::On || g_map3d == nullptr || WindowFromDC(dest) != g_map3d) {
        return;
    }
    render(src);
}

} // namespace hr::wing32::rl_shadow
