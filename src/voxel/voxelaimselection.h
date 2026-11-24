#pragma once

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>

#include <glm/glm.hpp>

class VoxelAimSelection {
public:
    explicit VoxelAimSelection(const glm::vec3& center) : m_center(center) {}

    void consider(const glm::ivec3& cell) {
        const glm::vec3 offset = glm::vec3(cell) - m_center;
        const float distance = glm::dot(offset, offset);
        const float tolerance = m_cell
            ? 0.0001f * std::max(1.0f, std::max(distance, m_distance))
            : 0.0f;
        if (!m_cell || distance < m_distance - tolerance ||
            (std::abs(distance - m_distance) <= tolerance &&
                (cell.x < m_cell->x || (cell.x == m_cell->x &&
                (cell.y < m_cell->y || (cell.y == m_cell->y && cell.z < m_cell->z)))))) {
            m_cell = cell;
            m_distance = distance;
        }
    }

    const std::optional<glm::ivec3>& cell() const { return m_cell; }

private:
    glm::vec3 m_center;
    float m_distance = std::numeric_limits<float>::infinity();
    std::optional<glm::ivec3> m_cell;
};
