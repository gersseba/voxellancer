#include "fighttaskimplementation.h"

#include <algorithm>
#include <stdexcept>

#include "ai/boardcomputer.h"
#include "utils/staggeredphase.h"
#include "utils/worldobjectgeometryhelper.h"
#include "world/world.h"
#include "worldobject/worldobject.h"
#include "worldobject/worldobjectcomponents.h"
#include "worldobject/helper/componentsinfo.h"

FightTaskImplementation::FightTaskImplementation(BoardComputer* boardComputer, const std::vector<Handle<WorldObject>>& targets) :
    AiTask(boardComputer),
    m_targets(targets),
    m_nextTargetSelectionTick(0),
    m_targetSelectionInitialized(false),
    m_targetSelectionKey(StaggeredPhase::keyForPosition(
        boardComputer->worldObject()->transform().position())),
    m_targetSelectionCycle(0)
{
    for (const Handle<WorldObject>& target : m_targets) {
        if (target.get()) {
            m_primaryTarget = target;
            break;
        }
    }
}

std::vector<Handle<WorldObject>>& FightTaskImplementation::targets() {
    return m_targets;
}

void FightTaskImplementation::addTarget(const Handle<WorldObject>& target) {
    m_targets.push_back(target);
}

void FightTaskImplementation::setTargets(const std::vector<Handle<WorldObject>>& targets) {
    m_targets = targets;
    WorldObject* primaryTarget = m_primaryTarget.get();
    if (primaryTarget && std::none_of(m_targets.begin(), m_targets.end(),
            [primaryTarget](const Handle<WorldObject>& target) {
                return target.get() == primaryTarget;
            })) {
        m_primaryTarget = Handle<WorldObject>();
    }
}

bool FightTaskImplementation::updateTargets(std::uint32_t selectionIntervalTicks) {
    WorldObject* owner = boardComputer()->worldObject();
    World* world = owner ? owner->world() : nullptr;
    if (world == nullptr) {
        throw std::logic_error("Fight task owner is not attached to a world.");
    }
    const std::uint64_t tick = world->simulationTickIndex();
    if (!m_targetSelectionInitialized) {
        m_nextTargetSelectionTick = tick + StaggeredPhase::initialPhaseTicks(
            m_targetSelectionKey, selectionIntervalTicks);
        m_targetSelectionInitialized = true;
    }

    if (tick < m_nextTargetSelectionTick) {
        return false;
    }

    auto iterator = m_targets.begin();
    while (iterator != m_targets.end()) {
        if (!iterator->get()) {
            iterator = m_targets.erase(iterator);
        } else {
            iterator++;
        }
    }

    WorldObject* primaryTarget = m_primaryTarget.get();
    if (primaryTarget && std::none_of(m_targets.begin(), m_targets.end(),
            [primaryTarget](const Handle<WorldObject>& target) {
                return target.get() == primaryTarget;
            })) {
        m_primaryTarget = Handle<WorldObject>();
    }

    m_nextTargetSelectionTick = tick + StaggeredPhase::periodTicks(
        m_targetSelectionKey,
        selectionIntervalTicks,
        m_targetSelectionCycle++);
    return true;
}

float FightTaskImplementation::targetDistance() {
    return WorldObjectGeometryHelper::sphereToSphereDistance(
        boardComputer()->worldObject(), m_primaryTarget.get());
}

const ComponentsInfo& FightTaskImplementation::componentsInfo() {
    return boardComputer()->worldObject()->components().componentsInfo();
}
