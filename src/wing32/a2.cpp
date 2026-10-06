#include "wing32/a2.h"

#include "core/content_rect.h"
#include "core/input_map.h"
#include "core/viewport.h"
#include "platform/child_input.h"
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
#include <optional>
#include <string>
#include <thread>

namespace hr::wing32::a2 {
namespace {

using Clock = std::chrono::steady_clock;
constexpr core::Size kPresenterSize{640, 480};

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

    void on_mouse(UINT msg, WPARAM wparam, POINT client) override {
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
        if (g_active && core::classify(msg, wparam) == core::Forward::Keyboard) {
            platform::post_key(g_glue, msg, wparam, lparam);
        }
    }
    void on_close() override { PostMessageA(g_glue, WM_CLOSE, 0, 0); }
    HCURSOR cursor() override { return g_cursor; }

  private:
    core::Rect dest_{0, 0, 640, 480};
};

void stop_with_unhide(HWND glue, const platform::HiddenState& state, const std::string& reason) {
    g_active = false;
    platform::unhide_window(glue, state);
    platform::log("presenter: game window unhidden (" + reason + ")");
}

void presenter_main() {
    const HWND glue = g_glue;
    platform::HiddenState hidden_state;
    try {
        const core::Config& config = settings().config;
        Input input;
        platform::PresenterWindow window(input, kPresenterSize, "Shadow of the Horned Rat");
        g_presenter = window.hwnd();

        std::string error;
        auto d3d = platform::D3DPresenter::create(window.hwnd(), kPresenterSize, true, error);
        if (!d3d) {
            platform::log("presenter: disabled, " + error);
            return;
        }
        platform::log(
            std::string("presenter: D3D11 ") +
            (d3d->driver() == platform::D3DPresenter::Driver::Hardware ? "hardware" : "WARP"));

        platform::WindowCapture capture;
        store_content_rect(find_content_rect(glue));
        if (!capture.capture(glue, content_rect())) {
            platform::log("presenter: disabled, first capture failed");
            return;
        }

        // Never destroyed: the game thread may be inside it at any time until the process ends.
        static platform::Subclass subclass(glue, &glue_proc);

        MONITORINFO monitor{};
        monitor.cbSize = sizeof monitor;
        GetMonitorInfoA(MonitorFromWindow(glue, MONITOR_DEFAULTTOPRIMARY), &monitor);
        hidden_state = platform::hide_window(glue, monitor.rcWork);
        RECT hidden{};
        GetWindowRect(glue, &hidden);
        g_hidden_x = hidden.left;
        g_hidden_y = hidden.top;
        g_active = true;
        window.show();
        platform::log("presenter: running, game window hidden at (" + std::to_string(hidden.left) +
                      "," + std::to_string(hidden.top) + ")");

        auto last_good = Clock::now();
        auto stats_start = Clock::now();
        double capture_ms = 0.0;
        double present_ms = 0.0;
        int frames = 0;
        while (!g_stop) {
            MSG msg;
            while (PeekMessageA(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageA(&msg);
            }
            const core::Rect content = find_content_rect(glue);
            store_content_rect(content);
            const core::Rect dest =
                core::centered_rect(kPresenterSize, {content.width, content.height});
            input.set_dest(dest);

            const auto t0 = Clock::now();
            if (capture.capture(glue, content)) {
                last_good = t0;
            } else if (t0 - last_good > std::chrono::seconds(2)) {
                stop_with_unhide(glue, hidden_state, "capture failed for 2 s");
                return;
            }
            const auto t1 = Clock::now();
            d3d->render(capture.pixels(), capture.size(), dest);
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
            if (!config.vsync) {
                Sleep(1);
            }
        }
        platform::log("presenter: game window destroyed, stopping");
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
    platform::log("presenter: starting for the main window");
    std::thread(presenter_main).detach();
}

bool active() { return g_active; }

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

} // namespace hr::wing32::a2
