#include "shield.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>

#include "geometry/sphere.h"
#include "shieldslot.h"
#include "voxel/voxelclusterbounds.h"
#include "worldobject/worldobject.h"
#include "voxel/voxel.h"

namespace {
    bool segmentIntersectsSphere(
        const glm::vec3& start,
        const glm::vec3& end,
        const Sphere& sphere,
        float& hitParameter
    ) {
        const glm::vec3 segment = end - start;
        const glm::vec3 offset = start - sphere.position();
        const float a = glm::dot(segment, segment);
        const float c = glm::dot(offset, offset) -
            sphere.radius() * sphere.radius();
        if (c <= 0.0f) {
            hitParameter = 0.0f;
            return true;
        }
        if (a <= 0.0f) {
            return false;
        }
        const float b = 2.0f * glm::dot(offset, segment);
        const float discriminant = b * b - 4.0f * a * c;
        if (discriminant < 0.0f) {
            return false;
        }
        const float parameter = (-b - std::sqrt(discriminant)) / (2.0f * a);
        if (parameter < 0.0f || parameter > 1.0f) {
            return false;
        }
        hitParameter = parameter;
        return true;
    }

    bool segmentIntersectsVoxel(
        const glm::vec3& start,
        const glm::vec3& end,
        const glm::ivec3& cell,
        float halfExtent,
        float& hitParameter
    ) {
        const glm::vec3 direction = end - start;
        float nearParameter = 0.0f;
        float farParameter = 1.0f;
        for (int axis = 0; axis < 3; ++axis) {
            const float minimum = static_cast<float>(cell[axis]) - halfExtent;
            const float maximum = static_cast<float>(cell[axis]) + halfExtent;
            if (std::abs(direction[axis]) < 1.0e-6f) {
                if (start[axis] < minimum || start[axis] > maximum) {
                    return false;
                }
                continue;
            }
            float first = (minimum - start[axis]) / direction[axis];
            float second = (maximum - start[axis]) / direction[axis];
            if (first > second) {
                std::swap(first, second);
            }
            nearParameter = std::max(nearParameter, first);
            farParameter = std::min(farParameter, second);
            if (nearParameter > farParameter) {
                return false;
            }
        }
        hitParameter = nearParameter;
        return farParameter >= 0.0f && nearParameter <= 1.0f;
    }
}


Shield::Shield(const std::string& key):
    Equipment(key),
    m_hp(0.0f),
    m_regeneration(0.0f),
    m_maxHP(0.0f),
    m_shieldSlot(nullptr),
    m_hitCount(0),
    m_maxHits(5)
{

}

Shield::~Shield() = default;

ShieldSlot* Shield::shieldSlot() {
    return m_shieldSlot;
}

void Shield::setShieldSlot(ShieldSlot* shieldSlot) {
    m_shieldSlot = shieldSlot;
}

float Shield::hp() const {
    return m_hp;
}

void Shield::setHP(float hp) {
    assert(hp <= maxHP() && hp >= 0);
    m_hp = hp;
}

float Shield::maxHP() const {
    return m_maxHP;
}

void Shield::setMaxHP(float maxHP) {
    m_maxHP = maxHP;
}

int Shield::maxHits() const {
    return m_maxHits;
}

void Shield::setMaxHits(int maxHits) {
    if (maxHits <= 0) {
        throw std::invalid_argument("Shield maximum hit count must be positive.");
    }
    m_maxHits = maxHits;
}

float Shield::regeneration() const {
    return m_regeneration;
}

void Shield::setRegeneration(float regeneration) {
    m_regeneration = regeneration;
}

bool Shield::depleted() const {
    return m_hitCount >= static_cast<unsigned int>(m_maxHits) ||
        m_hp <= 0.0f;
}

bool Shield::registerHit() {
    if (depleted()) {
        return false;
    }
    ++m_hitCount;
    m_hp = std::max(
        0.0f, m_hp - m_maxHP / static_cast<float>(m_maxHits));
    return true;
}

bool Shield::intersectsSegment(
    WorldObject& protectedObject,
    ShieldType shieldType,
    const glm::vec3& start,
    const glm::vec3& end,
    float projectileRadius,
    glm::ivec3& impactCell,
    float& hitParameter
) const {
    if (depleted() || shieldType == ShieldType::None) {
        return false;
    }
    if (shieldType == ShieldType::Sphere) {
        const Sphere shieldSphere =
            protectedObject.bounds().sphere(protectedObject.transform());
        Sphere expandedSphere(
            shieldSphere.position(),
            shieldSphere.radius() + projectileRadius);
        if (!segmentIntersectsSphere(
            start,
            end,
            expandedSphere,
            hitParameter)) {
            return false;
        }

        const glm::vec3 localImpact = protectedObject.transform().inverseApplyTo(
            glm::mix(start, end, hitParameter));
        float nearestDistanceSquared = std::numeric_limits<float>::max();
        for (const auto& entry : protectedObject.voxelMap()) {
            const glm::vec3 difference =
                glm::vec3(entry.first) - localImpact;
            const float distanceSquared = glm::dot(difference, difference);
            if (distanceSquared < nearestDistanceSquared) {
                nearestDistanceSquared = distanceSquared;
                impactCell = entry.first;
            }
        }
        return nearestDistanceSquared < std::numeric_limits<float>::max();
    }

    const Transform& transform = protectedObject.transform();
    const glm::vec3 localStart = transform.inverseApplyTo(start);
    const glm::vec3 localEnd = transform.inverseApplyTo(end);
    const float halfExtent = 0.75f +
        projectileRadius / transform.scale();
    const glm::ivec3 minimumCell = glm::ivec3(glm::ceil(
        glm::min(localStart, localEnd) - glm::vec3(halfExtent)));
    const glm::ivec3 maximumCell = glm::ivec3(glm::floor(
        glm::max(localStart, localEnd) + glm::vec3(halfExtent)));
    const glm::ivec3 surfaceNeighbors[] = {
        glm::ivec3(-1, 0, 0),
        glm::ivec3(1, 0, 0),
        glm::ivec3(0, -1, 0),
        glm::ivec3(0, 1, 0),
        glm::ivec3(0, 0, -1),
        glm::ivec3(0, 0, 1)
    };
    bool intersects = false;
    const auto testCell = [&](const glm::ivec3& cell) {
        if (protectedObject.voxel(cell) == nullptr) {
            return;
        }
        bool isSurfaceVoxel = false;
        for (const glm::ivec3& neighbor : surfaceNeighbors) {
            if (protectedObject.voxel(cell + neighbor) == nullptr) {
                isSurfaceVoxel = true;
                break;
            }
        }
        if (!isSurfaceVoxel) {
            return;
        }

        float parameter = 0.0f;
        if (segmentIntersectsVoxel(
                localStart, localEnd, cell, halfExtent, parameter) &&
            (!intersects || parameter < hitParameter)) {
            intersects = true;
            hitParameter = parameter;
            impactCell = cell;
        }
    };

    const std::int64_t rangeX =
        static_cast<std::int64_t>(maximumCell.x) - minimumCell.x + 1;
    const std::int64_t rangeY =
        static_cast<std::int64_t>(maximumCell.y) - minimumCell.y + 1;
    const std::int64_t rangeZ =
        static_cast<std::int64_t>(maximumCell.z) - minimumCell.z + 1;
    const double candidateCellCount =
        static_cast<double>(rangeX) *
        static_cast<double>(rangeY) *
        static_cast<double>(rangeZ);
    if (rangeX > 0 && rangeY > 0 && rangeZ > 0 &&
        candidateCellCount < protectedObject.voxelMap().size()) {
        for (std::int64_t x = minimumCell.x; x <= maximumCell.x; ++x) {
            for (std::int64_t y = minimumCell.y; y <= maximumCell.y; ++y) {
                for (std::int64_t z = minimumCell.z; z <= maximumCell.z; ++z) {
                    testCell(glm::ivec3(
                        static_cast<int>(x),
                        static_cast<int>(y),
                        static_cast<int>(z)));
                }
            }
        }
    } else {
        for (const auto& entry : protectedObject.voxelMap()) {
            testCell(entry.first);
        }
    }
    return intersects;
}

float Shield::compensate(float damage) {
    if (depleted()) {
        return damage;
    }
    float compensatedDamage = std::min(m_hp, damage);
    float forwardedDamage = damage - compensatedDamage;

    m_hp -= compensatedDamage;
    onDamageTaken(compensatedDamage);

    return forwardedDamage;
}

void Shield::update(float deltaSec) {
    if (m_hitCount >= static_cast<unsigned int>(m_maxHits)) {
        m_hp = 0.0f;
        return;
    }
    m_hp += regeneration() * deltaSec;
    m_hp = std::min(m_hp, maxHP());
}

void Shield::onDamageTaken(float damage) {

}
