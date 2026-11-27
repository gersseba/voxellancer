#pragma once

#include <list>

#include <glm/glm.hpp>

#include "geometry/aabb.h"
#include "geometry/transform.h"


class WorldObject;
class CollisionDetector;
class Mover;

class Movement {
public:
    Movement(WorldObject& worldObject, const Transform& originalTransform, const Transform& targetTransform,
        Mover* mover = nullptr);
    virtual ~Movement();

    bool perform();


protected:
    WorldObject& m_worldObject;
    CollisionDetector& m_collisionDetector;

    Transform m_originalTransform;
    Transform m_intersectionFreeTransform;
    Transform m_targetTransform;

    float m_distance;
    bool m_successful;
    Mover* m_mover;


    void performSplitted();
    void performStepped(const IAABB& phaseAABB);
    int calculateStepCount(bool shieldAware);
    Transform calculateStep(int s, int stepCount) const;
};
