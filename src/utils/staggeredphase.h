#pragma once

#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

namespace StaggeredPhase {
    inline std::uint32_t mix(std::uint32_t hash) {
        hash ^= hash >> 16;
        hash *= 0x7feb352d;
        hash ^= hash >> 15;
        hash *= 0x846ca68b;
        hash ^= hash >> 16;
        return hash;
    }

    inline std::uint32_t keyForPosition(const glm::vec3& position) {
        const auto quantize = [](float coordinate) {
            return static_cast<std::uint32_t>(
                static_cast<std::int32_t>(std::lround(coordinate * 256.0f)));
        };
        std::uint32_t hash = mix(quantize(position.x));
        hash = mix(hash ^ quantize(position.y));
        return mix(hash ^ quantize(position.z));
    }

    inline std::uint32_t initialPhaseTicks(
        std::uint32_t key,
        std::uint32_t intervalTicks
    ) {
        return intervalTicks == 0 ? 0 : mix(key) % intervalTicks;
    }

    inline std::uint32_t periodTicks(
        std::uint32_t key,
        std::uint32_t intervalTicks,
        std::uint32_t cycle,
        float variationFraction = 0.15f
    ) {
        if (intervalTicks == 0) {
            return 0;
        }
        const std::uint32_t variationTicks = static_cast<std::uint32_t>(
            static_cast<float>(intervalTicks) * variationFraction);
        const std::uint32_t minimum = intervalTicks > variationTicks
            ? intervalTicks - variationTicks
            : 1;
        const std::uint32_t maximum = intervalTicks + variationTicks;
        const std::uint32_t spread = maximum - minimum + 1;
        const std::uint32_t cycleHash =
            mix(key ^ ((cycle + 1U) * 0x9e3779b9U));
        return minimum + cycleHash % spread;
    }
}
