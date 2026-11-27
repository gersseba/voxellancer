#include "world.h"

#include <chrono>
#include <stdexcept>

#include "benchmark/benchmarkprofiler.h"
#include "events/eventpoller.h"

#include "ai/boardcomputer.h"
#include "factions/factionmatrix.h"
#include "utils/staggeredphase.h"

#ifndef VOXELLANCER_HEADLESS_SERVER
#include "voxeleffect/voxelparticleengine.h"
#include "voxeleffect/combateffects.h"
#endif

#include "worldtree/worldtree.h"

#include "worldobject/ship.h"
#include "worldobject/worldobject.h"
#include "collision/collisiondetector.h"

#ifndef VOXELLANCER_HEADLESS_SERVER
#include "display/rendering/skybox.h"
#endif
#include "scripting/scriptengine.h"

#include "worldlogic.h"
#include "god.h"
#ifndef VOXELLANCER_HEADLESS_SERVER
#include "player.h"
#endif
#include "missions/missionsystem.h"

namespace {
    std::uint64_t entityIdKey(voxellancer::protocol::EntityId id) {
        return (static_cast<std::uint64_t>(id.generation) << 32) | id.index;
    }
}

World *World::s_instance = nullptr;

World::World() :
    World(Mode::Client)
{
}

World::World(Mode mode):
    m_deltaSec(0.0f),
    m_simulationTickIndex(0),
    m_mode(mode),
#ifndef VOXELLANCER_HEADLESS_SERVER
    m_player(mode == Mode::Client ? std::unique_ptr<Player>(new Player()) : nullptr),
#endif
    m_scriptEngine(new ScriptEngine(*this)),
#ifndef VOXELLANCER_HEADLESS_SERVER
    m_skybox(mode == Mode::Client ? std::unique_ptr<Skybox>(new Skybox()) : nullptr),
#endif
    m_worldLogic(new WorldLogic(*this)),
    m_worldTree(new WorldTree()),
    m_god(new God(*this)),
#ifndef VOXELLANCER_HEADLESS_SERVER
    m_particleEngine(mode == Mode::Client
        ? std::unique_ptr<VoxelParticleEngine>(new VoxelParticleEngine(*this))
        : nullptr),
    m_combatEffects(mode == Mode::Client ? std::make_unique<CombatEffects>() : nullptr),
#endif
    m_factionMatrix(new FactionMatrix()),
    m_eventPoller(new EventPoller(*this)),
    m_missionSystem(new MissionSystem(*this))
{
#ifdef VOXELLANCER_HEADLESS_SERVER
    if (mode != Mode::Server) {
        throw std::invalid_argument("A headless server world requires server mode.");
    }
#endif
}

World::~World() {
    destroyWorldObjects();
}

World::Mode World::mode() const {
    return m_mode;
}

ShieldType World::shieldShape() const {
    return m_shieldShape;
}

void World::setShieldShape(const std::string& value) {
    m_shieldShape = shieldShapeFromString(value);
}

bool World::hasPresentation() const {
#ifdef VOXELLANCER_HEADLESS_SERVER
    return false;
#else
    return m_mode == Mode::Client;
#endif
}

Player& World::player() {
#ifdef VOXELLANCER_HEADLESS_SERVER
    throw std::logic_error("A headless server world has no client player.");
#else
    if (!m_player) {
        throw std::logic_error("A server world has no client player.");
    }
    return *m_player;
#endif
}

Skybox &World::skybox() {
#ifdef VOXELLANCER_HEADLESS_SERVER
    throw std::logic_error("A headless server world has no skybox.");
#else
    if (!m_skybox) {
        throw std::logic_error("A server world has no skybox.");
    }
    return *m_skybox;
#endif
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
#ifdef VOXELLANCER_HEADLESS_SERVER
    throw std::logic_error("A headless server world has no voxel-particle engine.");
#else
    if (!m_particleEngine) {
        throw std::logic_error("A server world has no voxel-particle engine.");
    }
    return *m_particleEngine;
#endif
}

ScriptEngine& World::scriptEngine() {
    return *m_scriptEngine;
}

#ifndef VOXELLANCER_HEADLESS_SERVER
CombatEffects& World::combatEffects() {
    if (!m_combatEffects) throw std::logic_error("A server world has no combat effects.");
    return *m_combatEffects;
}
#endif

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

std::optional<voxellancer::protocol::EntityId> World::entityId(
    const WorldObject* worldObject
) const {
    const auto id = m_entityIds.find(worldObject);
    if (id == m_entityIds.end()) {
        return std::nullopt;
    }
    return id->second;
}

WorldObject* World::worldObject(voxellancer::protocol::EntityId id) const {
    if (!m_entityIdAllocator.isAlive(id)) {
        return nullptr;
    }

    const auto worldObject = m_worldObjectsByEntityId.find(entityIdKey(id));
    return worldObject == m_worldObjectsByEntityId.end() ? nullptr : worldObject->second;
}

void World::update(float deltaSecs) {
    using Clock = std::chrono::steady_clock;
    BenchmarkCpuScope worldUpdateScope(BenchmarkMetric::WorldUpdate);
    m_lastUpdateTimings = WorldUpdateTimings{};
    ++m_simulationTickIndex;
    m_deltaSec = deltaSecs;

    auto sectionStart = Clock::now();
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (hasPresentation()) {
        BenchmarkCpuScope scope(BenchmarkMetric::PlayerUpdate);
        m_player->update(deltaSecs);
    }
#endif
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
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (hasPresentation()) {
        BenchmarkCpuScope scope(BenchmarkMetric::ParticleEngineUpdate);
        m_particleEngine->update(deltaSecs);
    }
#endif
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

void World::clear() {
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (m_combatEffects) m_combatEffects->clear();
#endif
    m_worldLogic.reset();
    m_eventPoller.reset();
    m_missionSystem.reset();
    if (m_god) {
        m_god->clearPending();
    }
    destroyWorldObjects();

    m_scriptEngine.reset();
    m_god.reset();
    m_worldTree.reset();
    m_factionMatrix.reset();

    m_scriptEngine = std::make_unique<ScriptEngine>(*this);
    m_worldTree = std::make_unique<WorldTree>();
    m_worldLogic = std::make_unique<WorldLogic>(*this);
    m_god = std::make_unique<God>(*this);
    m_factionMatrix = std::make_unique<FactionMatrix>();
    m_eventPoller = std::make_unique<EventPoller>(*this);
    m_missionSystem = std::make_unique<MissionSystem>(*this);

    m_deltaSec = 0.0f;
    m_lastUpdateTimings = WorldUpdateTimings{};
}

void World::destroyWorldObjects() {
    if (m_god) {
        m_god->clearPending();
    }
    while (!m_worldObjects.empty()) {
        WorldObject* worldObject = *m_worldObjects.begin();
        if (worldObject->collisionDetector().geode() != nullptr) {
            m_worldTree->remove(worldObject->collisionDetector().geode());
        }
        removeWorldObject(worldObject);
        delete worldObject;
    }
}

std::uint64_t World::simulationTickIndex() const {
    return m_simulationTickIndex;
}

const WorldUpdateTimings& World::lastUpdateTimings() const {
    return m_lastUpdateTimings;
}

float World::deltaSec() const {
    return m_deltaSec;
}

World *World::instance() {
    if(s_instance == nullptr) {
        s_instance = new World(Mode::Client);
    }

    return s_instance;
}

World* World::instance(Mode mode) {
    if (s_instance == nullptr) {
        s_instance = new World(mode);
    } else if (s_instance->mode() != mode) {
        throw std::logic_error("The active world was created with a different runtime mode.");
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
    if (worldObject->world() != nullptr && worldObject->world() != this) {
        throw std::logic_error("A world object cannot be attached to multiple worlds.");
    }
    if (!m_worldObjects.insert(worldObject).second) {
        return;
    }

    const auto entityId = m_entityIdAllocator.create();
    if (!entityId) {
        m_worldObjects.erase(worldObject);
        throw std::overflow_error("No server entity IDs are available.");
    }
    worldObject->setWorld(*this);
    m_entityIds.emplace(worldObject, *entityId);
    m_worldObjectsByEntityId.emplace(entityIdKey(*entityId), worldObject);

    switch(worldObject->objectType()) {
        case WorldObjectType::Ship:
            {
                Ship* ship = static_cast<Ship*>(worldObject);
                const std::uint64_t key = entityIdKey(*entityId);
                ship->boardComputer()->seedRandom(key);
                const std::uint32_t phaseKey = static_cast<std::uint32_t>(
                    key ^ (key >> 32));
                ship->setAiUpdatePhase(
                    StaggeredPhase::mix(phaseKey) % Ship::AiUpdateIntervalTicks);
                m_ships.insert(ship);
            }
            m_scriptEngine->registerScriptable(worldObject);
        break;
    }
}

void World::removeWorldObject(WorldObject* worldObject) {
    m_worldObjects.erase(worldObject);
    const auto entityId = m_entityIds.find(worldObject);
    if (entityId != m_entityIds.end()) {
        m_worldObjectsByEntityId.erase(entityIdKey(entityId->second));
        m_entityIdAllocator.release(entityId->second);
        m_entityIds.erase(entityId);
    }

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
    int particleCount = 0;
#ifndef VOXELLANCER_HEADLESS_SERVER
    particleCount = m_particleEngine ? m_particleEngine->particleCount() : 0;
#endif

    glow::info("World: status report");
    glow::info("  Worldobjects: %;", worldObjectCount);
    glow::info("  VoxelCount: %;", voxelCount);
    glow::info("  ParticleCount: %;", particleCount);
}
