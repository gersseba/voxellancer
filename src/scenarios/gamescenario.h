#pragma once

#include "scenarios/basescenario.h"

class GameScenario : public BaseScenario {
public:
    explicit GameScenario(World& world);

protected:
    virtual void populateWorld() override;

    void createArmada();
    void spawnPoliceFleet();
    void spawnPirateFleet();
    void spawnStuff();
};
