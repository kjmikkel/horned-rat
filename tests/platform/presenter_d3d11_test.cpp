#include "platform/presenter_d3d11.h"
#include "test_window.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
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
namespace {
int channel(std::uint32_t pixel, int shift) { return static_cast<int>((pixel >> shift) & 0xFF); }
bool near_equal(std::uint32_t a, std::uint32_t b) {
    for (const int shift : {16, 8, 0}) {
        if (std::abs(channel(a, shift) - channel(b, shift)) > 1) {
            return false;
        }
    }
    return true;
}
} // namespace

TEST_CASE("sharp scaling at a whole factor equals point scaling", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const auto frame = pattern(16, 8);
    presenter->render(frame.data(), {16, 8}, {4, 6, 32, 16}, D3DPresenter::Filter::Sharp);
    const auto pixels = presenter->read_back();
    int mismatches = 0;
    for (int y = 6; y < 22; ++y) {
        for (int x = 4; x < 36; ++x) {
            mismatches += !near_equal(at(pixels, 64, x, y),
                                      frame[static_cast<size_t>((y - 6) / 2) * 16 + (x - 4) / 2]);
        }
    }
    CHECK(mismatches == 0);
    DestroyWindow(window);
}

TEST_CASE("sharp scaling at a fractional factor keeps texel centres and blends only at edges",
          "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const auto frame = pattern(15, 7);
    presenter->render(frame.data(), {15, 7}, {3, 2, 40, 20}, D3DPresenter::Filter::Sharp);
    const auto pixels = presenter->read_back();
    int exact = 0;
    int blended = 0;
    int out_of_range = 0;
    for (int y = 0; y < 20; ++y) {
        for (int x = 0; x < 40; ++x) {
            const int sx = ((2 * x + 1) * 15) / 80;
            const int sy = ((2 * y + 1) * 7) / 40;
            const std::uint32_t p = at(pixels, 64, x + 3, y + 2);
            for (const int shift : {16, 8, 0}) {
                int lo = 255;
                int hi = 0;
                for (int ny = std::max(sy - 1, 0); ny <= std::min(sy + 1, 6); ++ny) {
                    for (int nx = std::max(sx - 1, 0); nx <= std::min(sx + 1, 14); ++nx) {
                        const int c = channel(frame[static_cast<size_t>(ny) * 15 + nx], shift);
                        lo = std::min(lo, c);
                        hi = std::max(hi, c);
                    }
                }
                out_of_range += channel(p, shift) < lo - 1 || channel(p, shift) > hi + 1;
            }
            if (near_equal(p, frame[static_cast<size_t>(sy) * 15 + sx])) {
                ++exact;
            } else {
                ++blended;
            }
        }
    }
    CHECK(out_of_range == 0);
    CHECK(exact >= 40 * 20 / 5); // the centre regions are 1/4 of the area at prescale 2
    CHECK(blended > 0);          // and it is not plain nearest neighbour
    DestroyWindow(window);
}
TEST_CASE("cursor sprites are alpha-blended over the frame, scaled and clipped",
          "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const std::vector<std::uint32_t> frame(64 * 48, 0x00102030u);
    // 2x2: opaque red, transparent, half green, opaque blue
    const std::uint32_t sprite[] = {0xFFFF0000u, 0x00000000u, 0x8000FF00u, 0xFF0000FFu};
    CHECK_FALSE(presenter->has_sprite(7));
    REQUIRE(presenter->add_sprite(7, sprite, {2, 2}));
    CHECK(presenter->has_sprite(7));

    presenter->render(frame.data(), {64, 48}, {0, 0, 64, 48}, D3DPresenter::Filter::Point);
    CHECK(presenter->draw_sprite(7, {10, 10, 4, 4}));
    CHECK(presenter->draw_sprite(7, {-2, -2, 4, 4})); // only its bottom-right texel is visible
    CHECK_FALSE(presenter->draw_sprite(8, {0, 0, 4, 4}));
    const auto pixels = presenter->read_back();
    REQUIRE(pixels.size() == 64u * 48u);
    const auto at = [&](int x, int y) {
        return pixels[static_cast<size_t>(y) * 64 + static_cast<size_t>(x)] & 0xFFFFFFu;
    };

    CHECK(at(10, 10) == 0xFF0000u);
    CHECK(at(11, 11) == 0xFF0000u);
    CHECK(at(12, 10) == 0x102030u); // transparent texel
    CHECK(at(13, 13) == 0x0000FFu);
    const std::uint32_t half = at(10, 12); // 0x00FF00 at 128/255 over 0x102030
    CHECK(std::abs(channel(half, 16) - 0x08) <= 2);
    CHECK(std::abs(channel(half, 8) - 0x90) <= 2);
    CHECK(std::abs(channel(half, 0) - 0x18) <= 2);
    CHECK(at(0, 0) == 0x0000FFu);
    CHECK(at(2, 2) == 0x102030u);
    CHECK(at(14, 14) == 0x102030u);
    presenter->present(false);
    DestroyWindow(window);
}

TEST_CASE("the next frame is drawn opaque again after a sprite", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const std::uint32_t sprite[] = {0x80FFFFFFu};
    REQUIRE(presenter->add_sprite(1, sprite, {1, 1}));
    const std::vector<std::uint32_t> frame(64 * 48, 0x00405060u);
    presenter->render(frame.data(), {64, 48}, {0, 0, 64, 48}, D3DPresenter::Filter::Point);
    presenter->draw_sprite(1, {0, 0, 8, 8});
    presenter->render(frame.data(), {64, 48}, {0, 0, 64, 48}, D3DPresenter::Filter::Point);
    const auto pixels = presenter->read_back();
    CHECK((pixels[0] & 0xFFFFFFu) == 0x405060u);
    presenter->present(false);
    DestroyWindow(window);
}

TEST_CASE("the overlay is blended over the frame where it is opaque", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const std::vector<std::uint32_t> frame(64 * 48, 0x00102030u);
    // 2x2: opaque red, transparent (colour kept), opaque green, opaque blue
    const std::uint32_t overlay[] = {0xFFFF0000u, 0x00FF0000u, 0xFF00FF00u, 0xFF0000FFu};
    REQUIRE(presenter->set_overlay(overlay, {2, 2}));
    presenter->render(frame.data(), {64, 48}, {0, 0, 64, 48}, D3DPresenter::Filter::Point);
    presenter->draw_overlay({8, 8, 4, 4}, D3DPresenter::Filter::Point);
    const auto pixels = presenter->read_back();
    REQUIRE(pixels.size() == 64u * 48u);
    CHECK(at(pixels, 64, 8, 8) == 0xFF0000u);
    CHECK(at(pixels, 64, 9, 9) == 0xFF0000u);
    CHECK(at(pixels, 64, 10, 8) == 0x102030u); // transparent texel
    CHECK(at(pixels, 64, 8, 10) == 0x00FF00u);
    CHECK(at(pixels, 64, 11, 11) == 0x0000FFu);
    CHECK(at(pixels, 64, 12, 12) == 0x102030u); // outside
    presenter->present(false);
    DestroyWindow(window);
}

TEST_CASE("drawing without an overlay leaves the frame alone", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const std::vector<std::uint32_t> frame(64 * 48, 0x00405060u);
    presenter->render(frame.data(), {64, 48}, {0, 0, 64, 48}, D3DPresenter::Filter::Point);
    presenter->draw_overlay({0, 0, 64, 48}, D3DPresenter::Filter::Point);
    const auto pixels = presenter->read_back();
    CHECK(at(pixels, 64, 5, 5) == 0x405060u);
    DestroyWindow(window);
}

TEST_CASE("an overlay of a new size replaces the old one", "[presenter_d3d11]") {
    const HWND window = hr::test::make_window(0, 0, 64, 48);
    auto presenter = make(window);
    const std::vector<std::uint32_t> frame(64 * 48, 0u);
    const std::uint32_t one_texel[] = {0xFFFF0000u};
    const std::uint32_t four_texels[] = {0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u, 0xFF00FF00u};
    REQUIRE(presenter->set_overlay(one_texel, {1, 1}));
    REQUIRE(presenter->set_overlay(four_texels, {2, 2}));
    presenter->render(frame.data(), {64, 48}, {0, 0, 64, 48}, D3DPresenter::Filter::Point);
    presenter->draw_overlay({0, 0, 4, 4}, D3DPresenter::Filter::Point);
    const auto pixels = presenter->read_back();
    CHECK(at(pixels, 64, 3, 3) == 0x00FF00u);
    DestroyWindow(window);
}
