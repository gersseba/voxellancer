#include "fighttask.h"

#include <cmath>
#include <limits>
#include <stdexcept>

#include "worldobject/ship.h"
#include "ai/boardcomputer.h"
#include "equipment/shield.h"
#include "equipment/shieldslot.h"
#include "voxel/voxel.h"
#include "worldobject/worldobjectcomponents.h"

#include "ai/basictasks/fighterfighttask.h"
#include "ai/basictasks/cruiserfighttask.h"
#include "ai/basictasks/interceptorfighttask.h"

namespace {
    AiArchetype defaultArchetype(BoardComputer* boardComputer) {
        if (boardComputer == nullptr) {
            throw std::invalid_argument("FightTask requires a board computer.");
        }
        Ship* ship = dynamic_cast<Ship*>(boardComputer->worldObject());
        if (ship == nullptr) {
            throw std::invalid_argument("FightTask requires a ship owner.");
        }
        return ship->shipType() == ShipType::Fighter
            ? AiArchetype::Fighter
            : AiArchetype::Cruiser;
    }
}

FightTask::FightTask(BoardComputer* boardComputer, const std::vector<Handle<WorldObject>>& targets) :
    FightTask(
        boardComputer,
        targets,
        defaultArchetype(boardComputer))
{
}

FightTask::FightTask(
    BoardComputer* boardComputer,
    const std::vector<Handle<WorldObject>>& targets,
    AiArchetype archetype,
    const ShootingSkill& skill) :
    AiTask(boardComputer),
    m_archetype(archetype),
    m_shooting(boardComputer,skill)
{
    switch (m_archetype) {
    case AiArchetype::Fighter:
        m_task.reset(new FighterFightTask(boardComputer, targets));
        break;
    case AiArchetype::Cruiser:
        m_task.reset(new CruiserFightTask(boardComputer, targets));
        break;
    case AiArchetype::Interceptor:
        m_task.reset(new InterceptorFightTask(boardComputer, targets));
        break;
    }
}

void FightTask::update(float deltaSec) {
    if (!m_engaged && !shouldEngage()) {
        WorldObject* nearest = nullptr;
        float nearestDistance = std::numeric_limits<float>::max();
        for (auto& target : m_task->targets()) {
            if (!target.valid()) continue;
            const float distance = glm::distance(target->position(), boardComputer()->worldObject()->position());
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = target.get();
            }
        }
        if (nearest) boardComputer()->rotateTo(nearest->position());
        return;
    }
    m_engaged = true;
    m_task->update(deltaSec);
    m_shooting.update(deltaSec,m_task->targets(),m_task->primaryTarget(),m_task->gunsEnabled());
    if (auto* target = m_task->rocketTarget()) boardComputer()->shootRockets(target);
}

AiArchetype FightTask::archetype() const {
    return m_archetype;
}

WorldObject* FightTask::primaryTarget() {
    return m_task->primaryTarget();
}

std::vector<Handle<WorldObject>>& FightTask::targets() {
    return m_task->targets();
}

void FightTask::addTarget(const Handle<WorldObject>& target) {
    m_task->addTarget(target);
}

void FightTask::setTargets(const std::vector<Handle<WorldObject>>& targets) {
    m_task->setTargets(targets);
}

bool FightTask::isFinished() {
    return m_task->isFinished();
}

void FightTask::setEngagementRange(float range) {
    if (!std::isfinite(range) || range < 0.0f)
        throw std::invalid_argument("FightTask engagement range must be finite and nonnegative.");
    m_engagementRange = range;
    m_engaged = range == 0.0f;
    m_lastIntegrity = glm::vec2(-1.0f);
}

float FightTask::engagementRange() const {
    return m_engagementRange;
}

bool FightTask::engaged() const {
    return m_engaged;
}

void FightTask::engage() {
    m_engaged = true;
}

glm::vec2 FightTask::integrity() {
    WorldObject* owner = boardComputer()->worldObject();
    glm::vec2 total(0.0f);
    for (const auto& entry : owner->voxelMap()) total.x += entry.second->hp();
    for (const auto& slot : owner->components().shieldSlots())
        if (slot->shield()) total.y += slot->shield()->hp();
    return total;
}

bool FightTask::shouldEngage() {
    // Hull and shield are tracked apart so shield regeneration can't mask a hull hit.
    const glm::vec2 current = integrity();
    const bool damaged = m_lastIntegrity.x >= 0.0f &&
        (current.x < m_lastIntegrity.x - 0.01f || current.y < m_lastIntegrity.y - 0.01f);
    m_lastIntegrity = current;
    if (damaged) return true;
    const glm::vec3 position = boardComputer()->worldObject()->position();
    for (auto& target : m_task->targets())
        if (target.valid() && glm::distance(target->position(), position) <= m_engagementRange)
            return true;
    return false;
}
