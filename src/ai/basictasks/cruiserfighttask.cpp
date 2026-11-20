#include "cruiserfighttask.h"

#include <limits>

#include "ai/boardcomputer.h"
#include "benchmark/benchmarkprofiler.h"
#include "utils/randfloat.h"
#include "utils/worldobjectgeometryhelper.h"
#include "voxel/voxelclusterbounds.h"
#include "worldobject/ship.h"
#include "worldobject/helper/componentsinfo.h"


CruiserFightTask::CruiserFightTask(BoardComputer* boardComputer, const std::vector<Handle<WorldObject>>& targets) :
    FightTaskImplementation(boardComputer, targets)
{
    m_state = State::IDLE;
    m_maxRocketDistance = 150.0f;
    m_minEnemyDistance = 50.0f;
    m_stateChanged = false;
}

void CruiserFightTask::update(float deltaSec) {
    if (updateTargets(60)) {
        selectPrimaryTarget();
    }
    updateState();

    WorldObject* primaryTarget = m_primaryTarget.get();
    if (!primaryTarget) {
        return;
    }

    switch (m_state) {
    case State::IDLE:
        return;
    case State::ENGAGE:
        boardComputer()->rotateTo(primaryTarget->transform().position());

        if (targetDistance() >= m_minEnemyDistance) {
            boardComputer()->moveTo(primaryTarget->transform().position());
        }

        break;
    }
}

bool CruiserFightTask::isFinished() {
    return m_state == State::IDLE;
}

WorldObject* CruiserFightTask::rocketTarget() {
    return m_state == State::ENGAGE && targetDistance() < m_maxRocketDistance ?
        m_primaryTarget.get() : nullptr;
}

void CruiserFightTask::selectPrimaryTarget() {
    if (useSharedSquadTarget()) {
        return;
    }

    WorldObject* currentTarget = m_primaryTarget.get();
    const float currentDistance = currentTarget
        ? targetDistance()
        : std::numeric_limits<float>::infinity();
    const bool currentCanFireRockets = currentTarget && currentDistance < m_maxRocketDistance;
    Handle<WorldObject> selectedTarget = currentTarget
        ? nearestTarget(std::nullopt, currentDistance * 0.75f)
        : nearestTarget(std::nullopt, std::numeric_limits<float>::max());

    if (selectedTarget.get() && currentCanFireRockets &&
        WorldObjectGeometryHelper::sphereToSphereDistance(
            boardComputer()->worldObject(), selectedTarget.get()) >= m_maxRocketDistance) {
        selectedTarget = Handle<WorldObject>();
    }

    if (selectedTarget.get() && selectedTarget.get() != currentTarget) {
        setPrimaryTarget(selectedTarget);
        BenchmarkProfiler::instance().addCounter(BenchmarkCounter::PrimaryTargetSwitches);
    }
}

void CruiserFightTask::updateState() {
    if (!m_primaryTarget.get()) {
        if (m_state != State::IDLE) {
            setState(State::IDLE);
        }
        return;
    }

    switch (m_state) {
    case State::IDLE:
        if (m_targets.empty()) {
            return;
        } else {
            setState(State::ENGAGE);
        }
        break;
    case State::ENGAGE:
        if (m_targets.empty()) {
            setState(State::IDLE);
        }
        break;
    default:
        assert(false);
        return;
    }

    if (m_stateChanged) {
        m_stateChanged = false;
        updateState();
    }
}

void CruiserFightTask::setState(State newState) {
    m_stateChanged = true;
    m_state = newState;
}
