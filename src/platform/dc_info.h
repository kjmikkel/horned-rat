#pragma once

#include <windows.h>

#include <string>

namespace hr::platform {

// "null", "window 0x... class 'Name'", "memory", or "type N" (GetObjectType value).
std::string describe_dc(HDC dc);

} // namespace hr::platform
