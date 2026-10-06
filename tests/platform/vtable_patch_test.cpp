#include "platform/vtable_patch.h"

#include <catch2/catch_test_macros.hpp>

namespace {
// A COM-like object: its first member points at a table of functions.
using Fn = int (*)(void*);
int original_a(void*) { return 1; }
int original_b(void*) { return 2; }
int replacement(void*) { return 99; }

struct Object {
    Fn* vtable;
};

int call(Object& object, int slot) { return object.vtable[slot](&object); }
} // namespace

TEST_CASE("a patched slot calls the replacement and remembers the original", "[vtable_patch]") {
    Fn table[2] = {&original_a, &original_b};
    Object object{table};
    hr::platform::VtablePatches patches;
    patches.patch(&object, 1, reinterpret_cast<void*>(&replacement));
    CHECK(call(object, 1) == 99);
    CHECK(call(object, 0) == 1);
    CHECK(patches.original(&object, 1) == reinterpret_cast<void*>(&original_b));
}

TEST_CASE("each vtable keeps its own original", "[vtable_patch]") {
    Fn first[1] = {&original_a};
    Fn second[1] = {&original_b};
    Object a{first};
    Object b{second};
    hr::platform::VtablePatches patches;
    patches.patch(&a, 0, reinterpret_cast<void*>(&replacement));
    patches.patch(&b, 0, reinterpret_cast<void*>(&replacement));
    CHECK(patches.original(&a, 0) == reinterpret_cast<void*>(&original_a));
    CHECK(patches.original(&b, 0) == reinterpret_cast<void*>(&original_b));
}

TEST_CASE("patching an already patched vtable keeps the first original", "[vtable_patch]") {
    Fn table[1] = {&original_a};
    Object one{table};
    Object two{table}; // same class, same vtable
    hr::platform::VtablePatches patches;
    patches.patch(&one, 0, reinterpret_cast<void*>(&replacement));
    patches.patch(&two, 0, reinterpret_cast<void*>(&replacement));
    CHECK(patches.original(&two, 0) == reinterpret_cast<void*>(&original_a));
}

TEST_CASE("an unpatched slot has no original", "[vtable_patch]") {
    Fn table[1] = {&original_a};
    Object object{table};
    hr::platform::VtablePatches patches;
    CHECK(patches.original(&object, 0) == nullptr);
}
