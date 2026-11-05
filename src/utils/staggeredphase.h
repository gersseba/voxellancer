#pragma once

#include <cstdint>
#include <limits>

namespace StaggeredPhase {
    inline float forObject(const void* object, float interval) {
        // Spread periodic work across frames without consuming gameplay randomness.
        const std::uint64_t address =
            static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(object));
        std::uint32_t hash = static_cast<std::uint32_t>(address ^ (address >> 32));
        hash ^= hash >> 16;
        hash *= 0x7feb352d;
        hash ^= hash >> 15;
        hash *= 0x846ca68b;
        hash ^= hash >> 16;
        return static_cast<float>(hash) /
            static_cast<float>(std::numeric_limits<std::uint32_t>::max()) * interval;
    }
}
