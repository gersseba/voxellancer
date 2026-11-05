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

    inline float unitInterval(std::uint32_t hash) {
        return static_cast<float>(static_cast<double>(hash) / 4294967296.0);
    }

    inline float initialPhase(std::uint32_t key, float interval) {
        return unitInterval(mix(key)) * interval;
    }

    inline float period(
        std::uint32_t key,
        float interval,
        std::uint32_t cycle,
        float variationFraction = 0.15f) {
        const std::uint32_t cycleHash = mix(key ^ ((cycle + 1U) * 0x9e3779b9U));
        const float variation = (unitInterval(cycleHash) * 2.0f - 1.0f) *
            variationFraction;
        return interval * (1.0f + variation);
    }
}
