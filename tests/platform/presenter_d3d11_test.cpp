#include "platform/presenter_d3d11.h"
#include "test_window.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <vector>

using hr::platform::D3DPresenter;

namespace {

std::vector<std::uint32_t> pattern(int width, int height) {
    std::vector<std::uint32_t> pixels(static_cast<size_t>(width) * height);
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            pixels[static_cast<size_t>(y) * width + x] =
                (static_cast<std::uint32_t>(x * 16) << 16) |
                (static_cast<std::uint32_t>(y * 32) << 8) | 0x40u;
        }
    }
    return pixels;
}

std::unique_ptr<D3DPresenter> make(HWND window) {
    std::string error;
    auto presenter = D3DPresenter::create(window, {64, 48}, /*allow_hardware=*/false, error);
    INFO(error);
    REQUIRE(presenter);
    return presenter;
}

} // namespace

TEST_CASE("the presenter copies a frame 1:1 into the destination on black", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    CHECK(presenter->driver() == D3DPresenter::Driver::Warp);

    const auto frame = pattern(16, 8);
    presenter->render(frame.data(), {16, 8}, {10, 5, 16, 8}, D3DPresenter::Filter::Point);
    const auto pixels = presenter->read_back();
    REQUIRE(pixels.size() == 64u * 48u);

    int mismatches = 0;
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 64; ++x) {
            const bool inside = x >= 10 && x < 26 && y >= 5 && y < 13;
            const std::uint32_t expected =
                inside ? frame[static_cast<size_t>(y - 5) * 16 + (x - 10)] : 0u;
            mismatches += (pixels[static_cast<size_t>(y) * 64 + x] & 0xFFFFFF) != expected;
        }
    }
    CHECK(mismatches == 0);
    presenter->present(false);
    DestroyWindow(window);
}

TEST_CASE("frames partly outside the back buffer are clipped", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const auto frame = pattern(16, 8);
    presenter->render(frame.data(), {16, 8}, {-4, -2, 16, 8}, D3DPresenter::Filter::Point);
    const auto pixels = presenter->read_back();
    CHECK((pixels[0] & 0xFFFFFF) == frame[2 * 16 + 4]);
    CHECK((pixels[5 * 64 + 11] & 0xFFFFFF) == frame[7 * 16 + 15]);
    CHECK((pixels[0 * 64 + 12] & 0xFFFFFF) == 0u);
    DestroyWindow(window);
}

TEST_CASE("rendering without a frame just clears to black", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    presenter->render(nullptr, {0, 0}, {0, 0, 0, 0}, D3DPresenter::Filter::Point);
    const auto pixels = presenter->read_back();
    CHECK((pixels[10 * 64 + 10] & 0xFFFFFF) == 0u);
    DestroyWindow(window);
}

namespace {
std::uint32_t at(const std::vector<std::uint32_t>& pixels, int width, int x, int y) {
    return pixels[static_cast<size_t>(y) * width + x] & 0xFFFFFF;
}
} // namespace

TEST_CASE("point scaling at 2x repeats every texel as a 2x2 block", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const auto frame = pattern(16, 8);
    presenter->render(frame.data(), {16, 8}, {4, 6, 32, 16}, D3DPresenter::Filter::Point);
    const auto pixels = presenter->read_back();
    int mismatches = 0;
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 64; ++x) {
            const bool inside = x >= 4 && x < 36 && y >= 6 && y < 22;
            const std::uint32_t expected =
                inside ? frame[static_cast<size_t>((y - 6) / 2) * 16 + (x - 4) / 2] : 0u;
            mismatches += at(pixels, 64, x, y) != expected;
        }
    }
    CHECK(mismatches == 0);
    DestroyWindow(window);
}

TEST_CASE("point scaling at a fractional factor picks the texel under each pixel centre",
          "[presenter_d3d11]") {
    // Odd frame sizes and even destination sizes: no pixel centre falls on a texel edge.
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const auto frame = pattern(15, 7);
    presenter->render(frame.data(), {15, 7}, {3, 2, 40, 20}, D3DPresenter::Filter::Point);
    const auto pixels = presenter->read_back();
    int mismatches = 0;
    for (int y = 0; y < 48; ++y) {
        for (int x = 0; x < 64; ++x) {
            const bool inside = x >= 3 && x < 43 && y >= 2 && y < 22;
            std::uint32_t expected = 0;
            if (inside) {
                const int sx = ((2 * (x - 3) + 1) * 15) / 80;
                const int sy = ((2 * (y - 2) + 1) * 7) / 40;
                expected = frame[static_cast<size_t>(sy) * 15 + sx];
            }
            mismatches += at(pixels, 64, x, y) != expected;
        }
    }
    CHECK(mismatches == 0);
    DestroyWindow(window);
}

TEST_CASE("resizing changes the back buffer size and drawing follows", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    REQUIRE(presenter->resize({100, 80}));
    CHECK(presenter->size() == hr::core::Size{100, 80});
    const auto frame = pattern(16, 8);
    presenter->render(frame.data(), {16, 8}, {70, 60, 16, 8}, D3DPresenter::Filter::Point);
    const auto pixels = presenter->read_back();
    REQUIRE(pixels.size() == 100u * 80u);
    CHECK(at(pixels, 100, 85, 67) == frame[7 * 16 + 15]);
    CHECK(presenter->resize({0, 0}));
    CHECK(presenter->size() == hr::core::Size{100, 80});
    DestroyWindow(window);
}