#include "damager.h"

#include <cmath>
#include <set>
#include <stdexcept>

#include <glow/logging.h>

#include "equipment/shield.h"
#include "equipment/shieldslot.h"
#include "geometry/sphere.h"

#include "voxel/voxel.h"
#include "voxel/voxelclusterbounds.h"

#include "worldobject/ship.h"
#include "worldobject/worldobject.h"
#include "worldobject/worldobjectcomponents.h"

#include "world/world.h"
#include "world/helper/damageimpact.h"

#ifndef VOXELLANCER_HEADLESS_SERVER
#include "player.h"
#endif


Damager::Damager(World& world) :
    m_world(world),
    m_playerShipUndestroyable("general.playerShipUndestroyable")
{
}

void Damager::applyDamages(std::list<DamageImpact> &damageImpacts) {
    m_dampedDeadlyDamageImpacts.clear();
    m_deadlyDamageImpacts.clear();
    m_deadVoxels.clear();

    for(DamageImpact &damageImpact : damageImpacts) {
#ifndef VOXELLANCER_HEADLESS_SERVER
        if (m_playerShipUndestroyable && m_world.hasPresentation() &&
            m_world.player().ship() == damageImpact.worldObject()) {
            continue;
        }
#endif

        const float incomingDamage = damageImpact.damage();
        applyShields(damageImpact);
        const float absorbedDamage = incomingDamage - damageImpact.damage();
        if (m_shieldHitEventReplicationEnabled && absorbedDamage > 0.0f) {
            const auto entity = m_world.entityId(damageImpact.worldObject());
            if (!entity) {
                throw std::logic_error(
                    "Shield impact owner has no server entity ID.");
            }
            if (m_shieldHitEvents.size() >= MaxQueuedShieldHitEvents ||
                !std::isfinite(absorbedDamage)) {
                throw std::overflow_error(
                    "Shield-hit event queue exceeded its valid limit.");
            }
            const glm::vec3 localCell(
                damageImpact.voxel()->cell());
            const Sphere boundsSphere =
                damageImpact.worldObject()->bounds().sphere(
                    damageImpact.worldObject()->transform());
            const glm::vec3 localCenter =
                damageImpact.worldObject()->transform().inverseApplyTo(
                    boundsSphere.position());
            glm::vec3 localImpactPosition = localCell;
            Ship* ship = dynamic_cast<Ship*>(damageImpact.worldObject());
            glm::vec3 localNormal = localCell - localCenter;
            if (glm::length(localNormal) < 0.001f) {
                localNormal = glm::vec3(0.0f, 1.0f, 0.0f);
            } else {
                localNormal = glm::normalize(localNormal);
            }
            if (ship != nullptr &&
                ship->shieldType() == ShieldType::Sphere) {
                localImpactPosition = localCenter + localNormal *
                    (boundsSphere.radius() /
                        damageImpact.worldObject()->transform().scale() +
                     0.08f);
            } else {
                localImpactPosition += localNormal * 0.8f;
            }
            m_shieldHitEvents.push_back({
                *entity,
                m_world.simulationTickIndex(),
                damageImpact.voxel()->cell(),
                localImpactPosition,
                0xffffff,
                absorbedDamage
            });
        }
        if(damageImpact.damage() == 0) {
            continue;
        }

        Voxel *voxel = damageImpact.voxel();

        float hpBeforeDamage = voxel->hp();
        voxel->applyDamage(damageImpact.damage());

        if(voxel->hp() <= 0) {
            m_dampedDeadlyDamageImpacts.push_back(dampDamageImpact(damageImpact, damageImpact.damage() - hpBeforeDamage));
            m_deadlyDamageImpacts.push_back(damageImpact);
            m_deadVoxels.push_back(voxel);

            auto i = m_worldObjectModificationMap.find(damageImpact.worldObject());
            if(i == m_worldObjectModificationMap.end()) {
                WorldObjectModification modification(damageImpact.worldObject());
                modification.removedVoxel(voxel->cell());
                m_worldObjectModificationMap.insert(std::pair<WorldObject*, WorldObjectModification>(damageImpact.worldObject(), modification));
            } else {
                i->second.removedVoxel(voxel->cell());
            }
        }
    }
}

void Damager::setShieldHitEventReplicationEnabled(bool enabled) {
    m_shieldHitEventReplicationEnabled = enabled;
    if (!enabled) {
        m_shieldHitEvents.clear();
    }
}

std::vector<voxellancer::protocol::ShieldHitEvent>
Damager::drainShieldHitEvents() {
    std::vector<voxellancer::protocol::ShieldHitEvent> events;
    events.swap(m_shieldHitEvents);
    return events;
}

std::list<DamageImpact> &Damager::dampedDeadlyDamageImpacts() {
    return m_dampedDeadlyDamageImpacts;
}

std::list<DamageImpact> &Damager::deadlyDamageImpacts() {
    return m_deadlyDamageImpacts;
}

std::list<Voxel*> &Damager::deadVoxels() {
    return m_deadVoxels;
}

std::list<WorldObjectModification>& Damager::worldObjectModifications() {
    m_worldObjectModifications.clear();

    for(auto& pair : m_worldObjectModificationMap) {
        m_worldObjectModifications.push_back(pair.second);
    }
    return m_worldObjectModifications;
}

DamageImpact Damager::dampDamageImpact(DamageImpact &undamped, float factor) {
    return DamageImpact(undamped.worldObject(), undamped.voxel(), glm::normalize(undamped.damageVec()) * factor, undamped.fieldOfDamage());
}

void Damager::reset() {
    m_worldObjectModificationMap.clear();
}

void Damager::applyShields(DamageImpact& damageImpact) {
    const Ship* ship = dynamic_cast<const Ship*>(damageImpact.worldObject());
    if (ship != nullptr && ship->shieldType() == ShieldType::None) {
        return;
    }
    damageImpact.setDamage(damageImpact.worldObject()->components().compensateDamage(damageImpact.damage()));
}
