#pragma once

#include <cstdint>
#include <list>

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


class World;

struct WorldLogicTimings {
    double movementMs = 0.0;
    double collisionDamageMs = 0.0;
    double lifecycleMs = 0.0;
};

class WorldLogic {
public:
    WorldLogic(World &world);

    void update(float deltaSecs);
    std::uint64_t voxelCollisionPairCount() const;
    const WorldLogicTimings& lastUpdateTimings() const;

    DamageForwarder &damageForwarder();


protected:
    World &m_world;
    std::list<DamageImpact> m_damageImpacts;
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

    void damageForwardLoop(std::list<DamageImpact> damageImpulses);
};
