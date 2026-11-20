#pragma once

#include "ai/basictasks/fighterfighttask.h"

class InterceptorFightTask : public FighterFightTask {
public:
    InterceptorFightTask(BoardComputer* computer,
        const std::vector<Handle<WorldObject>>& targets);

    void update(float deltaSec) override;
    bool isFinished() override;
    bool gunsEnabled() const override { return true; }
    WorldObject* rocketTarget() override { return nullptr; }

private:
    bool m_separating = false;
};
