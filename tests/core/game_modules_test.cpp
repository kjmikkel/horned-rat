#include "core/game_modules.h"

#include <catch2/catch_test_macros.hpp>

using hr::core::is_game_module;
using hr::core::should_install_hooks;

TEST_CASE("modules inside the game folder are game modules", "[game_modules]") {
    const auto dir = R"(D:\Games\SotHR\WARFB)";
    CHECK(is_game_module(R"(D:\Games\SotHR\WARFB\WHSHR.EXE)", dir));
    CHECK(is_game_module(R"(D:\Games\SotHR\WARFB\RL\SOFTF.DLL)", dir));
    CHECK(is_game_module(R"(d:/games/sothr/warfb/gamef.dll)", dir));
    CHECK(is_game_module(R"(D:\Games\SotHR\WARFB\GAMEF.DLL)", R"(D:\Games\SotHR\WARFB\)"));
}

TEST_CASE("modules outside the game folder are not game modules", "[game_modules]") {
    const auto dir = R"(D:\Games\SotHR\WARFB)";
    CHECK_FALSE(is_game_module(R"(C:\Windows\SysWOW64\gdi32.dll)", dir));
    CHECK_FALSE(is_game_module(R"(D:\Games\SotHR\WARFB2\X.DLL)", dir));
    CHECK_FALSE(is_game_module(R"(D:\Games\SotHR\WARFB)", dir));
}

TEST_CASE("our own DLL and the original WinG are never hooked", "[game_modules]") {
    const auto dir = R"(D:\Games\SotHR\WARFB)";
    CHECK_FALSE(is_game_module(R"(D:\Games\SotHR\WARFB\wing32.dll)", dir));
    CHECK_FALSE(is_game_module(R"(D:\Games\SotHR\WARFB\WING32_ORIG.DLL)", dir));
}

TEST_CASE("hooks are installed only into the game, when enabled", "[game_modules]") {
    CHECK(should_install_hooks(R"(D:\x\WHSHR.EXE)", true, ""));
    CHECK(should_install_hooks(R"(D:\x\whshr.exe)", true, "0"));
    CHECK_FALSE(should_install_hooks(R"(D:\build\bin\platform_tests.exe)", true, ""));
    CHECK_FALSE(should_install_hooks(R"(D:\x\WHSHR.EXE)", false, ""));
    CHECK_FALSE(should_install_hooks(R"(D:\x\WHSHR.EXE)", true, "1"));
}
