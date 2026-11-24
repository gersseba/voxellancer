#pragma once

#include "geometry/ray.h"
#include "geometry/voxelray.h"


class Voxel;
class WorldObject;

/**
 * Identifies the position of the voxel a worldObject is aiming at
 */
class Aimer {
public:
    Aimer(WorldObject* worldObject);
    CrosshairAim aim(const Ray& ray);

    void setWorldObject(WorldObject* worldObject);

protected:
    WorldObject* m_worldObject;
};

