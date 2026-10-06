#include "wing32/a2.h"

#include "core/content_rect.h"
#include "core/input_map.h"
#include "core/scaling.h"
#include "core/viewport.h"
#include "platform/child_input.h"
#include "platform/dpi_context.h"
#include "platform/hidden_window.h"
#include "platform/log.h"
#include "platform/presenter_d3d11.h"
#include "platform/presenter_window.h"
#include "platform/window_capture.h"
#include "platform/window_subclass.h"
#include "wing32/runtime.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <exception>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

namespace hr::wing32::a2 {
namespace {

using Clock = std::chrono::steady_clock;

std::atomic<HWND> g_glue{nullptr};
std::atomic<HWND> g_presenter{nullptr};
std::atomic<bool> g_started{false};
std::atomic<bool> g_active{false};
std::atomic<bool> g_stop{false};
std::atomic<HCURSOR> g_cursor{nullptr};
std::atomic<int> g_cursor_x{0};
std::atomic<int> g_cursor_y{0};
std::atomic<int> g_content_x{0};
std::atomic<int> g_content_y{0};
std::atomic<int> g_content_w{640};
std::atomic<int> g_content_h{480};
std::atomic<int> g_hidden_x{0}; // hidden position, enforced while active
std::atomic<int> g_hidden_y{0};
// Written on the game thread before the presenter thread starts (thread start orders the access).
platform::HiddenState g_hidden_state;
std::atomic<bool> g_gave_up{false};
// The game's DPI context. Presenter-window messages run per-monitor aware; anything touching the
// game's windows from them switches back to this, so the game's coordinates stay its own.
std::atomic<DPI_AWARENESS_CONTEXT> g_game_dpi{nullptr};
std::mutex g_display_mutex;
std::optional<DEVMODEA> g_deferred_mode; // guarded by g_display_mutex; nullopt = registry mode
DWORD g_deferred_flags = 0;
bool g_display_deferred = false;

std::string class_of(HWND window) {
    if (window == nullptr) {
        return "(none)";
    }
    char name[64] = {};
    GetClassNameA(window, name, sizeof name);
    return name;
}

const char* activation_name(UINT msg) {
    switch (msg) {
    case WM_ACTIVATE:
        return "WM_ACTIVATE";
    case WM_ACTIVATEAPP:
        return "WM_ACTIVATEAPP";
    case WM_NCACTIVATE:
        return "WM_NCACTIVATE";
    case WM_SETFOCUS:
        return "WM_SETFOCUS";
    case WM_KILLFOCUS:
        return "WM_KILLFOCUS";
    default:
        return nullptr;
    }
}

core::Rect content_rect() {
    return {g_content_x.load(), g_content_y.load(), g_content_w.load(), g_content_h.load()};
}

core::Rect find_content_rect(HWND glue) {
    RECT client{};
    GetClientRect(glue, &client);
    std::optional<core::Rect> child;
    if (const HWND screen = FindWindowExA(glue, nullptr, "BitmapScrn", nullptr)) {
        RECT r{};
        GetWindowRect(screen, &r);
        MapWindowPoints(HWND_DESKTOP, glue, reinterpret_cast<POINT*>(&r), 2);
        child = core::Rect{r.left, r.top, r.right - r.left, r.bottom - r.top};
    }
    return core::choose_content_rect({client.right, client.bottom}, child);
}

void store_content_rect(core::Rect r) {
    g_content_x = r.x;
    g_content_y = r.y;
    g_content_w = r.width;
    g_content_h = r.height;
}

// Runs on the game thread for every message of the main window (A2 spec §5.4).
LRESULT glue_proc(HWND window, UINT msg, WPARAM wparam, LPARAM lparam, WNDPROC original) {
    if (const char* name = activation_name(msg)) {
        const WPARAM filtered = g_active ? core::filter_activation(msg, wparam) : wparam;
        platform::log(std::string("diag: Glue ") + name + " wparam=" + std::to_string(wparam) +
                      " -> " + std::to_string(filtered) +
                      " foreground=" + class_of(GetForegroundWindow()));
    }
    if (g_active) {
        if (msg == WM_ACTIVATE || msg == WM_ACTIVATEAPP) {
            wparam = core::filter_activation(msg, wparam);
        } else if (msg == WM_WINDOWPOSCHANGING) {
            auto* const pos = reinterpret_cast<WINDOWPOS*>(lparam);
            if ((pos->flags & SWP_NOMOVE) == 0) {
                pos->x = g_hidden_x;
                pos->y = g_hidden_y;
            }
        }
    }
    if (msg == WM_DESTROY) {
        g_active = false;
        g_stop = true;
    }
    return CallWindowProcA(original, window, msg, wparam, lparam);
}

class Input final : public platform::PresenterInput {
  public:
    void set_dest(core::Rect dest) { dest_ = dest; }
    void set_tick(std::function<void()> tick) { tick_ = std::move(tick); }

    void on_mouse(UINT msg, WPARAM wparam, POINT client) override {
        const platform::ScopedDpiContext game_dpi(g_game_dpi.load());
        const core::Rect content = content_rect();
        const core::Point g =
            core::to_content({client.x, client.y}, dest_, {content.width, content.height});
        g_cursor_x = g.x;
        g_cursor_y = g.y;
        if (g_active && core::classify(msg, wparam) == core::Forward::Mouse) {
            platform::post_mouse(g_glue, msg, wparam, POINT{content.x + g.x, content.y + g.y});
        }
    }
    void on_key(UINT msg, WPARAM wparam, LPARAM lparam) override {
        const platform::ScopedDpiContext game_dpi(g_game_dpi.load());
        if (g_active && core::classify(msg, wparam) == core::Forward::Keyboard) {
            platform::post_key(g_glue, msg, wparam, lparam);
        }
    }
    void on_close() override { PostMessageA(g_glue, WM_CLOSE, 0, 0); }
    void on_tick() override {
        const platform::ScopedDpiContext game_dpi(g_game_dpi.load());
        if (tick_) {
            tick_();
        }
    }
    HCURSOR cursor() override { return g_cursor; }

  private:
    core::Rect dest_{0, 0, 640, 480};
    std::function<void()> tick_;
};

void stop_with_unhide(HWND glue, const platform::HiddenState& state, const std::string& reason) {
    g_active = false;
    g_gave_up = true;
    platform::unhide_window(glue, state);
    platform::log("presenter: game window unhidden (" + reason + ")");

    const std::lock_guard lock(g_display_mutex);
    if (g_display_deferred) {
        DEVMODEA mode = g_deferred_mode.value_or(DEVMODEA{});
        const LONG result =
            ChangeDisplaySettingsA(g_deferred_mode ? &mode : nullptr, g_deferred_flags);
        platform::log("presenter: applied the game's deferred display mode -> " +
                      std::to_string(result));
    }
}

void presenter_body();

void presenter_main() {
    presenter_body();
    platform::log("diag: presenter thread finished (window and device released)");
}

const char* scale_name(core::ScaleMode scale) {
    switch (scale) {
    case core::ScaleMode::Integer:
        return "integer";
    case core::ScaleMode::Sharp:
        return "sharp";
    case core::ScaleMode::NearestFit:
        return "nearest-fit";
    }
    return "?";
}

void presenter_body() {
    g_game_dpi = platform::current_dpi_context();
    const HWND glue = g_glue;
    const platform::HiddenState& hidden_state = g_hidden_state;
    try {
        const core::Config& config = settings().config;
        Input input;
        platform::PresenterWindow window(input, config.window_size, "Shadow of the Horned Rat");
        g_presenter = window.hwnd();
        if (config.mode == core::DisplayMode::Borderless) {
            window.set_borderless(true);
        }

        std::string error;
        auto d3d = platform::D3DPresenter::create(window.hwnd(), window.client_size(), true, error);
        if (!d3d) {
            stop_with_unhide(glue, hidden_state, "presenter disabled, " + error);
            return;
        }
        platform::log(
            std::string("presenter: D3D11 ") +
            (d3d->driver() == platform::D3DPresenter::Driver::Hardware ? "hardware" : "WARP"));

        platform::WindowCapture capture;
        store_content_rect(find_content_rect(glue));
        if (!capture.capture(glue, content_rect())) {
            stop_with_unhide(glue, hidden_state, "presenter disabled, first capture failed");
            return;
        }

        // Never destroyed: the game thread may be inside it at any time until the process ends.
        static platform::Subclass subclass(glue, &glue_proc);

        RECT hidden{};
        GetWindowRect(glue, &hidden);
        g_hidden_x = hidden.left;
        g_hidden_y = hidden.top;
        g_active = true;
        window.show();
        platform::log("presenter: running, game window hidden at (" + std::to_string(hidden.left) +
                      "," + std::to_string(hidden.top) + ")");

        const auto filter = config.scale == core::ScaleMode::Sharp
                                ? platform::D3DPresenter::Filter::Sharp
                                : platform::D3DPresenter::Filter::Point;
        std::string failure; // set when the presenter must give up (spec §7)
        core::Rect last_dest{-1, -1, -1, -1};
        auto last_good = Clock::now();
        auto stats_start = Clock::now();
        double capture_ms = 0.0;
        double present_ms = 0.0;
        int frames = 0;

        // One frame: follow the window size, capture, scale, present. Runs from the loop below
        // and from on_tick while the user drags the window frame.
        const auto frame = [&] {
            if (!failure.empty()) {
                return;
            }
            const core::Size client = window.client_size();
            if (!d3d->resize(client)) {
                failure = "swap chain resize failed";
                return;
            }
            const core::Rect content = find_content_rect(glue);
            store_content_rect(content);
            const core::Rect dest = core::dest_rect(client, {content.width, content.height},
                                                    config.scale, config.aspect);
            input.set_dest(dest);
            if (dest != last_dest) {
                char line[160];
                std::snprintf(line, sizeof line,
                              "presenter: client %dx%d, scale %s, dest (%d,%d %dx%d)", client.width,
                              client.height, scale_name(config.scale), dest.x, dest.y, dest.width,
                              dest.height);
                platform::log(line);
                last_dest = dest;
            }

            const auto t0 = Clock::now();
            if (capture.capture(glue, content)) {
                last_good = t0;
            } else if (t0 - last_good > std::chrono::seconds(2)) {
                failure = "capture failed for 2 s";
                return;
            }
            const auto t1 = Clock::now();
            d3d->render(capture.pixels(), capture.size(), dest, filter);
            d3d->present(config.vsync);
            const auto t2 = Clock::now();

            capture_ms += std::chrono::duration<double, std::milli>(t1 - t0).count();
            present_ms += std::chrono::duration<double, std::milli>(t2 - t1).count();
            ++frames;
            if (t2 - stats_start >= std::chrono::seconds(10)) {
                char line[160];
                std::snprintf(line, sizeof line,
                              "presenter: %d frames in 10 s, capture %.2f ms, present %.2f ms",
                              frames, capture_ms / frames, present_ms / frames);
                platform::log(line);
                stats_start = t2;
                capture_ms = present_ms = 0.0;
                frames = 0;
            }
        };
        input.set_tick(frame);

        while (!g_stop && failure.empty()) {
            MSG msg;
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            frame();
            if (!config.vsync) {
                Sleep(1);
            }
        }
        input.set_tick(nullptr);
        if (!failure.empty()) {
            stop_with_unhide(glue, hidden_state, failure);
            return;
        }
        platform::log("presenter: game window destroyed, stopping");
        g_presenter = nullptr;
    } catch (const std::exception& e) {
        platform::log_exception("presenter", e.what());
        if (!g_stop) {
            stop_with_unhide(glue, hidden_state, "exception");
        }
    } catch (...) {
        platform::log_exception("presenter", nullptr);
        if (!g_stop) {
            stop_with_unhide(glue, hidden_state, "exception");
        }
    }
}

} // namespace

void on_window_created(HWND window, HWND parent, DWORD style) {
    if (parent != nullptr || (style & WS_CHILD) != 0) {
        return;
    }
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true)) {
        return;
    }
    g_glue = window;
    // Hidden here, on the game thread before the game first shows the window, so it never flashes
    // on screen while the presenter starts up. Every presenter failure unhides it again.
    MONITORINFO monitor{};
    monitor.cbSize = sizeof monitor;
    GetMonitorInfoA(MonitorFromWindow(window, MONITOR_DEFAULTTOPRIMARY), &monitor);
    g_hidden_state = platform::hide_window(window, monitor.rcWork);
    platform::log("presenter: starting for the main window (hidden at creation)");
    std::thread(presenter_main).detach();
}

bool active() { return g_active; }

bool owns_display() { return g_started && !g_gave_up; }

void defer_display_mode(const DEVMODEA* mode, DWORD flags) {
    const std::lock_guard lock(g_display_mutex);
    if (mode != nullptr) {
        g_deferred_mode = *mode;
        g_deferred_mode->dmSize = sizeof(DEVMODEA);
        g_deferred_mode->dmDriverExtra = 0; // only the public part is copied
    } else {
        g_deferred_mode.reset();
    }
    g_deferred_flags = flags;
    g_display_deferred = true;
}

HWND presenter_window() { return g_active ? g_presenter.load() : nullptr; }

void fake_cursor_pos(POINT& out) {
    POINT origin{0, 0};
    ClientToScreen(g_glue, &origin);
    const core::Point p = core::fake_cursor_screen({origin.x, origin.y}, content_rect(),
                                                   {g_cursor_x.load(), g_cursor_y.load()});
    out = POINT{p.x, p.y};
}

bool mask_button(int vk) {
    const HWND presenter = g_presenter;
    if (!g_active || presenter == nullptr || !core::is_mouse_button(vk)) {
        return false;
    }
    POINT cursor{};
    GetCursorPos(&cursor); // our own import: the real position
    bool in_client = WindowFromPoint(cursor) == presenter;
    if (in_client) {
        POINT client = cursor;
        ScreenToClient(presenter, &client);
        RECT r{};
        GetClientRect(presenter, &r);
        in_client = PtInRect(&r, client) != FALSE;
    }
    return !core::report_button(GetForegroundWindow() == presenter, in_client);
}

void set_game_cursor(HCURSOR cursor) { g_cursor = cursor; }

void note_button(int vk, bool physically_down, bool masked) {
    static std::atomic<int> last[8]{};
    if (vk < 0 || vk >= 8) {
        return;
    }
    const int now = (physically_down ? 1 : 0) | (masked ? 2 : 0) | 4;
    if (last[vk].exchange(now) == now) {
        return;
    }
    POINT cursor{};
    GetCursorPos(&cursor);
    platform::log("diag: button vk=" + std::to_string(vk) + (physically_down ? " down" : " up") +
                  (masked ? " MASKED" : " passed") +
                  " foreground=" + class_of(GetForegroundWindow()) +
                  " under_cursor=" + class_of(WindowFromPoint(cursor)));
}

} // namespace hr::wing32::a2
