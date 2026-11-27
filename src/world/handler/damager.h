#pragma once

#include <cstddef>
#include <map>
#include <list>
#include <vector>

#include "world/helper/damageimpact.h"
#include "world/helper/worldobjectmodification.h"

#include "property/property.h"
#include "protocol/serverprotocol.h"


class WorldObject;
class World;

class Damager
{
public:
    explicit Damager(World& world);

    bool canDamage(WorldObject& object) const;
    void applyDamages(std::list<DamageImpact> &damageImpacts);
    void applyShieldContact(WorldObject& object, float damage, glm::vec3 position);
    void setShieldHitEventReplicationEnabled(bool enabled);
    std::vector<voxellancer::protocol::ShieldHitEvent>
    drainShieldHitEvents();
    void reset();

    std::list<DamageImpact> &dampedDeadlyDamageImpacts();
    std::list<DamageImpact> &deadlyDamageImpacts();
    std::list<Voxel*>& deadVoxels();
    std::list<WorldObjectModification>& worldObjectModifications();


protected:
    World& m_world;
    std::list<DamageImpact> m_dampedDeadlyDamageImpacts;
    std::list<DamageImpact> m_deadlyDamageImpacts;
    std::list<Voxel*> m_deadVoxels;
    std::map<WorldObject*, WorldObjectModification> m_worldObjectModificationMap;
    std::list<WorldObjectModification> m_worldObjectModifications;
    std::vector<voxellancer::protocol::ShieldHitEvent> m_shieldHitEvents;
    bool m_shieldHitEventReplicationEnabled = false;

    DamageImpact dampDamageImpact(DamageImpact& undamped, float factor);
    void applyShields(DamageImpact& damageImpact);

    Property<bool> m_playerShipUndestroyable;
    static constexpr std::size_t MaxQueuedShieldHitEvents = 4096;
};
