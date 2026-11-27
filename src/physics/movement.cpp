#include "movement.h"

#include <algorithm>
#include <cassert>

#include <glm/glm.hpp>

#include <glow/logging.h>

#include "collision/collisiondetector.h"
#include "collision/shieldcontact.h"
#include "world/handler/mover.h"

#include "worldobject/worldobject.h"

#include "worldtree/worldtree.h"
#include "worldtree/worldtreequery.h"
#include "worldtree/worldtreegeode.h"

#include "voxel/voxelclusterbounds.h"


static const float ATOMIC_DIRECTIONAL_STEP = 0.4f;
static const float ATOMIC_ANGULAR_STEP = glm::radians(30.0f);
static const float MAX_STEPPED_DISTANCE = 1.2f;

Movement::Movement(WorldObject& worldObject, const Transform& originalTransform, const Transform& targetTransform,
    Mover* mover):
    m_worldObject(worldObject),
    m_collisionDetector(m_worldObject.collisionDetector()),
    m_originalTransform(originalTransform),
    m_targetTransform(targetTransform),
    m_distance(0.0f),
    m_successful(true),
    m_mover(mover)
{
}

Movement::~Movement() = default;

bool Movement::perform() {
    assert(m_worldObject.collisionDetector().geode() != nullptr);

    m_successful = true;

    IAABB phaseAABB = m_worldObject.bounds().aabb(m_originalTransform).united(m_worldObject.bounds().aabb(m_targetTransform));
    WorldTreeHint nodeHint = m_worldObject.collisionDetector().geode()->hint();

    if (WorldTreeQuery(m_collisionDetector.worldTree(), &phaseAABB, nodeHint, &m_worldObject.collisionFilter()).areGeodesNear()) {
        glm::vec3 directionalStep = m_targetTransform.position() - m_originalTransform.position();
        m_distance = glm::length(directionalStep);

        if (m_distance > MAX_STEPPED_DISTANCE) {
            performSplitted();
        } else {
            performStepped(phaseAABB);
        }
    } else {
        m_intersectionFreeTransform = m_targetTransform;
    }

    m_worldObject.updateTransformAndGeode(m_intersectionFreeTransform.position(), m_intersectionFreeTransform.orientation());

    return m_successful;
}

void Movement::performSplitted() {
    Transform pivotTransform = m_originalTransform.mixed(m_targetTransform, 0.5f);

    Movement left(m_worldObject, m_originalTransform, pivotTransform,m_mover);
    Movement right(m_worldObject, pivotTransform, m_targetTransform,m_mover);

    m_successful = left.perform() && right.perform();
    m_intersectionFreeTransform = m_worldObject.transform();
}

void Movement::performStepped(const IAABB& phaseAABB) {
    WorldTreeHint hint = m_worldObject.collisionDetector().geode()->hint();
    std::unordered_set<WorldTreeGeode*> possibleColliders = WorldTreeQuery(m_collisionDetector.worldTree(), &phaseAABB, hint, &m_worldObject.collisionFilter()).nearGeodes();
    const bool projectile = m_worldObject.objectType() == WorldObjectType::Bullet ||
        m_worldObject.objectType() == WorldObjectType::Rocket;
    const bool shieldAware = m_mover && !projectile &&
        (hasSphericalShield(m_worldObject) ||
         std::any_of(possibleColliders.begin(),possibleColliders.end(),[](auto* geode) {
             return hasSphericalShield(*geode->worldObject());
         }));
    int stepCount = calculateStepCount(shieldAware);

    m_intersectionFreeTransform = m_worldObject.transform();

    for (int s = 0; s < stepCount; s++) {
        m_intersectionFreeTransform = m_worldObject.transform();
        Transform newTransform(calculateStep(s, stepCount));

        if (shieldAware) {
            const auto contact = m_mover->findShieldContact(
                m_worldObject,m_intersectionFreeTransform,newTransform,possibleColliders);
            if (contact) {
                m_intersectionFreeTransform = m_intersectionFreeTransform.mixed(
                    newTransform,std::max(0.0f,contact->second.time-1e-5f));
                m_worldObject.setTransform(m_intersectionFreeTransform);
                m_mover->resolveShieldContact(m_worldObject,*contact->first,contact->second);
                m_successful = false;
                break;
            }
        }
        m_worldObject.setTransform(newTransform);

        std::list<VoxelCollision>* collisionsPointer;
        if (shieldAware) {
            auto hullColliders = possibleColliders;
            for (auto it = hullColliders.begin(); it != hullColliders.end();) {
                auto* other = (*it)->worldObject();
                if (other->objectType() != WorldObjectType::Bullet &&
                    other->objectType() != WorldObjectType::Rocket &&
                    (hasSphericalShield(m_worldObject) || hasSphericalShield(*other)))
                    it = hullColliders.erase(it);
                else ++it;
            }
            collisionsPointer = &m_collisionDetector.checkCollisions(hullColliders);
        } else {
            collisionsPointer = &m_collisionDetector.checkCollisions(possibleColliders);
        }
        const auto& collisions = *collisionsPointer;

        if (!collisions.empty()) {
            m_successful = false;
            break;
        } else {
            m_intersectionFreeTransform = newTransform;
        }
    }
}

int Movement::calculateStepCount(bool shieldAware) {
    int steps = std::max(static_cast<int>(glm::ceil(m_distance / ATOMIC_DIRECTIONAL_STEP)) - 1, 1); // at least one

    float angularDiff = glm::angle(glm::inverse(m_targetTransform.orientation()) * m_originalTransform.orientation());

    if (std::isfinite(angularDiff)) { // sometimes glm::angle returns INF for really small angles
        const float angularStep = shieldAware ? glm::radians(5.0f) : ATOMIC_ANGULAR_STEP;
        const int angularSteps = static_cast<int>(glm::ceil(angularDiff / angularStep));
        steps = std::max(steps, shieldAware ? angularSteps : angularSteps-1);
    }

    assert(steps > 0);
    return steps;
}

Transform Movement::calculateStep(int s, int stepCount) const {
    Transform transform;

    float rel = static_cast<float>(s + 1) / static_cast<float>(stepCount);

    transform.setOrientation(glm::slerp(m_originalTransform.orientation(), m_targetTransform.orientation(), rel));
    transform.setPosition(glm::mix(m_originalTransform.position(), m_targetTransform.position(), rel));
    transform.setScale(m_originalTransform.scale());
    transform.setCenter(m_originalTransform.center());

    return transform;
}
