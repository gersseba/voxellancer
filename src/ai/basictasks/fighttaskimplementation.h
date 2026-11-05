#pragma once

#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "ai/aitask.h"
#include "utils/handle/handle.h"


class Ship;
class WorldObject;
class ComponentsInfo;

/**
 *  Base Class for all FightTask variants, e.g. for FighterFightTask
 */
class FightTaskImplementation : public AiTask {
public:
    FightTaskImplementation(BoardComputer* boardComputer, const std::vector<Handle<WorldObject>>& targets);

    virtual void update(float deltaSec) override = 0;

    std::vector<Handle<WorldObject>>& targets();
    virtual void addTarget(const Handle<WorldObject>& targets);
    virtual void setTargets(const std::vector<Handle<WorldObject>>& targets);

    virtual bool isFinished() override = 0;

protected:
    std::vector<Handle<WorldObject>> m_targets;
    Handle<WorldObject> m_primaryTarget;

    bool updateTargets(std::uint32_t selectionIntervalTicks);
    float targetDistance();

    const ComponentsInfo& componentsInfo();

private:
    std::uint64_t m_nextTargetSelectionTick;
    bool m_targetSelectionInitialized;
    std::uint32_t m_targetSelectionKey;
    std::uint32_t m_targetSelectionCycle;
};
