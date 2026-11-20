#pragma once

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

struct ContactMarker {
    glm::vec2 position;
    bool onScreen;
    bool behind;
};

inline ContactMarker projectContactMarker(const glm::vec4& clip) {
    glm::vec2 offset(clip.x, -clip.y);
    offset *= 0.5f / std::max(std::abs(clip.w), 0.0001f);
    const bool behind = clip.w <= 0.0f;
    const bool onScreen = !behind &&
        std::abs(offset.x) <= 0.42f && std::abs(offset.y) <= 0.33f;
    if (!onScreen) {
        if (glm::dot(offset, offset) < 1e-8f) {
            offset = glm::vec2(0.0f, 0.33f);
        } else {
            offset *= std::min(
                0.42f / std::max(std::abs(offset.x), 0.0001f),
                0.33f / std::max(std::abs(offset.y), 0.0001f));
        }
    }
    return {glm::vec2(0.5f) + offset, onScreen, behind};
}
