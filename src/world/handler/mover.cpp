#include "mover.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include "benchmark/benchmarkprofiler.h"
#include "collision/collisiondetector.h"
#include "collision/collisionfilter.h"
#include "equipment/shield.h"
#include "equipment/shieldslot.h"
#include "equipment/weapons/projectile.h"
#include "geometry/sphere.h"
#include "geometry/aabb.h"
#include "voxel/voxel.h"
#include "voxel/voxelclusterbounds.h"
#include "world/god.h"
#include "world/world.h"
#include "worldobject/worldobject.h"
#include "worldobject/worldobjectcomponents.h"
#include "worldobject/ship.h"
#include "physics/physics.h"
#include "worldtree/worldtreegeode.h"
#include "worldtree/worldtreequery.h"


Mover::Mover(World& world):
    m_world(world)
{
}

void Mover::moveWorldObjects(float deltaSec) {
    m_voxelCollisions.clear();
    std::vector<std::pair<WorldObject*, glm::vec3>> projectileStartPositions;

    for (WorldObject *worldObject : m_world.worldObjects()) {
        if (worldObject->objectType() == WorldObjectType::Bullet ||
            worldObject->objectType() == WorldObjectType::Rocket) {
            projectileStartPositions.emplace_back(
                worldObject, worldObject->transform().position());
        }
        std::list<VoxelCollision>* collisionsPointer = nullptr;
        if (worldObject->objectType() == WorldObjectType::Bullet ||
            worldObject->objectType() == WorldObjectType::Rocket) {
            BenchmarkCpuScope scope(BenchmarkMetric::ProjectileMovement);
            collisionsPointer = &worldObject->physics().move(deltaSec);
        } else {
            collisionsPointer = &worldObject->physics().move(deltaSec);
        }
        std::list<VoxelCollision>& collisions = *collisionsPointer;
        m_voxelCollisions.splice(m_voxelCollisions.end(), collisions);
    }

    interceptShieldHits(projectileStartPositions);
}

std::list<VoxelCollision> &Mover::voxelCollisions() {
    return m_voxelCollisions;
}

void Mover::setShieldHitEventReplicationEnabled(bool enabled) {
    m_shieldHitEventReplicationEnabled = enabled;
    if (!enabled) {
        m_shieldHitEvents.clear();
    }
}

std::vector<voxellancer::protocol::ShieldHitEvent>
Mover::drainShieldHitEvents() {
    std::vector<voxellancer::protocol::ShieldHitEvent> events;
    events.swap(m_shieldHitEvents);
    return events;
}

void Mover::interceptShieldHits(
    const std::vector<std::pair<WorldObject*, glm::vec3>>&
        projectileStartPositions
) {
    if (projectileStartPositions.empty()) {
        return;
    }

    float maximumShieldRadius = 0.0f;
    for (WorldObject* object : m_world.worldObjects()) {
        Ship* ship = dynamic_cast<Ship*>(object);
        if (ship == nullptr || ship->shieldType() == ShieldType::None) {
            continue;
        }
        for (const std::shared_ptr<ShieldSlot>& slot :
            object->components().shieldSlots()) {
            if (slot && slot->shield() && !slot->shield()->depleted()) {
                const Sphere boundSphere =
                    object->bounds().sphere(object->transform());
                maximumShieldRadius = std::max(maximumShieldRadius,
                    glm::length(
                        boundSphere.position() - object->transform().position()) +
                        boundSphere.radius());
                break;
            }
        }
    }

    std::unordered_set<WorldObject*> interceptedProjectiles;
    for (const auto& startEntry : projectileStartPositions) {
        WorldObject* projectileObject = startEntry.first;
        WorldTreeGeode* projectileGeode =
            projectileObject->collisionDetector().geode();
        if (projectileGeode == nullptr || maximumShieldRadius <= 0.0f) {
            continue;
        }

        const glm::vec3 start = startEntry.second;
        const glm::vec3 end = projectileObject->transform().position();
        const float projectileRadius =
            projectileObject->bounds().sphere(projectileObject->transform()).radius();
        const float broadphaseRadius = maximumShieldRadius + projectileRadius;
        const glm::vec3 lower = glm::min(start, end) -
            glm::vec3(broadphaseRadius);
        const glm::vec3 upper = glm::max(start, end) +
            glm::vec3(broadphaseRadius);
        const IAABB phaseAABB(
            glm::ivec3(glm::floor(lower)),
            glm::ivec3(glm::ceil(upper)));
        const auto candidates = WorldTreeQuery(
            projectileObject->collisionDetector().worldTree(),
            &phaseAABB,
            projectileGeode->hint(),
            &projectileObject->collisionFilter()).nearGeodes();

        Shield* hitShield = nullptr;
        WorldObject* protectedObject = nullptr;
        glm::ivec3 impactCell(0);
        float earliestHit = std::numeric_limits<float>::max();
        for (WorldTreeGeode* candidate : candidates) {
            WorldObject* object = candidate->worldObject();
            Ship* ship = dynamic_cast<Ship*>(object);
            if (ship == nullptr ||
                ship->shieldType() == ShieldType::None ||
                !projectileObject->collisionFilter().isCollideableWith(
                    &object->collisionFilter())) {
                continue;
            }
            for (const std::shared_ptr<ShieldSlot>& slot :
                object->components().shieldSlots()) {
                if (!slot || !slot->shield()) {
                    continue;
                }
                glm::ivec3 candidateCell(0);
                float hitParameter = 0.0f;
                Shield& shield = *slot->shield();
                if (shield.intersectsSegment(
                        *ship,
                        ship->shieldType(),
                        start,
                        end,
                        projectileRadius,
                        candidateCell,
                        hitParameter) &&
                    hitParameter < earliestHit) {
                    hitShield = &shield;
                    protectedObject = object;
                    impactCell = candidateCell;
                    earliestHit = hitParameter;
                }
            }
        }
        if (hitShield == nullptr || !hitShield->registerHit()) {
            continue;
        }

        interceptedProjectiles.insert(projectileObject);
        const glm::vec3 impactPosition = glm::mix(start, end, earliestHit);
        const glm::vec3 localImpactPosition =
            protectedObject->transform().inverseApplyTo(impactPosition);
        Projectile* projectile =
            static_cast<Projectile*>(projectileObject);
        const auto impactColor = projectile->impactColor();
        projectileObject->updateTransformAndGeode(
            impactPosition,
            projectileObject->orientation());
        projectile->onShieldImpact();
        if (m_shieldHitEventReplicationEnabled) {
            const auto entity = m_world.entityId(protectedObject);
            if (!entity) {
                throw std::logic_error(
                    "Shield impact owner has no server entity ID.");
            }
            if (m_shieldHitEvents.size() >= 4096) {
                throw std::overflow_error(
                    "Shield-hit event queue exceeded its valid limit.");
            }
            m_shieldHitEvents.push_back({
                *entity,
                m_world.simulationTickIndex(),
                impactCell,
                localImpactPosition,
                impactColor,
                1.0f
            });
        }
    }

    if (!interceptedProjectiles.empty()) {
        m_voxelCollisions.remove_if(
            [&interceptedProjectiles](VoxelCollision& collision) {
                return interceptedProjectiles.count(
                        collision.a().worldObject()) != 0 ||
                    interceptedProjectiles.count(
                        collision.b().worldObject()) != 0;
            });
    }
}
