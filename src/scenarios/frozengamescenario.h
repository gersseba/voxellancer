#pragma once

#include "scenarios/basescenario.h"

class FrozenGameScenario : public BaseScenario {
public:
    explicit FrozenGameScenario(World& world);

protected:
    virtual void populateWorld() override;
};
