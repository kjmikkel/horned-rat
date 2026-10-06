#include "platform/child_input.h"
#include "platform/hidden_window.h"
#include "test_window.h"

#include <catch2/catch_test_macros.hpp>

using namespace hr::platform;

namespace {

struct Tree {
    HWND root = hr::test::make_window(100, 100, 300, 200);
    HWND a = hr::test::make_window(50, 40, 100, 80, root);
    HWND b = hr::test::make_window(10, 10, 30, 30, a);
    Tree() { hr::test::pump(); }
    ~Tree() { DestroyWindow(root); }
};

bool got(HWND window, UINT msg, WPARAM wparam, LPARAM lparam) {
    for (const auto& m : hr::test::received()[window]) {
        if (m.msg == msg && m.wparam == wparam && m.lparam == lparam) {
            return true;
        }
    }
    return false;
}

} // namespace

TEST_CASE("deepest_child_at descends to the innermost visible child", "[child_input]") {
    Tree tree;
    auto hit = deepest_child_at(tree.root, {65, 55});
    CHECK(hit.window == tree.b);
    CHECK(hit.point.x == 5);
    CHECK(hit.point.y == 5);

    hit = deepest_child_at(tree.root, {55, 45});
    CHECK(hit.window == tree.a);
    CHECK(hit.point.x == 5);

    hit = deepest_child_at(tree.root, {5, 5});
    CHECK(hit.window == tree.root);

    ShowWindow(tree.b, SW_HIDE);
    CHECK(deepest_child_at(tree.root, {65, 55}).window == tree.a);
}

TEST_CASE("post_mouse delivers child-relative coordinates to the child under the point",
          "[child_input]") {
    Tree tree;
    post_mouse(tree.root, WM_LBUTTONDOWN, MK_LBUTTON, {65, 55});
    hr::test::pump();
    CHECK(got(tree.b, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5)));
}

TEST_CASE("post_mouse goes to the window holding mouse capture", "[child_input]") {
    Tree tree;
    SetCapture(tree.a);
    post_mouse(tree.root, WM_MOUSEMOVE, 0, {65, 55});
    hr::test::pump();
    ReleaseCapture();
    CHECK(got(tree.a, WM_MOUSEMOVE, 0, MAKELPARAM(15, 15)));
}

TEST_CASE("post_key goes to the window with keyboard focus", "[child_input]") {
    Tree tree;
    SetFocus(tree.b);
    post_key(tree.root, WM_CHAR, 'x', 1);
    hr::test::pump();
    CHECK(got(tree.b, WM_CHAR, 'x', 1));
}

TEST_CASE("children are found and reached when the root window is hidden", "[child_input]") {
    // Regression (A2.1 smoke test): ChildWindowFromPointEx finds nothing under a layered alpha-0
    // parent, so every forwarded click landed on the hidden main window instead of its children.
    Tree tree;
    RECT area{};
    SystemParametersInfoA(SPI_GETWORKAREA, 0, &area, 0);
    const HiddenState hidden = hide_window(tree.root, area);

    const auto hit = deepest_child_at(tree.root, {65, 55});
    CHECK(hit.window == tree.b);
    CHECK(hit.point.x == 5);
    CHECK(hit.point.y == 5);

    CHECK(post_mouse(tree.root, WM_LBUTTONDOWN, MK_LBUTTON, {65, 55}) == tree.b);
    hr::test::pump();
    CHECK(got(tree.b, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(5, 5)));
    unhide_window(tree.root, hidden);
}
