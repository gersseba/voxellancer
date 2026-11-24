#pragma once

#include <algorithm>
#include <cmath>
#include <optional>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

struct CrosshairAim {
    glm::vec3 point;
    bool hitVoxel;
};

// The ray direction is normalized in world space; the returned distances are world units.
inline std::optional<glm::vec2> voxelRayInterval(const glm::vec3& origin,
    const glm::vec3& direction, const glm::vec3& position,
    const glm::quat& orientation, float scale, const glm::vec3& center,
    const glm::ivec3& cell, float maxDistance)
{
    const auto inverse = glm::inverse(orientation);
    const glm::vec3 localOrigin = inverse * (origin - position) / scale + center;
    const glm::vec3 localDirection = inverse * direction / scale;
    float enter = 0.0f;
    float exit = maxDistance;
    for (int axis = 0; axis < 3; ++axis) {
        const float lower = static_cast<float>(cell[axis]) - 0.5f;
        const float upper = static_cast<float>(cell[axis]) + 0.5f;
        if (std::abs(localDirection[axis]) < 1e-8f) {
            if (localOrigin[axis] < lower || localOrigin[axis] > upper) return std::nullopt;
            continue;
        }
        float first = (lower - localOrigin[axis]) / localDirection[axis];
        float last = (upper - localOrigin[axis]) / localDirection[axis];
        if (first > last) std::swap(first, last);
        enter = std::max(enter, first);
        exit = std::min(exit, last);
        if (enter > exit) return std::nullopt;
    }
    return glm::vec2(enter, exit);
}
