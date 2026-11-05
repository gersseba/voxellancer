#include "scriptedscenario.h"

#include <glm/glm.hpp>

#include <glowutils/AutoTimer.h>

#include "ai/character.h"
#include "ai/basictasks/fighttask.h"

#include "equipment/hardpoint.h"
#include "equipment/engineslot.h"
#include "equipment/engine.h"
#include "equipment/weapon.h"
#include "equipment/weapons/gun.h"

#include "gamestate/gameplay/gameplay.h"

#include "resource/clustercache.h"
#include "resource/worldobjectbuilder.h"

#include "worldobject/ship.h"

#include "sound/soundmanager.h"

#include "world/world.h"
#include "world/god.h"

#include "player.h"
#include "worldobject/worldobjectinfo.h"
#include "utils/randvec3.h"

#include "scripting/scriptengine.h"
#include "scripting/gameplayscript.h"


ScriptedScenario::ScriptedScenario(World& world, const std::string& path):
    BaseScenario(world),
    m_path(path)
{
}

ScriptedScenario::~ScriptedScenario() = default;

void ScriptedScenario::populateWorld() {
    Ship *playerShip = worldObjectBuilder("mox").buildShip();
    playerShip->transform().setPosition(glm::vec3(0, 0, 10));
    playerShip->info().setName("mox");
    playerShip->info().setShowOnHud(false);
    playerShip->info().setCanLockOn(false);
    m_world->god().scheduleSpawn(playerShip);
    if (m_world->hasPresentation()) {
        m_world->player().setShip(playerShip);
    }

    m_script = std::make_shared<GamePlayScript>(m_world->scriptEngine());
    m_script->load(m_path);

    m_world->scriptEngine().addScript(m_script);
}
