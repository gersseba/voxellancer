#include "world.h"

#include <chrono>

#include "benchmark/benchmarkprofiler.h"
#include "events/eventpoller.h"

#include "factions/factionmatrix.h"

#include "voxeleffect/voxelparticleengine.h"

#include "worldtree/worldtree.h"

#include "worldobject/ship.h"
#include "worldobject/worldobject.h"

#include "display/rendering/skybox.h"
#include "scripting/scriptengine.h"

#include "worldlogic.h"
#include "god.h"
#include "player.h"
#include "missions/missionsystem.h"


World *World::s_instance = nullptr;

World::World():
    m_player(new Player()),
    m_scriptEngine(new ScriptEngine()),
    m_skybox(new Skybox()),
    m_worldLogic(new WorldLogic(*this)),
    m_worldTree(new WorldTree()),
    m_god(new God(*this)),
    m_particleEngine(new VoxelParticleEngine()),
    m_factionMatrix(new FactionMatrix()),
    m_eventPoller(new EventPoller()),
    m_missionSystem(new MissionSystem())
{
}

World::~World() {

}

Player& World::player() {
    return *m_player;
}

Skybox &World::skybox() {
    return *m_skybox;
}

WorldLogic &World::worldLogic() {
    return *m_worldLogic;
}

God &World::god() {
    return *m_god;
}

WorldTree &World::worldTree() {
    return *m_worldTree;
}

VoxelParticleEngine &World::particleEngine() {
    return *m_particleEngine;
}

ScriptEngine& World::scriptEngine() {
    return *m_scriptEngine;
}

FactionMatrix &World::factionMatrix() {
    return *m_factionMatrix;
}

EventPoller &World::eventPoller() {
    return *m_eventPoller;
}

MissionSystem& World::missionSystem() {
    return *m_missionSystem;
}

std::unordered_set<WorldObject*> &World::worldObjects() {
    return m_worldObjects;
}

std::unordered_set<Ship*> &World::ships() {
    return m_ships;
}

void World::update(float deltaSecs) {
    using Clock = std::chrono::steady_clock;
    BenchmarkCpuScope worldUpdateScope(BenchmarkMetric::WorldUpdate);
    m_lastUpdateTimings = WorldUpdateTimings{};
    m_deltaSec = deltaSecs;

    auto sectionStart = Clock::now();
    {
        BenchmarkCpuScope scope(BenchmarkMetric::PlayerUpdate);
        m_player->update(deltaSecs);
    }
    m_lastUpdateTimings.playerMs =
        std::chrono::duration<double, std::milli>(Clock::now() - sectionStart).count();

    sectionStart = Clock::now();
    {
        m_worldLogic->update(deltaSecs);
    }
    m_lastUpdateTimings.worldLogicMs =
        std::chrono::duration<double, std::milli>(Clock::now() - sectionStart).count();

    sectionStart = Clock::now();
    {
        BenchmarkCpuScope scope(BenchmarkMetric::ScriptUpdate);
        m_scriptEngine->update(deltaSecs);
    }
    m_lastUpdateTimings.scriptsMs =
        std::chrono::duration<double, std::milli>(Clock::now() - sectionStart).count();

    sectionStart = Clock::now();
    {
        BenchmarkCpuScope scope(BenchmarkMetric::EventPollerUpdate);
        m_eventPoller->update(deltaSecs);
    }
    m_lastUpdateTimings.eventPollerMs =
        std::chrono::duration<double, std::milli>(Clock::now() - sectionStart).count();

    sectionStart = Clock::now();
    {
        BenchmarkCpuScope scope(BenchmarkMetric::ParticleEngineUpdate);
        m_particleEngine->update(deltaSecs);
    }
    m_lastUpdateTimings.particleEngineMs =
        std::chrono::duration<double, std::milli>(Clock::now() - sectionStart).count();

    sectionStart = Clock::now();
    {
        BenchmarkCpuScope scope(BenchmarkMetric::MissionSystemUpdate);
        m_missionSystem->update(deltaSecs);
    }
    m_lastUpdateTimings.missionSystemMs =
        std::chrono::duration<double, std::milli>(Clock::now() - sectionStart).count();

    sectionStart = Clock::now();
    {
        BenchmarkCpuScope scope(BenchmarkMetric::WorldObjectUpdate);
        for (WorldObject *worldObject : m_worldObjects) {
            worldObject->update(deltaSecs);
        }
    }
    m_lastUpdateTimings.worldObjectsMs =
        std::chrono::duration<double, std::milli>(Clock::now() - sectionStart).count();
}

const WorldUpdateTimings& World::lastUpdateTimings() const {
    return m_lastUpdateTimings;
}

float World::deltaSec() const {
    return m_deltaSec;
}

World *World::instance() {
    if(s_instance == nullptr) {
        s_instance = new World();
    }

    return s_instance;
}

void World::reset(bool showWarning) {
    if (showWarning) {
        glow::warning("world reset!");
    }
    delete s_instance;
    s_instance = nullptr;
}

void World::addWorldObject(WorldObject* worldObject) {
    m_worldObjects.insert(worldObject);

    switch(worldObject->objectType()) {
        case WorldObjectType::Ship:
            m_ships.insert(static_cast<Ship*>(worldObject));
            m_scriptEngine->registerScriptable(worldObject);
        break;
    }
}

void World::removeWorldObject(WorldObject* worldObject) {
    m_worldObjects.erase(worldObject);

    switch(worldObject->objectType()) {
        case WorldObjectType::Ship:
            m_ships.erase(static_cast<Ship*>(worldObject));
        break;
    }

    m_scriptEngine->unregisterScriptable(worldObject);
}

void World::printStatus() {
    int worldObjectCount = m_worldObjects.size();
    int voxelCount = 0;
    for (WorldObject* worldObject : m_worldObjects) {
        voxelCount += worldObject->voxelMap().size();
    }
    int particleCount = m_particleEngine->particleCount();

    glow::info("World: status report");
    glow::info("  Worldobjects: %;", worldObjectCount);
    glow::info("  VoxelCount: %;", voxelCount);
    glow::info("  ParticleCount: %;", particleCount);
}
