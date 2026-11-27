#include "god.h"
#include "collision/shieldcontact.h"
#include "worldtree/worldtreequery.h"
#include "worldtree/worldtreegeode.h"



#include <glow/logging.h>
#include <unordered_set>

#include "benchmark/benchmarkprofiler.h"
#include "collision/collisiondetector.h"

#include "scripting/scriptengine.h"

#include "worldobject/worldobjectinfo.h"

#include "worldtree/worldtree.h"

#include "worldobject/worldobject.h"

#include "world.h"


God::God(World& world):
    m_world(world)
{
}

God::~God() {

}

void God::scheduleSpawn(SpawnRequest spawnRequest) {
    m_spawnRequests.push_back(spawnRequest);
}

void God::scheduleRemoval(WorldObject* worldObject) {
    assert(worldObject->collisionDetector().geode() != nullptr);
    assert(worldObject->collisionDetector().worldTree() == &m_world.worldTree());
    for (WorldObject* scheduled : m_scheduledRemovals){
        if (scheduled == worldObject) {
            return;
        }
    }
    m_scheduledRemovals.push_back(worldObject);
    worldObject->setSpawnState(SpawnState::RemovalScheduled);
}

void God::scheduleRemovals(const std::list<WorldObject*>& removals) {
    for (WorldObject* worldObject : removals) {
        scheduleRemoval(worldObject);
    }
}

void God::spawn() {
    for (SpawnRequest& spawnRequest : m_spawnRequests) {
        for (WorldObject* worldObject : spawnRequest.worldObjects()) {
            m_world.worldTree().insert(worldObject);

            std::list<VoxelCollision> collisions;
            if (worldObject->objectType() == WorldObjectType::Bullet) {
                BenchmarkCpuScope scope(BenchmarkMetric::BulletSpawnCollision);
                collisions = worldObject->collisionDetector().checkCollisions();
            } else {
                collisions = worldObject->collisionDetector().checkCollisions();
            }

            WorldObject* collider = nullptr;
            if (!collisions.empty()) {
                collider = collisions.front().a().worldObject();
                if (collider == worldObject) collider = collisions.front().b().worldObject();
            } else if (worldObject->objectType() != WorldObjectType::Bullet &&
                       worldObject->objectType() != WorldObjectType::Rocket) {
                const auto candidates = WorldTreeQuery(
                    &m_world.worldTree(),&worldObject->collisionDetector().geode()->aabb(),
                    worldObject->collisionDetector().geode()->hint(),
                    &worldObject->collisionFilter()).nearGeodes();
                for (auto* candidate : candidates) {
                    auto* other = candidate->worldObject();
                    if (other->objectType() == WorldObjectType::Bullet ||
                        other->objectType() == WorldObjectType::Rocket) continue;
                    if (sweepShieldContact(*worldObject,worldObject->transform(),
                                           worldObject->transform(),*other,true)) {
                        collider = other;
                        break;
                    }
                }
            }
            if (collider) {
                glow::warning("Failed to spawn %; due to collision with %;",
                    worldObject->info().name(), collider->info().name());

                m_world.worldTree().remove(worldObject->collisionDetector().geode());

                worldObject->setSpawnState(SpawnState::Rejected);
                worldObject->onSpawnFail();

                if (spawnRequest.deleteOnRejection()) {
                    delete worldObject;
                }
            } else {
                worldObject->setSpawnState(SpawnState::Spawned);
                m_world.addWorldObject(worldObject);
            }
        }
    }
    m_spawnRequests.clear();
}

void God::remove() {
    for (WorldObject* worldObject : m_scheduledRemovals) {
        m_world.worldTree().remove(worldObject->collisionDetector().geode());
        m_world.removeWorldObject(worldObject);
        delete worldObject;
    }
    m_scheduledRemovals.clear();
}

void God::clearPending() {
    std::unordered_set<WorldObject*> discarded;
    for (SpawnRequest& request : m_spawnRequests) {
        if (!request.deleteOnRejection()) {
            continue;
        }
        for (WorldObject* worldObject : request.worldObjects()) {
            if (worldObject != nullptr &&
                m_world.worldObjects().find(worldObject) ==
                    m_world.worldObjects().end() &&
                discarded.insert(worldObject).second) {
                delete worldObject;
            }
        }
    }
    m_spawnRequests.clear();
    m_scheduledRemovals.clear();
}
