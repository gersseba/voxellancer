#include "shieldshowcasescenario.h"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ai/character.h"
#include "equipment/shield.h"
#include "equipment/shieldslot.h"
#include "factions/factionmatrix.h"
#include "factions/factionrelation.h"
#include "resource/worldobjectbuilder.h"
#ifndef VOXELLANCER_HEADLESS_SERVER
#include "player.h"
#endif
#include "world/god.h"
#include "world/world.h"
#include "worldobject/ship.h"
#include "worldobject/worldobjectcomponents.h"
#include "worldobject/worldobjectinfo.h"

namespace {
    void setShieldHitCapacity(Ship& ship, int maxHits) {
        for (const std::shared_ptr<ShieldSlot>& slot :
            ship.components().shieldSlots()) {
            if (slot && slot->shield()) {
                slot->shield()->setMaxHits(maxHits);
                return;
            }
        }
        throw std::logic_error(
            "Shield showcase cruiser has no equipped shield.");
    }
}

ShieldShowcaseScenario::ShieldShowcaseScenario(World& world) :
    BaseScenario(world)
{
}

void ShieldShowcaseScenario::populateWorld() {
    FactionMatrix& factions = m_world->factionMatrix();
    Faction& policeFaction = factions.policeFaction();
    factions.getRelationToPlayer(policeFaction).setFriendliness(100.0f);

    Ship* hullShieldCruiser = worldObjectBuilder("bc304").buildShip();
    hullShieldCruiser->transform().setPosition(glm::vec3(-450.0f, 0.0f, 0.0f));
    hullShieldCruiser->transform().setOrientation(
        glm::angleAxis(-glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)));
    hullShieldCruiser->info().setName("Shielded cruiser");
    hullShieldCruiser->info().setShowOnHud(true);
    hullShieldCruiser->info().setCanLockOn(true);
    hullShieldCruiser->character()->setFaction(policeFaction);
    setShieldHitCapacity(*hullShieldCruiser, 100);
    m_world->god().scheduleSpawn(hullShieldCruiser);

    Ship* sphereShieldCruiser = worldObjectBuilder("normandy").buildShip();
    sphereShieldCruiser->transform().setPosition(glm::vec3(450.0f, 0.0f, 0.0f));
    sphereShieldCruiser->transform().setOrientation(
        glm::angleAxis(glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f)));
    sphereShieldCruiser->info().setName("Shielded cruiser");
    sphereShieldCruiser->info().setShowOnHud(true);
    sphereShieldCruiser->info().setCanLockOn(true);
    sphereShieldCruiser->character()->setFaction(policeFaction);
    setShieldHitCapacity(*sphereShieldCruiser, 100);
    m_world->god().scheduleSpawn(sphereShieldCruiser);

    Ship* playerShip = worldObjectBuilder("f302").buildShip();
    playerShip->transform().setPosition(glm::vec3(0.0f, 0.0f, 0.0f));
    playerShip->info().setName("Player fighter");
    playerShip->info().setShowOnHud(true);
    playerShip->character()->setFaction(factions.playerFaction());
    setShieldHitCapacity(*playerShip, 100);
    m_world->god().scheduleSpawn(playerShip);
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (m_world->hasPresentation()) {
        m_world->player().setShip(playerShip);
    }
#endif
}
