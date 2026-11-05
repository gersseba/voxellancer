#include "worldlogic.h"

#include <chrono>
#include <limits>
#include <stdexcept>
#include <unordered_map>
#include <utility>

#include "benchmark/benchmarkprofiler.h"
#include "world.h"
#include "god.h"
#include "helper/splitdata.h"
#include "voxel/voxel.h"


WorldLogic::WorldLogic(World &world):
    m_world(world),
    m_voxelCollisionPairCount(0),
    m_mover(world),
    m_damager(world)
{

}

void WorldLogic::update(float deltaSecs) {
    using Clock = std::chrono::steady_clock;
    BenchmarkCpuScope worldLogicScope(BenchmarkMetric::WorldLogic);
    m_lastUpdateTimings = WorldLogicTimings{};

    auto sectionStart = Clock::now();
    {
        BenchmarkCpuScope scope(BenchmarkMetric::SpawnSplitCleanup);
        m_world.god().remove();
        m_world.god().spawn();
    }
    m_lastUpdateTimings.lifecycleMs +=
        std::chrono::duration<double, std::milli>(Clock::now() - sectionStart).count();

    sectionStart = Clock::now();
    {
        BenchmarkCpuScope scope(BenchmarkMetric::MovementCollisionDetection);
        m_mover.moveWorldObjects(deltaSecs);
    }
    m_lastUpdateTimings.movementMs =
        std::chrono::duration<double, std::milli>(Clock::now() - sectionStart).count();

    sectionStart = Clock::now();
    {
        BenchmarkCpuScope scope(BenchmarkMetric::CollisionProcessingDamage);
        std::list<VoxelCollision>& collisions = m_mover.voxelCollisions();
        m_voxelCollisionPairCount += static_cast<std::uint64_t>(collisions.size());
        m_voxelCollisionAccumulator.parse(collisions);
        m_voxelCollisionAccumulator.applyOnCollsionHooks();

        m_damageImpactGenerator.parse(m_voxelCollisionAccumulator.worldObjectCollisions());

        m_impulseGenerator.parse(m_voxelCollisionAccumulator.worldObjectCollisions());

        m_impulsor.parse(m_impulseGenerator.worldObjectImpulses());

        processDamageImpacts(m_damageImpactGenerator.damageImpacts());
    }
    m_lastUpdateTimings.collisionDamageMs =
        std::chrono::duration<double, std::milli>(Clock::now() - sectionStart).count();

    sectionStart = Clock::now();
    {
        BenchmarkCpuScope scope(BenchmarkMetric::SpawnSplitCleanup);
        m_splitDetector.searchSplitOffs(m_damager.worldObjectModifications());
        if (m_voxelRemovalReplicationEnabled) {
            for (const std::shared_ptr<SplitData>& split :
                m_splitDetector.splitDataList()) {
                const auto entity = m_world.entityId(split->exWorldObject());
                if (!entity) {
                    continue;
                }
                for (Voxel* voxel : split->splitOffVoxels()) {
                    m_voxelRemovalEvents.push_back({*entity, voxel->cell()});
                }
            }
        }
        m_splitter.split(m_splitDetector.splitDataList());

        m_world.god().scheduleSpawn(m_splitter.splitOffWorldObjects());

        m_wrecker.detectWreckedObjects(m_damager.worldObjectModifications());
        //m_wrecker.applyOnWreckageHooks();

        m_world.god().scheduleRemovals(m_wrecker.wreckedObjects());
        m_world.god().scheduleSpawn(m_wrecker.newWreckages());

        m_garbageCollector.check(m_world.worldObjects()/*m_damager.modifiedVoxelClusters()*/);
        //m_garbageCollector.applyOnGarbageHooks();
        m_world.god().scheduleRemovals(m_garbageCollector.garbageVoxelClusters());

        m_world.god().remove();
        m_world.god().spawn();
        m_damager.reset();
    }
    m_lastUpdateTimings.lifecycleMs +=
        std::chrono::duration<double, std::milli>(Clock::now() - sectionStart).count();
}

void WorldLogic::processDamageImpacts(std::list<DamageImpact> damageImpacts) {
    damageForwardLoop(std::move(damageImpacts));
}

void WorldLogic::setVoxelReplicationEnabled(bool enabled) {
    m_voxelRemovalReplicationEnabled = enabled;
    if (!enabled) {
        m_voxelRemovalEvents.clear();
        m_voxelDamageEvents.clear();
    }
}

void WorldLogic::setShieldHitEventReplicationEnabled(bool enabled) {
    m_damager.setShieldHitEventReplicationEnabled(enabled);
}

void WorldLogic::setWeaponEventReplicationEnabled(bool enabled) {
    m_weaponEventReplicationEnabled = enabled;
    if (!enabled) {
        m_weaponFiredEvents.clear();
    }
}

void WorldLogic::recordWeaponFired(
    const WorldObject* owner,
    int group,
    const glm::ivec3& cell
) {
    if (!m_weaponEventReplicationEnabled) {
        return;
    }
    const auto entity = m_world.entityId(owner);
    if (!entity) {
        throw std::logic_error("Weapon firing owner has no server entity ID.");
    }
    if (group < 0 ||
        group > std::numeric_limits<std::uint16_t>::max()) {
        throw std::overflow_error("Weapon slot group exceeds the protocol limit.");
    }
    if (m_weaponFiredEvents.size() >= MaxQueuedWeaponFiredEvents) {
        throw std::overflow_error("Weapon-fired event queue exceeded its limit.");
    }
    m_weaponFiredEvents.push_back({
        *entity,
        m_world.simulationTickIndex(),
        static_cast<std::uint16_t>(group),
        cell
    });
}

std::vector<voxellancer::protocol::VoxelRemovalEvent>
WorldLogic::drainVoxelRemovalEvents() {
    std::vector<voxellancer::protocol::VoxelRemovalEvent> events;
    events.swap(m_voxelRemovalEvents);
    return events;
}

std::vector<voxellancer::protocol::VoxelDamageEvent>
WorldLogic::drainVoxelDamageEvents() {
    std::vector<voxellancer::protocol::VoxelDamageEvent> events;
    events.swap(m_voxelDamageEvents);
    return events;
}

std::vector<voxellancer::protocol::ShieldHitEvent>
WorldLogic::drainShieldHitEvents() {
    return m_damager.drainShieldHitEvents();
}

std::vector<voxellancer::protocol::WeaponFiredEvent>
WorldLogic::drainWeaponFiredEvents() {
    std::vector<voxellancer::protocol::WeaponFiredEvent> events;
    events.swap(m_weaponFiredEvents);
    return events;
}

std::uint64_t WorldLogic::voxelCollisionPairCount() const {
    return m_voxelCollisionPairCount;
}

const WorldLogicTimings& WorldLogic::lastUpdateTimings() const {
    return m_lastUpdateTimings;
}

 DamageForwarder &WorldLogic::damageForwarder() {
    return m_damageForwarder;
 }

void WorldLogic::damageForwardLoop(std::list<DamageImpact> damageImpacts) {
    while(!damageImpacts.empty()) {
        struct PreviousVoxelHealth {
            const WorldObject* worldObject;
            float hp;
        };
        std::unordered_map<const Voxel*, PreviousVoxelHealth> previousHealth;
        for (const DamageImpact& impact : damageImpacts) {
            previousHealth.try_emplace(
                impact.voxel(),
                PreviousVoxelHealth{impact.worldObject(), impact.voxel()->hp()});
        }

        m_damager.applyDamages(damageImpacts);
        if (m_voxelRemovalReplicationEnabled) {
            for (const auto& entry : previousHealth) {
                const Voxel* voxel = entry.first;
                if (voxel->hp() >= entry.second.hp || voxel->hp() <= 0.0f) {
                    continue;
                }
                const auto entity = m_world.entityId(entry.second.worldObject);
                if (entity) {
                    m_voxelDamageEvents.push_back(
                        {*entity, voxel->cell(), voxel->hp()});
                }
            }
            for (const DamageImpact& impact : m_damager.deadlyDamageImpacts()) {
                const auto entity = m_world.entityId(impact.worldObject());
                if (entity) {
                    m_voxelRemovalEvents.push_back(
                        {*entity, impact.voxel()->cell()});
                }
            }
        }

        m_damageForwarder.forwardDamageImpacts(m_damager.deadlyDamageImpacts());
        m_damageForwarder.dontForwardTo(m_damager.deadVoxels());

        m_voxelHangman.applyOnDestructionHooks(m_damager.deadlyDamageImpacts());
        m_voxelHangman.removeDestroyedVoxels(m_damager.deadlyDamageImpacts());

        damageImpacts = m_damageForwarder.forwardedDamageImpacts();
    }
}
