#pragma once

#include "scenarios/basescenario.h"

class PirateScenario : public BaseScenario {
public:
    explicit PirateScenario(World& world);

protected:
    virtual void populateWorld() override;

    void createArmada();
};
