#pragma once

#include <cstdint>

// The last call received by the fake wing32_orig.dll.
struct FakeWingCall {
    char function[32];
    int argc;
    std::uintptr_t args[10];
};

// Exported by the fake as "FakeWing_LastCall".
using FakeWing_LastCall_t = const FakeWingCall*(__stdcall*)();
