#include "friendlyfirescenario.h"

#include <glm/glm.hpp>

#include <glowutils/AutoTimer.h>

#include "ai/character.h"
#include "ai/basictasks/flytotask.h"
#include "ai/grouptasks/patrolwaypointstask.h"
#include "ai/grouptasks/defendareatask.h"
#include "ai/basictasks/formationmembertask.h"
#include "ai/squadlogic.h"
#include "ai/squad.h"

#include "equipment/engineslot.h"
#include "equipment/hardpoint.h"
#include "equipment/weapons/gun.h"

#include "factions/factionmatrix.h"
#include "factions/factionrelation.h"

#include "resource/worldobjectbuilder.h"

#include "worldobject/ship.h"
#include "sound/soundmanager.h"
#include "gamestate/gameplay/gameplay.h"
#include "world/world.h"
#include "voxel/voxel.h"
#include "world/god.h"
#include "player.h"
#include "worldobject/worldobjectinfo.h"


FriendlyFireScenario::FriendlyFireScenario(World& world) :
    BaseScenario(world)
{
}

void FriendlyFireScenario::populateWorld() {
    glow::debug("Create WorldObjects");

    Ship *victim = worldObjectBuilder("mox").buildShip();
    victim->transform().setPosition(glm::vec3(0, 0, 200));
    victim->info().setName("member");
    victim->info().setShowOnHud(true);
    victim->info().setCanLockOn(true);
    victim->character()->setFaction(m_world->factionMatrix().policeFaction());
    m_world->god().scheduleSpawn(victim);

    std::shared_ptr<Squad> attacker = std::make_shared<Squad>();
    attacker->setTask(std::make_shared<DefendAreaTask>(*attacker,
        std::list<glm::vec3>{glm::vec3(0, 0, 0)}, 500.0f));
    Ship* pirate = worldObjectBuilder("piratelight").buildShip();
    pirate->transform().setPosition(glm::vec3(0, -10, -200));
    pirate->transform().setOrientation(glm::quat(glm::vec3(0, 0, 1)));
    pirate->info().setName("pirate");
    pirate->info().setShowOnHud(true);
    pirate->info().setCanLockOn(true);
    pirate->character()->setFaction(m_world->factionMatrix().pirateFaction());
    pirate->squadLogic()->joinSquad(attacker);
    m_world->god().scheduleSpawn(pirate);

    Ship *bigship = worldObjectBuilder("bc304").buildShip();
    bigship->transform().setPosition(glm::vec3(0, 0, 10));
    bigship->transform().setOrientation(glm::quat(glm::vec3(0, 0, 1)));
    bigship->info().setName("big");
    bigship->info().setShowOnHud(true);
    bigship->info().setShowOnHud(true);
    bigship->character()->setFaction(m_world->factionMatrix().pirateFaction());
    m_world->god().scheduleSpawn(bigship);

    Ship *playerShip = worldObjectBuilder("basicship").buildShip();
    playerShip->transform().setPosition(glm::vec3(100, 0, 0));
    playerShip->transform().setOrientation(glm::quat(glm::vec3(0, glm::radians(70.0f), 0)));
    playerShip->info().setName("player");
    if (m_world->hasPresentation()) {
        m_world->player().setShip(playerShip);
    }
    m_world->god().scheduleSpawn(playerShip);

    m_world->factionMatrix().getRelationToPlayer(
        m_world->factionMatrix().pirateFaction()).setFriendliness(100);

    glow::debug("Initial spawn");
    m_world->god().spawn();
}
