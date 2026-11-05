#pragma once

#include "scenarios/basescenario.h"

class FriendlyFireScenario : public BaseScenario {
public:
    explicit FriendlyFireScenario(World& world);

protected:
    virtual void populateWorld() override;
};
