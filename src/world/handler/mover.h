#pragma once

#include <list>
#include <utility>
#include <vector>

#include "collision/voxelcollision.h"
#include "physics/impulse.h"
#include "protocol/serverprotocol.h"

class World;
class WorldObject;

class Mover
{
public:
    explicit Mover(World& world);

    void moveWorldObjects(float deltaSec);

    std::list<VoxelCollision>& voxelCollisions();
    void setShieldHitEventReplicationEnabled(bool enabled);
    std::vector<voxellancer::protocol::ShieldHitEvent>
    drainShieldHitEvents();

protected:
    World& m_world;
    std::list<VoxelCollision> m_voxelCollisions;
    std::vector<voxellancer::protocol::ShieldHitEvent> m_shieldHitEvents;
    bool m_shieldHitEventReplicationEnabled = false;

    void interceptShieldHits(
        const std::vector<std::pair<WorldObject*, glm::vec3>>&
            projectileStartPositions
    );
};
