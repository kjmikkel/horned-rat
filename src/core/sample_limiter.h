#pragma once

#include <cstdint>

namespace hr::core {

// Decides which calls of a frequently called function get logged: the first `first` calls,
// then every `every`-th call. Call numbers start at 1.
struct SampleLimiter {
    std::uint64_t first = 50;
    std::uint64_t every = 1000;

    constexpr bool should_log(std::uint64_t call_number) const {
        return call_number <= first || call_number % every == 0;
    }
};

} // namespace hr::core
