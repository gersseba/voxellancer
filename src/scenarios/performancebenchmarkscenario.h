#pragma once

#include <cstdint>
#include <vector>

#include "scenarios/basescenario.h"

enum class BenchmarkMode;
class Ship;

class PerformanceBenchmarkScenario : public BaseScenario {
public:
    static constexpr int RenderingScenarioId = 6;
    static constexpr int CombatScenarioId = 7;
    static constexpr int LargeCombatVsyncScenarioId = 8;
    static constexpr std::uint32_t LargeCombatPlacementSeed = 20261104;
    static constexpr unsigned int SimulationRandomSeed = 20261105;

    PerformanceBenchmarkScenario(GamePlay* gamePlay, BenchmarkMode mode);

protected:
    void populateWorld() override;

private:
    BenchmarkMode m_mode;

    void populateRenderingBenchmark();
    void populateCombatBenchmark();
    void populateLargeCombatVsyncBenchmark();
    void assignCombatTasks(std::vector<Ship*> teams[2]);
};
