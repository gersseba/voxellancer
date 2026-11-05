#pragma once

#include <vector>

#include "basescenario.h"


class GamePlay;
class Ship;

class BattleScenario: public BaseScenario {
public:
    static constexpr int ScenarioId = 2;

    explicit BattleScenario(World& world);
    Ship* playerShip() const;

private:
    Ship* m_playerShip = nullptr;

    virtual void populateWorld() override;

    void populateBattle(int numberOfEnemies1, int numberOfEnemies2);
    void setTargets(const std::vector<Ship*>& fleet, const std::vector<Ship*>& enemies);
    void spawnCapital(const std::vector<Ship*>& enemies);
};
