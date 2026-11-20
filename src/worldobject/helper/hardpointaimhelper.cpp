#include "hardpointaimhelper.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "equipment/weapon.h"
#include "equipment/hardpoint.h"
#include "equipment/weapons/gun.h"

#include "physics/physics.h"

#include "utils/safenormalize.h"

#include "voxel/specialvoxels/hardpointvoxel.h"

#include "worldobject/worldobject.h"
#include "worldobject/worldobjectcomponents.h"


HardpointAimHelper::HardpointAimHelper(Hardpoint* hardpoint, WorldObject* targetObject,
    bool accountForSpawnOffset):
    m_hardpoint(hardpoint),
    m_targetObject(targetObject),
    m_bulletSpeed(0.0f),
    m_hitable(false),
    m_aimed(false),
    m_bulletLifetime(0.0f)
{
    assert(m_hardpoint->weapon());

    Gun& gun = dynamic_cast<Gun&>(*m_hardpoint->weapon().get());

    m_hardpointPosition = m_hardpoint->voxel()->position();

    m_targetPosition = m_targetObject->transform().position();
    m_targetSpeed = m_targetObject->physics().speed().directional();
    if (gun.profile().enabled)
        m_targetSpeed -= m_hardpoint->components()->worldObject()->physics().speed().directional();
    m_bulletSpeed = gun.bulletSpeed();
    m_bulletLifetime = gun.bulletLifetime();
    m_bulletRange = gun.profile().enabled ? gun.profile().range :
        m_bulletSpeed*m_bulletLifetime;
    m_spawnOffset = accountForSpawnOffset ? gun.bulletSpawnOffset() : 0.0f;
}

void HardpointAimHelper::aim() {
    m_aimed = true;
    const auto point = interceptPoint(m_hardpointPosition, m_targetPosition, m_targetSpeed,
        m_bulletSpeed, m_bulletLifetime, m_bulletRange, m_spawnOffset);
    m_hitable = point.has_value();
    if (!point) return;
    m_point = *point;
    m_direction = safeNormalize(m_point - m_hardpointPosition, glm::vec3(0.0f, 0.0f, -1.0f));
}

std::optional<glm::vec3> HardpointAimHelper::interceptPoint(const glm::vec3& muzzle,
    const glm::vec3& targetPosition, const glm::vec3& relativeVelocity,
    float speed, float lifetime, float range, float spawnOffset) {
    if (speed <= 0) return std::nullopt;
    const glm::dvec3 offset = glm::dvec3(targetPosition)-glm::dvec3(muzzle);
    const glm::dvec3 velocity(relativeVelocity);
    const double a = glm::dot(velocity,velocity)-double(speed)*speed;
    // At impact the target's relative distance is spawnOffset + speed * time.
    const double b = 2*(glm::dot(offset,velocity)-double(spawnOffset)*speed);
    const double c = glm::dot(offset,offset)-double(spawnOffset)*spawnOffset;
    double time = std::numeric_limits<double>::infinity();
    if (c < 0.000001) return std::nullopt;
    if (std::abs(a) < 0.000001) {
        if (std::abs(b) >= 0.000001 && -c/b > 0) time = -c/b;
    } else {
        const double discriminant = b*b-4*a*c;
        if (discriminant >= 0) {
            const double root = std::sqrt(discriminant);
            for (double candidate : {(-b-root)/(2*a),(-b+root)/(2*a)})
                if (candidate > 0) time = std::min(time,candidate);
        }
    }
    if (!std::isfinite(time) || time > lifetime || time*speed > range) return std::nullopt;
    return targetPosition + relativeVelocity * static_cast<float>(time);
}

bool HardpointAimHelper::isHittable() {
    if(!m_aimed) {
        aim();
    }
    return m_hitable;
}

const glm::vec3& HardpointAimHelper::direction() {
    if(!m_aimed) {
        aim();
    }
    return m_direction;
}

const glm::vec3& HardpointAimHelper::point() {
    if(!m_aimed) {
        aim();
    }
    return m_point;
}

float HardpointAimHelper::bulletTravelTime(const glm::vec3& point) {
    float distance = glm::length(point - m_hardpointPosition);
    return distance / m_bulletSpeed;
}

glm::vec3 HardpointAimHelper::targetPositionIn(float deltaSec) {
    return m_targetPosition + m_targetSpeed * deltaSec;
}

