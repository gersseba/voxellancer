#include "worldlogic.h"

#include <chrono>

#include "benchmark/benchmarkprofiler.h"
#include "world.h"
#include "god.h"


WorldLogic::WorldLogic(World &world):
    m_world(world),
    m_voxelCollisionPairCount(0)
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

        damageForwardLoop(m_damageImpactGenerator.damageImpacts());
    }
    m_lastUpdateTimings.collisionDamageMs =
        std::chrono::duration<double, std::milli>(Clock::now() - sectionStart).count();

    sectionStart = Clock::now();
    {
        BenchmarkCpuScope scope(BenchmarkMetric::SpawnSplitCleanup);
        m_splitDetector.searchSplitOffs(m_damager.worldObjectModifications());
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
    }
    m_lastUpdateTimings.lifecycleMs +=
        std::chrono::duration<double, std::milli>(Clock::now() - sectionStart).count();
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
    m_damager.reset();

    while(!damageImpacts.empty()) {

        m_damager.applyDamages(damageImpacts);

        m_damageForwarder.forwardDamageImpacts(m_damager.deadlyDamageImpacts());
        m_damageForwarder.dontForwardTo(m_damager.deadVoxels());

        m_voxelHangman.applyOnDestructionHooks(m_damager.deadlyDamageImpacts());
        m_voxelHangman.removeDestroyedVoxels(m_damager.deadlyDamageImpacts());

        damageImpacts = m_damageForwarder.forwardedDamageImpacts();
    }
}
