#pragma once

#include <vector>

#include <glm/glm.hpp>

#include "ai/basictasks/fighttaskimplementation.h"

#include "utils/handle/handle.h"


class Ship;
class WorldObject;

/**
 *  FightTask for bigger ships and capitals
 */
class CruiserFightTask : public FightTaskImplementation {
public:
    CruiserFightTask(BoardComputer* boardComputer, const std::vector<Handle<WorldObject>>& targets);

    virtual void update(float deltaSec) override;

    virtual bool isFinished() override;
    WorldObject* rocketTarget() override;


protected:
    enum class State {
        IDLE,
        ENGAGE
    };

    void updateState();
    void selectPrimaryTarget();
    void setState(State newState);

    State m_state;
    bool m_stateChanged;
    float m_maxRocketDistance;
    float m_minEnemyDistance;
};
