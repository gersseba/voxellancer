#pragma once

#include <optional>
#include <glm/glm.hpp>

class Transform;
class Voxel;
class WorldObject;

struct ShieldContact {
    float time;
    glm::vec3 normal;
    glm::vec3 point;
    Voxel* movingVoxel;
    Voxel* otherVoxel;
    bool movingShield;
    bool otherShield;
};

bool hasSphericalShield(WorldObject& object);
std::optional<ShieldContact> sweepShieldContact(
    WorldObject& moving, const Transform& start, const Transform& end, WorldObject& other,
    bool includeSeparating = false);
