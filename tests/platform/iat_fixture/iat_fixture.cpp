// A tiny DLL whose import the IAT hook tests redirect.
#include <windows.h>

extern "C" int WINAPI Fixture_Add(int a, int b) { return a + b; }
