#pragma once

#include <vector>
#include <glm/glm.hpp>

#include "ai/aitask.h"
#include "ai/basictasks/fighttaskimplementation.h"
#include "ai/shootingcontroller.h"

#include "utils/handle/handle.h"


class Ship;
class WorldObject;

enum class AiArchetype {
    Fighter,
    Cruiser,
    Interceptor
};

class FightTask : public AiTask {
public:
    FightTask(BoardComputer* boardComputer, const std::vector<Handle<WorldObject>>& targets);
    FightTask(
        BoardComputer* boardComputer,
        const std::vector<Handle<WorldObject>>& targets,
        AiArchetype archetype,
        const ShootingSkill& skill = ShootingSkill::defaults());

    virtual void update(float deltaSec) override;

    AiArchetype archetype() const;
    ShootingController& shooting() { return m_shooting; }
    const ShootingController& shooting() const { return m_shooting; }
    WorldObject* primaryTarget();
    std::vector<Handle<WorldObject>>& targets();
    virtual void addTarget(const Handle<WorldObject>& targets);
    virtual void setTargets(const std::vector<Handle<WorldObject>>& targets);

    virtual bool isFinished() override;

    /**
     * Holds the ship in place, facing the nearest target, until a target comes
     * within range or the ship takes damage. A range of 0 engages immediately.
     */
    void setEngagementRange(float range);
    float engagementRange() const;
    bool engaged() const;
    void engage();

protected:
    std::unique_ptr<FightTaskImplementation> m_task;
    AiArchetype m_archetype;
    ShootingController m_shooting;
    float m_engagementRange = 0.0f;
    bool m_engaged = true;
    glm::vec2 m_lastIntegrity{-1.0f};

    bool shouldEngage();
    glm::vec2 integrity();
};
