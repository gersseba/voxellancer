#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <glm/glm.hpp>

#include "scenarios/basescenario.h"
#include "utils/handle/handle.h"
#include "ai/basictasks/fighttask.h"

class WorldObject;
struct AiComparisonState;

class AiComparisonScenario : public BaseScenario {
public:
    static constexpr int ScenarioId = 9;
    static constexpr std::size_t PairCount = 16;
    static constexpr int DurationSeconds = 120;
    enum class Result { Fighting, SideAWin, SideBWin, MutualLoss, Timeout };
    enum class Experiment { Behaviour, Lead, Tracking, Accuracy };
    struct Duel {
        std::array<Handle<WorldObject>,2> ships;
        std::array<float,2> initialHull{};
        std::array<float,2> hull{1,1};
        Result result = Result::Fighting;
        std::uint64_t firstLossTick = 0;
        std::uint64_t resolvedTick = 0;
        glm::vec3 center{0};
        Experiment experiment = Experiment::Behaviour;
        std::array<AiArchetype,2> behaviours{AiArchetype::Fighter,AiArchetype::Interceptor};
        std::array<ShootingSkill,2> skills{};
        std::array<std::shared_ptr<ShotStatistics>,2> statistics;
        std::array<ShotStatistics,2> fire{};
        bool mirrored = false;
        int geometry = 0;
    };

    explicit AiComparisonScenario(World& world);
    const std::array<Duel,PairCount>& duels() const;
    std::uint64_t elapsedTicks() const;
    bool finished() const;
    void watchPair(std::size_t index);
    std::size_t watchedPair() const { return m_watchedPair; }
    static const char* resultName(Result result);
    static const char* experimentName(Experiment experiment);
    static const char* behaviourName(AiArchetype archetype);

protected:
    void populateWorld() override;

private:
    std::shared_ptr<AiComparisonState> m_state;
    std::size_t m_watchedPair = 0;
};
