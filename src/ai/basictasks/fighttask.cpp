#include "fighttask.h"

#include <stdexcept>

#include "worldobject/ship.h"
#include "ai/boardcomputer.h"

#include "ai/basictasks/fighterfighttask.h"
#include "ai/basictasks/cruiserfighttask.h"

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
    AiArchetype archetype) :
    AiTask(boardComputer),
    m_archetype(archetype)
{
    switch (m_archetype) {
    case AiArchetype::Fighter:
        m_task.reset(new FighterFightTask(boardComputer, targets));
        break;
    case AiArchetype::Cruiser:
        m_task.reset(new CruiserFightTask(boardComputer, targets));
        break;
    }
}

void FightTask::update(float deltaSec) {
    m_task->update(deltaSec);
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
