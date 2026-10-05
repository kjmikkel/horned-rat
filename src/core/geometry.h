#pragma once

namespace hr::core {

struct Size {
    int width = 0;
    int height = 0;

    friend bool operator==(const Size&, const Size&) = default;
};

} // namespace hr::core
