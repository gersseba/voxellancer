#include "missionscenario.h"

#include "missions/mission.h"
#include "missions/missionsystem.h"

#include "player.h"

#include "resource/worldobjectbuilder.h"

#include "worldobject/worldobjectinfo.h"

#include "world/world.h"
#include "world/god.h"
#include "worldobject/ship.h"


MissionScenario::MissionScenario(World& world, const std::string& path):
    BaseScenario(world),
    m_path(path)
{
}

void MissionScenario::populateWorld() {
    Ship *playerShip = worldObjectBuilder("mox").buildShip();
    playerShip->transform().setPosition(glm::vec3(0, 0, 10));
    playerShip->info().setName("mox");
    m_world->god().scheduleSpawn(playerShip);

    if (m_world->hasPresentation()) {
        m_world->player().setShip(playerShip);
    }
    m_mission = std::make_shared<Mission>(*m_world, m_path);
    m_world->missionSystem().addMission(m_mission);
}
