#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

#include "equipment/shieldtype.h"
#include "protocol/serverprotocol.h"

class EventPoller;
class FactionMatrix;
class God;
class MissionSystem;
class Player;
class Ship;
class Skybox;
class ScriptEngine;
class VoxelParticleEngine;
class CombatEffects;
class WorldObject;
class WorldLogic;
class WorldTree;

struct WorldUpdateTimings {
    double playerMs = 0.0;
    double worldLogicMs = 0.0;
    double scriptsMs = 0.0;
    double eventPollerMs = 0.0;
    double particleEngineMs = 0.0;
    double missionSystemMs = 0.0;
    double worldObjectsMs = 0.0;
};

class World {
public:
    enum class Mode {
        Client,
        Server
    };

    World();
    explicit World(Mode mode);
    virtual ~World();

    Mode mode() const;
    bool hasPresentation() const;
    Player& player();
    Skybox& skybox();
    WorldLogic& worldLogic();
    God& god();
    WorldTree& worldTree();
    ScriptEngine& scriptEngine();
    VoxelParticleEngine& particleEngine();
#ifndef VOXELLANCER_HEADLESS_SERVER
    CombatEffects& combatEffects();
#endif
    FactionMatrix& factionMatrix();
    EventPoller& eventPoller();
    MissionSystem& missionSystem();

    std::unordered_set<WorldObject*>& worldObjects();
    std::unordered_set<Ship*>& ships();
    std::optional<voxellancer::protocol::EntityId> entityId(
        const WorldObject* worldObject
    ) const;
    WorldObject* worldObject(voxellancer::protocol::EntityId id) const;

    void printStatus();

    void update(float deltaSecs);
    void clear();
    std::uint64_t simulationTickIndex() const;
    const WorldUpdateTimings& lastUpdateTimings() const;

    float deltaSec() const;
    ShieldType shieldShape() const;
    void setShieldShape(const std::string& value);

    static World* instance();
    static World* instance(Mode mode);
    static void reset(bool showWarning=true);
    

protected:
    friend class God;

    void addWorldObject(WorldObject* worldObject);
    void removeWorldObject(WorldObject* worldObject);
    void destroyWorldObjects();

protected:
    static World* s_instance;

    float m_deltaSec;
    std::uint64_t m_simulationTickIndex;
    Mode m_mode;
    ShieldType m_shieldShape = ShieldType::Sphere;
    WorldUpdateTimings m_lastUpdateTimings;

#ifndef VOXELLANCER_HEADLESS_SERVER
    std::unique_ptr<Player> m_player;
#endif
    std::unique_ptr<ScriptEngine> m_scriptEngine;
#ifndef VOXELLANCER_HEADLESS_SERVER
    std::unique_ptr<Skybox> m_skybox;
#endif
    std::unique_ptr<WorldTree> m_worldTree;
    std::unique_ptr<WorldLogic> m_worldLogic;
    std::unique_ptr<God> m_god;
#ifndef VOXELLANCER_HEADLESS_SERVER
    std::unique_ptr<VoxelParticleEngine> m_particleEngine;
    std::unique_ptr<CombatEffects> m_combatEffects;
#endif
    std::unique_ptr<FactionMatrix> m_factionMatrix;
    std::unique_ptr<EventPoller> m_eventPoller;
    std::unique_ptr<MissionSystem> m_missionSystem;

    std::unordered_set<WorldObject*> m_worldObjects;
    std::unordered_set<Ship*> m_ships;
    voxellancer::protocol::EntityIdAllocator m_entityIdAllocator;
    std::unordered_map<const WorldObject*, voxellancer::protocol::EntityId> m_entityIds;
    std::unordered_map<std::uint64_t, WorldObject*> m_worldObjectsByEntityId;
};
