#include "platform/iat_hook.h"

#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <string>

// Imported from iat_fixture.dll, so every call goes through this executable's IAT.
extern "C" __declspec(dllimport) int WINAPI Fixture_Add(int a, int b);

using hr::platform::hook_import;
using hr::platform::HookResult;

namespace {

int WINAPI fake_add(int a, int b) { return a * 100 + b; }

void* real_add() {
    return reinterpret_cast<void*>(
        GetProcAddress(GetModuleHandleW(L"iat_fixture.dll"), "Fixture_Add"));
}

} // namespace

TEST_CASE("hook_import redirects a module's import and can restore it", "[iat]") {
    const HMODULE exe = GetModuleHandleW(nullptr);
    REQUIRE(real_add() != nullptr);
    REQUIRE(Fixture_Add(2, 3) == 5);

    CHECK(hook_import(exe, "iat_fixture.dll", "Fixture_Add", reinterpret_cast<void*>(&fake_add)) ==
          HookResult::Patched);
    CHECK(Fixture_Add(2, 3) == 203);

    // Idempotent, and the DLL name is matched case-insensitively.
    CHECK(hook_import(exe, "IAT_FIXTURE.DLL", "Fixture_Add", reinterpret_cast<void*>(&fake_add)) ==
          HookResult::AlreadyPatched);

    CHECK(hook_import(exe, "iat_fixture.dll", "Fixture_Add", real_add()) == HookResult::Patched);
    CHECK(Fixture_Add(2, 3) == 5);
}

TEST_CASE("hook_import reports imports that do not exist", "[iat]") {
    const HMODULE exe = GetModuleHandleW(nullptr);
    const auto replacement = reinterpret_cast<void*>(&fake_add);
    CHECK(hook_import(exe, "iat_fixture.dll", "No_Such_Function", replacement) ==
          HookResult::NotImported);
    CHECK(hook_import(exe, "no_such.dll", "Fixture_Add", replacement) == HookResult::NotImported);
}

TEST_CASE("HookResult has readable names", "[iat]") {
    CHECK(std::string(hr::platform::to_string(HookResult::Patched)) == "patched");
    CHECK(std::string(hr::platform::to_string(HookResult::AlreadyPatched)) == "already patched");
    CHECK(std::string(hr::platform::to_string(HookResult::NotImported)) == "not imported");
    CHECK(std::string(hr::platform::to_string(HookResult::Failed)) == "failed");
}
