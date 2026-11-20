#include "aicomparisonscenario.h"

#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

#include <glm/gtx/quaternion.hpp>

#include "ai/basictasks/fighttask.h"
#include "ai/boardcomputer.h"
#include "ai/character.h"
#include "equipment/hardpoint.h"
#include "equipment/engine.h"
#include "equipment/engineslot.h"
#include "events/eventpoll.h"
#include "events/eventpoller.h"
#include "factions/factionmatrix.h"
#include "factions/factionrelation.h"
#include "physics/physics.h"
#include "resource/worldobjectbuilder.h"
#include "utils/simulationtickrunner.h"
#include "voxel/voxel.h"
#include "world/god.h"
#include "world/world.h"
#include "worldobject/ship.h"
#include "worldobject/worldobjectcomponents.h"
#include "worldobject/worldobjectinfo.h"
#ifndef VOXELLANCER_HEADLESS_SERVER
#include "player.h"
#endif

namespace {
    float hullHP(WorldObject* ship) {
        float hp = 0;
        if (ship) for (const auto& entry : ship->voxelMap()) hp += entry.second->hp();
        return hp;
    }

    void stop(WorldObject* object) {
        if (!object) return;
        auto* ship = static_cast<Ship*>(object);
        ship->character()->setTask(nullptr);
        ship->components().setEngineState(EngineState());
        ship->physics().setSpeed(Speed());
    }
}

struct AiComparisonState {
    std::array<AiComparisonScenario::Duel,AiComparisonScenario::PairCount> duels;
    std::uint64_t startTick = 0;
    std::uint64_t elapsed = 0;
    bool finished = false;

    void update(World& world) {
        if (finished) return;
        elapsed = world.simulationTickIndex()-startTick;
        using Result = AiComparisonScenario::Result;
        const auto limit = AiComparisonScenario::DurationSeconds*SimulationTickRunner::TickRate;
        for (std::size_t index = 0; index < duels.size(); ++index) {
            auto& duel = duels[index];
            if (duel.result != Result::Fighting) continue;
            const bool aAlive = duel.ships[0].valid();
            const bool bAlive = duel.ships[1].valid();
            for (int side = 0; side < 2; ++side) {
                duel.hull[side] = hullHP(duel.ships[side].get())/duel.initialHull[side];
                duel.fire[side] = *duel.statistics[side];
            }
            if ((!aAlive || !bAlive) && duel.firstLossTick == 0)
                duel.firstLossTick = elapsed;
            // Let already-fired pulse bolts resolve before declaring a one-sided kill.
            if ((!aAlive && !bAlive) ||
                    (duel.firstLossTick && elapsed >= duel.firstLossTick+2*SimulationTickRunner::TickRate)) {
                duel.result = aAlive ? Result::SideAWin :
                    bAlive ? Result::SideBWin : Result::MutualLoss;
            } else if (elapsed >= static_cast<std::uint64_t>(limit) && !duel.firstLossTick) {
                duel.result = Result::Timeout;
            } else continue;
            duel.resolvedTick = elapsed;
            for (auto& ship : duel.ships) stop(ship.get());
            std::cout << "AI_COMPARISON pair=" << index+1
                << " experiment=" << AiComparisonScenario::experimentName(duel.experiment)
                << " mirrored=" << duel.mirrored << " geometry=" << duel.geometry
                << " result=" << AiComparisonScenario::resultName(duel.result)
                << " hull_a=" << duel.hull[0] << " hull_b=" << duel.hull[1];
            for (int side = 0; side < 2; ++side) {
                const char suffix = side == 0 ? 'a' : 'b';
                const auto& skill = duel.skills[side];
                const auto& fire = duel.fire[side];
                std::cout << " behaviour_" << suffix << "=" <<
                    AiComparisonScenario::behaviourName(duel.behaviours[side])
                    << " lead_" << suffix << "=" << skill.lead
                    << " tracking_" << suffix << "=" << skill.tracking
                    << " accuracy_" << suffix << "=" << skill.accuracy
                    << " shots_" << suffix << "=" << fire.shots
                    << " hits_" << suffix << "=" << fire.hits
                    << " hit_rate_" << suffix << "=";
                if (fire.shots) std::cout << double(fire.hits)/fire.shots;
                else std::cout << "NA";
            }
            std::cout << " ticks=" << elapsed << std::endl;
        }
        finished = std::all_of(duels.begin(),duels.end(),
            [](const auto& duel) { return duel.result != Result::Fighting; });
        if (finished) {
            std::array<int,5> counts{};
            for (const auto& duel : duels) ++counts[static_cast<std::size_t>(duel.result)];
            for (int experiment = 0; experiment < 4; ++experiment) {
                std::array<int,5> group{};
                std::array<ShotStatistics,2> fire{};
                for (const auto& duel : duels) {
                    if (static_cast<int>(duel.experiment) != experiment) continue;
                    ++group[static_cast<std::size_t>(duel.result)];
                    for (int side = 0; side < 2; ++side) {
                        fire[side].shots += duel.fire[side].shots;
                        fire[side].hits += duel.fire[side].hits;
                    }
                }
                std::cout << "AI_COMPARISON GROUP experiment=" <<
                    AiComparisonScenario::experimentName(
                        static_cast<AiComparisonScenario::Experiment>(experiment))
                    << " pairs=4 a_wins=" << group[1] << " b_wins=" << group[2]
                    << " mutual_losses=" << group[3] << " timeouts=" << group[4]
                    << " shots_a=" << fire[0].shots << " hits_a=" << fire[0].hits
                    << " shots_b=" << fire[1].shots << " hits_b=" << fire[1].hits << std::endl;
            }
            std::cout << "AI_COMPARISON FINAL a_wins=" << counts[1]
                << " b_wins=" << counts[2] << " mutual_losses=" << counts[3]
                << " timeouts=" << counts[4] << " ticks=" << elapsed << std::endl;
        }
    }
};

namespace {
    class ComparisonPoll : public EventPoll {
    public:
        ComparisonPoll(World& world, std::shared_ptr<AiComparisonState> state) :
            EventPoll(Callback()), m_world(world), m_state(std::move(state)) {}
        void update(float) override { m_state->update(m_world); }
        bool isDead() override { return m_state->finished; }
    private:
        bool poll() override { return false; }
        World& m_world;
        std::shared_ptr<AiComparisonState> m_state;
    };
}

AiComparisonScenario::AiComparisonScenario(World& world) :
    BaseScenario(world), m_state(std::make_shared<AiComparisonState>())
{
}

const std::array<AiComparisonScenario::Duel,AiComparisonScenario::PairCount>&
AiComparisonScenario::duels() const { return m_state->duels; }
std::uint64_t AiComparisonScenario::elapsedTicks() const { return m_state->elapsed; }
bool AiComparisonScenario::finished() const { return m_state->finished; }

const char* AiComparisonScenario::resultName(Result result) {
    switch (result) {
    case Result::Fighting: return "FIGHTING";
    case Result::SideAWin: return "A_WIN";
    case Result::SideBWin: return "B_WIN";
    case Result::MutualLoss: return "MUTUAL_LOSS";
    case Result::Timeout: return "TIMEOUT";
    }
    throw std::invalid_argument("Unknown AI duel result.");
}

const char* AiComparisonScenario::experimentName(Experiment experiment) {
    switch (experiment) {
    case Experiment::Behaviour: return "FLIGHT";
    case Experiment::Lead: return "LEAD";
    case Experiment::Tracking: return "TRACKING";
    case Experiment::Accuracy: return "ACCURACY";
    }
    throw std::invalid_argument("Unknown AI comparison experiment.");
}

const char* AiComparisonScenario::behaviourName(AiArchetype archetype) {
    switch (archetype) {
    case AiArchetype::Fighter: return "FIGHTER";
    case AiArchetype::Cruiser: return "CRUISER";
    case AiArchetype::Interceptor: return "INTERCEPTOR";
    }
    throw std::invalid_argument("Unknown AI flight behaviour.");
}

void AiComparisonScenario::populateWorld() {
    m_state = std::make_shared<AiComparisonState>();
    m_state->startTick = m_world->simulationTickIndex();
    auto& factions = m_world->factionMatrix();
    factions.getRelation(factions.pirateFaction(),factions.policeFaction()).setFriendliness(-100);
    for (std::size_t index = 0; index < PairCount; ++index) {
        auto& duel = m_state->duels[index];
        duel.mirrored = index%2 != 0;
        duel.geometry = index < 4 ? static_cast<int>(index/2) : 1;
        if (index >= 4) {
            duel.experiment = static_cast<Experiment>(1+(index-4)/4);
            duel.behaviours[0] = AiArchetype::Interceptor;
            const float value = ((index-4)%4)/2 == 0 ? 0.0f : 0.5f;
            switch (duel.experiment) {
            case Experiment::Lead: duel.skills[0].lead = value; break;
            case Experiment::Tracking: duel.skills[0].tracking = value; break;
            case Experiment::Accuracy: duel.skills[0].accuracy = value; break;
            case Experiment::Behaviour: break;
            }
        }
        duel.center = glm::vec3(float(index%5)*50000,0,float(index/5)*50000);
        const int geometry = duel.geometry;
        const float gap = 500;
        for (int position = 0; position < 2; ++position) {
            const int side = duel.mirrored ? 1-position : position;
            auto* ship = worldObjectBuilder("contractraider").buildShip();
            const float sign = position == 0 ? -1.0f : 1.0f;
            const auto offset = glm::vec3(sign*gap*0.5f,0,0);
            ship->transform().setPosition(duel.center+offset);
            glm::vec3 facing(-sign,0,0);
            if (geometry == 1) facing = glm::normalize(glm::vec3(-sign,0,sign));
            ship->transform().setOrientation(glm::rotation(glm::vec3(0,0,-1),facing));
            ship->physics().setSpeed(Speed(glm::vec3(0,0,
                geometry == 1 ? sign*25 : 0),glm::vec3(0)));
            ship->info().setName("Duel "+std::to_string(index+1)+(side == 0 ? " A " : " B ")+
                experimentName(duel.experiment));
            ship->info().setInvulnerable(false);
            ship->character()->setFaction(side == 0 ? factions.pirateFaction() : factions.policeFaction());
            duel.ships[side] = ship->handle<WorldObject>();
            duel.initialHull[side] = hullHP(ship);
            m_world->god().scheduleSpawn(ship);
        }
    }
    m_world->god().spawn();
    for (std::size_t index = 0; index < PairCount; ++index) {
        auto& duel = m_state->duels[index];
        for (int side = 0; side < 2; ++side) {
            auto* ship = static_cast<Ship*>(duel.ships[side].get());
            ship->boardComputer()->seedRandom(7100+duel.geometry);
            ship->setAiUpdatePhase(0);
            auto task = std::make_shared<FightTask>(ship->boardComputer(),
                std::vector<Handle<WorldObject>>{duel.ships[1-side]},
                duel.behaviours[side],duel.skills[side]);
            task->shooting().seed(8100+duel.geometry);
            duel.statistics[side] = task->shooting().statistics();
            ship->character()->setTask(task);
        }
    }
    m_world->eventPoller().addPoll(std::make_shared<ComparisonPoll>(*m_world,m_state));
    std::cout << "AI_COMPARISON START pairs=" << PairCount << " seconds=" << DurationSeconds
        << " ship=contractraider groups=4 skill_levels=0,0.5,1 mirrored=1"
        << " shots=gun_volleys hits=volleys_touching_designated_opponent" << std::endl;
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (m_world->hasPresentation()) {
        auto* observer = worldObjectBuilder("mox").buildShip();
        observer->info().setName("AI comparison observer");
        observer->info().setInvulnerable(true);
        for (const auto& mount : observer->components().hardpoints()) mount->setWeapon(nullptr);
        m_world->god().scheduleSpawn(observer);
        m_world->god().spawn();
        m_world->player().setShip(observer);
        watchPair(0);
    }
#endif
}

void AiComparisonScenario::watchPair(std::size_t index) {
    if (index >= PairCount) throw std::out_of_range("Unknown AI comparison pair.");
    m_watchedPair = index;
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (!m_world->hasPresentation()) return;
    glm::vec3 center(0);
    int alive = 0;
    for (const auto& ship : duels()[index].ships)
        if (ship.valid()) { center += ship.get()->position(); ++alive; }
    center = alive ? center/float(alive) : duels()[index].center;
    auto* observer = m_world->player().ship();
    if (!observer) throw std::logic_error("AI comparison observer is missing.");
    observer->updateTransformAndGeode(center+glm::vec3(0,180,650),
        glm::rotation(glm::vec3(0,0,-1),glm::normalize(glm::vec3(0,-180,-650))));
    observer->physics().setSpeed(Speed());
    // Teleports must not connect the old and new emitter positions with a trail.
    for (const auto& slot : observer->components().engineSlots())
        if (slot->engine()) slot->engine()->setEngineSlot(slot.get());
#endif
}
