// Dev tool for the A2.1 fade check: records the mean brightness of the game's composed picture.
//   luma_probe <seconds> <output.csv>
// Works for vanilla (window on-screen) and with A2 (window hidden on-screen). Never commit its
// output.
#include "core/luma.h"
#include "platform/window_capture.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <cstdlib>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fputs("usage: luma_probe <seconds> <output.csv>\n", stderr);
        return 2;
    }
    const HWND game = FindWindowA("Glue", nullptr);
    if (game == nullptr) {
        std::fputs("game window (class 'Glue') not found\n", stderr);
        return 1;
    }
    FILE* out = nullptr;
    if (fopen_s(&out, argv[2], "w") != 0 || out == nullptr) {
        std::fputs("cannot open output file\n", stderr);
        return 1;
    }
    std::fputs("t,luma\n", out);
    hr::platform::WindowCapture capture;
    const auto start = std::chrono::steady_clock::now();
    const double seconds = std::atof(argv[1]);
    int samples = 0;
    for (;;) {
        const double t =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        if (t >= seconds) {
            break;
        }
        RECT client{};
        GetClientRect(game, &client);
        if (capture.capture(game, {0, 0, client.right, client.bottom})) {
            std::fprintf(
                out, "%.4f,%.2f\n", t,
                hr::core::mean_luma(capture.pixels(), capture.size().width, capture.size().height));
            ++samples;
        }
        Sleep(5);
    }
    std::fclose(out);
    std::printf("%d samples written to %s\n", samples, argv[2]);
    return 0;
}
