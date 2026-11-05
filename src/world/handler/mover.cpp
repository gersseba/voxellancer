#include "mover.h"

#include "benchmark/benchmarkprofiler.h"
#include "world/god.h"
#include "world/world.h"
#include "worldobject/worldobject.h"
#include "physics/physics.h"


Mover::Mover(World& world):
    m_world(world)
{
}

void Mover::moveWorldObjects(float deltaSec) {
    m_voxelCollisions.clear();

    for (WorldObject *worldObject : m_world.worldObjects()) {
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
}

std::list<VoxelCollision> &Mover::voxelCollisions() {
    return m_voxelCollisions;
}
