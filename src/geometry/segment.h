#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

inline bool segmentIntersectsVoxel(const glm::vec3& start, const glm::vec3& end,
    const glm::ivec3& cell, float halfExtent, float& hitParameter) {
    const glm::vec3 direction = end - start;
    float nearParameter = 0, farParameter = 1;
    for (int axis = 0; axis < 3; ++axis) {
        const float minimum = cell[axis] - halfExtent;
        const float maximum = cell[axis] + halfExtent;
        if (std::abs(direction[axis]) < 1e-6f) {
            if (start[axis] < minimum || start[axis] > maximum) return false;
        } else {
            float first = (minimum - start[axis]) / direction[axis];
            float second = (maximum - start[axis]) / direction[axis];
            if (first > second) std::swap(first, second);
            nearParameter = std::max(nearParameter, first);
            farParameter = std::min(farParameter, second);
            if (nearParameter > farParameter) return false;
        }
    }
    hitParameter = nearParameter;
    return farParameter >= 0 && nearParameter <= 1;
}
