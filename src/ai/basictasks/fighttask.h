#pragma once

#include <vector>
#include <glm/glm.hpp>

#include "ai/aitask.h"
#include "ai/basictasks/fighttaskimplementation.h"

#include "utils/handle/handle.h"


class Ship;
class WorldObject;

enum class AiArchetype {
    Fighter,
    Cruiser
};

class FightTask : public AiTask {
public:
    FightTask(BoardComputer* boardComputer, const std::vector<Handle<WorldObject>>& targets);
    FightTask(
        BoardComputer* boardComputer,
        const std::vector<Handle<WorldObject>>& targets,
        AiArchetype archetype);

    virtual void update(float deltaSec) override;

    AiArchetype archetype() const;
    WorldObject* primaryTarget();
    std::vector<Handle<WorldObject>>& targets();
    virtual void addTarget(const Handle<WorldObject>& targets);
    virtual void setTargets(const std::vector<Handle<WorldObject>>& targets);

    virtual bool isFinished() override;

protected:
    std::unique_ptr<FightTaskImplementation> m_task;
    AiArchetype m_archetype;
};
