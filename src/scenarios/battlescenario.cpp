#include "battlescenario.h"

#include <algorithm>
#include <random>

#include <glm/glm.hpp>

#include "ai/character.h"
#include "ai/basictasks/fighttask.h"

#include "resource/worldobjectbuilder.h"

#include "equipment/hardpoint.h"
#include "equipment/engineslot.h"
#include "equipment/engine.h"
#include "equipment/weapon.h"
#include "equipment/weapons/gun.h"
#include "worldobject/ship.h"

#include "world/world.h"
#include "world/god.h"

#include "worldobject/worldobject.h"

#include "utils/randvec3.h"
#include "utils/handle/handle.h"
#ifndef VOXELLANCER_HEADLESS_SERVER
#include "player.h"
#endif
#include "worldobject/worldobjectinfo.h"


BattleScenario::BattleScenario(World& world):
    BaseScenario(world)
{
}

Ship* BattleScenario::playerShip() const {
    return m_playerShip;
}

void BattleScenario::populateWorld() {

    glow::debug("Create WorldObjects");

    Ship *playerShip = worldObjectBuilder("mox").buildShip();
    m_playerShip = playerShip;
    playerShip->transform().setPosition(glm::vec3(0, 0, 10));
    playerShip->info().setName("basicship");
    playerShip->info().setShowOnHud(false);
    playerShip->info().setCanLockOn(false);

    m_world->god().scheduleSpawn(playerShip);
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (m_world->hasPresentation()) {
        m_world->player().setShip(playerShip);
    }
#endif

    // create enemy ai driven ship
    Ship *aitester = worldObjectBuilder("basicship").buildShip();
    aitester->transform().setPosition(glm::vec3(0, 0, 10));

    aitester->info().setName("basicship");
    aitester->info().setShowOnHud(false);
    aitester->character()->setTask(std::make_shared<FightTask>(aitester->boardComputer(), std::vector<Handle<WorldObject>>{ playerShip->handle<WorldObject>() }));

    //m_world->god().scheduleSpawn(aitester);


    WorldObject* banner = worldObjectBuilder("banner").buildWorldObject();
    banner->transform().setScale(30.0f);
    banner->transform().move(glm::vec3(0, 0, -600));
    banner->info().setShowOnHud(false);
    banner->info().setCanLockOn(false);
    //m_world->god().scheduleSpawn(banner);

    // create two opposing enemy forces
    populateBattle(3, 3);

    glow::debug("Initial spawn");
    m_world->god().spawn();
}

void BattleScenario::populateBattle(int numberOfEnemies1, int numberOfEnemies2) {
    std::vector<Ship*> fleet1;
    std::vector<Ship*> fleet2;
    for (int e = 0; e < numberOfEnemies1; e++) {
        Ship *ship = worldObjectBuilder("basicship").buildShip();
        float r = 600;
        ship->transform().move(RandVec3::rand(0.0f, r) + glm::vec3(-200, 0, -200));

        ship->info().setName("enemy2");
        ship->info().setShowOnHud(true);
        ship->info().setCanLockOn(true);

        m_world->god().scheduleSpawn(ship);
        fleet2.push_back(ship);
    }
    for (int e = 0; e < numberOfEnemies2; e++) {
        Ship *ship = worldObjectBuilder("basicship").buildShip();
        float r = 600;
        ship->transform().move(RandVec3::rand(0.0f, r) + glm::vec3(200, 0, -200));
        ship->info().setName("enemy1");
        ship->info().setShowOnHud(true);

        m_world->god().scheduleSpawn(ship);
        fleet1.push_back(ship);
    }

    spawnCapital(fleet1);

    setTargets(fleet1, fleet2);
    setTargets(fleet2, fleet1);
}

void BattleScenario::spawnCapital(const std::vector<Ship*>& enemies) {
    Ship *ship = worldObjectBuilder("normandy").buildShip();
    ship->info().setShowOnHud(true);
    ship->info().setCanLockOn(true);

    ship->transform().move(glm::vec3(-200, 300, -200));

    std::vector<Handle<WorldObject>> enemyHandles;
    for (Ship* enemy : enemies) {
        Handle<WorldObject> handle = enemy->handle<WorldObject>();
        enemyHandles.push_back(handle);
    }
    ship->character()->setTask(std::make_shared<FightTask>(ship->boardComputer(), enemyHandles));
    m_world->god().scheduleSpawn(ship);
}

void BattleScenario::setTargets(const std::vector<Ship*>& fleet, const std::vector<Ship*>& enemies) {
    static std::mt19937 randomEngine(std::random_device{}());
    std::vector<Handle<WorldObject>> enemyHandles;
    for (Ship* enemy : enemies) {
        enemyHandles.push_back(enemy->handle<WorldObject>());
    }
    if (m_playerShip) {
        enemyHandles.push_back(m_playerShip->handle<WorldObject>());
    }
    for (Ship* ship : fleet) {
        std::shuffle(enemyHandles.begin(), enemyHandles.end(), randomEngine);
        ship->character()->setTask(std::make_shared<FightTask>(ship->boardComputer(), enemyHandles));
    }
}
