#pragma once

#include <cstddef>
#include <cstdint>
#include <list>
#include <vector>

#include "handler/mover.h"
#include "handler/damageforwarder.h"
#include "handler/damager.h"
#include "handler/voxelcollisionaccumulator.h"
#include "handler/damageimpactgenerator.h"
#include "handler/impulsegenerator.h"
#include "handler/splitter.h"
#include "handler/garbagecollector.h"
#include "handler/wrecker.h"
#include "handler/impulsor.h"
#include "handler/voxelhangman.h"
#include "handler/splitdetector.h"
#include "protocol/serverprotocol.h"


class World;
class WorldObject;

struct WorldLogicTimings {
    double movementMs = 0.0;
    double collisionDamageMs = 0.0;
    double lifecycleMs = 0.0;
};

class WorldLogic {
public:
    static constexpr std::size_t MaxQueuedWeaponFiredEvents = 4096;

    WorldLogic(World &world);

    void update(float deltaSecs);
    void setVoxelReplicationEnabled(bool enabled);
    void setShieldHitEventReplicationEnabled(bool enabled);
    void setWeaponEventReplicationEnabled(bool enabled);
    void recordWeaponFired(
        const WorldObject* owner,
        int group,
        const glm::ivec3& cell
    );
    void processDamageImpacts(std::list<DamageImpact> damageImpacts);
    std::vector<voxellancer::protocol::VoxelRemovalEvent>
    drainVoxelRemovalEvents();
    std::vector<voxellancer::protocol::VoxelDamageEvent>
    drainVoxelDamageEvents();
    std::vector<voxellancer::protocol::ShieldHitEvent>
    drainShieldHitEvents();
    std::vector<voxellancer::protocol::WeaponFiredEvent>
    drainWeaponFiredEvents();
    std::uint64_t voxelCollisionPairCount() const;
    const WorldLogicTimings& lastUpdateTimings() const;

    DamageForwarder &damageForwarder();


protected:
    World &m_world;
    std::list<DamageImpact> m_damageImpacts;
    std::vector<voxellancer::protocol::VoxelRemovalEvent> m_voxelRemovalEvents;
    std::vector<voxellancer::protocol::VoxelDamageEvent> m_voxelDamageEvents;
    std::vector<voxellancer::protocol::WeaponFiredEvent> m_weaponFiredEvents;
    bool m_voxelRemovalReplicationEnabled = false;
    bool m_weaponEventReplicationEnabled = false;
    std::uint64_t m_voxelCollisionPairCount;
    WorldLogicTimings m_lastUpdateTimings;

    Mover m_mover;
    VoxelCollisionAccumulator m_voxelCollisionAccumulator;
    ImpulseGenerator m_impulseGenerator;
    Impulsor m_impulsor;
    Damager m_damager;
    DamageForwarder m_damageForwarder;
    DamageImpactGenerator m_damageImpactGenerator;
    SplitDetector m_splitDetector;
    Splitter m_splitter;
    Wrecker m_wrecker;
    GarbageCollector m_garbageCollector;
    VoxelHangman m_voxelHangman;

    void damageForwardLoop(std::list<DamageImpact> damageImpacts);
};
