// Dev tool for the A2.1 fade check: records the mean brightness of the game's composed picture.
//   luma_probe <seconds> <output.csv>
// Start it before the game: it waits up to two minutes for the game window. Each run is appended
// to the file, tagged with its start time and mode (vanilla, or a2 when the game window is hidden),
// so the vanilla and the A2 run end up in one file. Works for both; never commit its output.
#include "core/luma.h"
#include "platform/window_capture.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

namespace {

HWND wait_for_game(std::chrono::seconds limit) {
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
        if (const HWND game = FindWindowA("Glue", nullptr)) {
            return game;
        }
        if (std::chrono::steady_clock::now() - start >= limit) {
            return nullptr;
        }
        Sleep(20);
    }
}

std::string local_time_now() {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char text[32];
    std::strftime(text, sizeof text, "%Y-%m-%d %H:%M:%S", &local);
    return text;
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fputs("usage: luma_probe <seconds> <output.csv>\n", stderr);
        return 2;
    }
    FILE* out = nullptr;
    if (fopen_s(&out, argv[2], "a") != 0 || out == nullptr) {
        std::fputs("cannot open output file\n", stderr);
        return 1;
    }
    std::fseek(out, 0, SEEK_END);
    if (std::ftell(out) == 0) {
        std::fprintf(out, "%s\n", hr::core::kLumaCsvHeader);
    }

    std::puts("waiting for the game window...");
    const HWND game = wait_for_game(std::chrono::minutes(2));
    if (game == nullptr) {
        std::fclose(out);
        std::fputs("game window (class 'Glue') not found\n", stderr);
        return 1;
    }
    const std::string run = local_time_now();
    std::printf("recording run %s\n", run.c_str());

    hr::platform::WindowCapture capture;
    const auto start = std::chrono::steady_clock::now();
    const double seconds = std::atof(argv[1]);
    int samples = 0;
    const char* mode = "vanilla";
    for (;;) {
        const double t =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (t >= seconds || !IsWindow(game)) {
            break;
        }
        // A2 hides the game window (layered) right after it is created; checked per sample.
        if ((GetWindowLongPtrA(game, GWL_EXSTYLE) & WS_EX_LAYERED) != 0) {
            mode = "a2";
        }
        RECT client{};
        GetClientRect(game, &client);
        if (capture.capture(game, {0, 0, client.right, client.bottom})) {
            const double luma =
                hr::core::mean_luma(capture.pixels(), capture.size().width, capture.size().height);
            std::fprintf(out, "%s\n", hr::core::luma_csv_row(run, mode, t, luma).c_str());
            ++samples;
        }
        Sleep(5);
    }
    std::fclose(out);
    std::printf("%d samples (%s) appended to %s\n", samples, mode, argv[2]);
    return 0;
}
