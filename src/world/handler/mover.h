#pragma once

#include <list>
#include <utility>
#include <vector>
#include <map>
#include <unordered_set>

#include "collision/shieldcontact.h"
#include "collision/voxelcollision.h"
#include "physics/impulse.h"
#include "protocol/serverprotocol.h"

class World;
class WorldObject;
class Projectile;
class WorldTreeGeode;
class Transform;

class Mover
{
public:
    explicit Mover(World& world);

    void moveWorldObjects(float deltaSec);
    void moveProfiledProjectiles(float deltaSec);
    void applyShieldHullImpacts();
    std::optional<std::pair<WorldObject*,ShieldContact>> findShieldContact(
        WorldObject& moving, const Transform& start, const Transform& end,
        const std::unordered_set<WorldTreeGeode*>& candidates);
    void resolveShieldContact(WorldObject& moving, WorldObject& other, const ShieldContact& contact);

    std::list<VoxelCollision>& voxelCollisions();
    void setShieldHitEventReplicationEnabled(bool enabled);
    std::vector<voxellancer::protocol::ShieldHitEvent>
    drainShieldHitEvents();

protected:
    World& m_world;
    std::list<VoxelCollision> m_voxelCollisions;
    std::vector<voxellancer::protocol::ShieldHitEvent> m_shieldHitEvents;
    bool m_shieldHitEventReplicationEnabled = false;
    using ContactPair = std::pair<std::uint64_t,std::uint64_t>;
    std::map<ContactPair,unsigned> m_previousShieldContacts;
    std::map<ContactPair,unsigned> m_currentShieldContacts;
    std::map<WorldObject*,glm::vec3> m_tickAccelerations;
    float m_tickDuration = 0;
    struct ShieldHullImpact {
        WorldObject* object;
        glm::ivec3 cell;
        glm::vec3 direction;
        float damage;
        float fieldOfDamage;
    };
    std::vector<ShieldHullImpact> m_shieldHullImpacts;
    ContactPair contactPair(WorldObject& a, WorldObject& b) const;
    void moveProfiledProjectile(Projectile& projectile, float deltaSec, unsigned impacts = 0);

    void interceptShieldHits(
        const std::vector<std::pair<WorldObject*, glm::vec3>>&
            projectileStartPositions
    );
};
