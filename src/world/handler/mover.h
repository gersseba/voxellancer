#pragma once

#include <list>
#include "collision/voxelcollision.h"
#include "physics/impulse.h"

class World;

class Mover
{
public:
    explicit Mover(World& world);

    void moveWorldObjects(float deltaSec);

    std::list<VoxelCollision>& voxelCollisions();

protected:
    World& m_world;
    std::list<VoxelCollision> m_voxelCollisions;
};
