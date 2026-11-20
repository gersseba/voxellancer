#pragma once

#include <glm/glm.hpp>
#include <optional>


class Hardpoint;
class Vpxel;
class WorldObject;

/**
 * Identifies the direction a hardpoint must shoot to hit a moving WorldObject
 * given the direction and the speed of this other WorldObject doesn't change too
 * much
 */
class HardpointAimHelper {
public:
    HardpointAimHelper(Hardpoint* hardpoint, WorldObject* targetObject,
        bool accountForSpawnOffset = false);
    static std::optional<glm::vec3> interceptPoint(const glm::vec3& muzzle,
        const glm::vec3& targetPosition, const glm::vec3& relativeVelocity,
        float speed, float lifetime, float range, float spawnOffset = 0.0f);

    void aim();

    /**
     * For some reason it might be impossible to hit the target
     * (e.g. ship faster than bullet, target out of range) then this will return false.
     * direction() and point() are invalid then.
     */
    bool isHittable();

    const glm::vec3& direction();
    const glm::vec3& point();


protected:
    float bulletTravelTime(const glm::vec3& point);
    glm::vec3 targetPositionIn(float deltaSec);
    float bulletSpeedInDirection(const glm::vec3& direction);


protected:
    Hardpoint* m_hardpoint;
    WorldObject* m_targetObject;

    bool m_aimed;

    glm::vec3 m_hardpointPosition;

    glm::vec3 m_targetPosition;
    glm::vec3 m_targetSpeed;

    float m_bulletSpeed;
    float m_bulletLifetime;
    float m_bulletRange;
    float m_spawnOffset;

    bool m_hitable;

    glm::vec3 m_direction;
    glm::vec3 m_point;
};

