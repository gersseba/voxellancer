#pragma once

#include <cstdint>

#include "scenarios/basescenario.h"

class LargeCombatScenario : public BaseScenario {
public:
    static constexpr int ScenarioId = 8;
    static constexpr std::uint32_t PlacementSeed = 20261104;
    static constexpr unsigned int SimulationRandomSeed = 20261105;

    explicit LargeCombatScenario(
        World& world,
        int shipsPerTeam = 400,
        int squadSize = 0);

protected:
    void populateWorld() override;

private:
    int m_shipsPerTeam;
    int m_squadSize;

    void populateLargeCombat();
};
