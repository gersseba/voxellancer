#pragma once

#include "scenarios/basescenario.h"

class ShieldShowcaseScenario : public BaseScenario {
public:
    static constexpr int ScenarioId = 6;

    explicit ShieldShowcaseScenario(World& world);

protected:
    void populateWorld() override;
};
