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
    if (updateTargets(deltaSec, 1.0f)) {
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

        if (targetDistance() < m_maxRocketDistance) {
            boardComputer()->shootRockets(primaryTarget);
        }

        boardComputer()->shootBullet(m_targets, deltaSec);
        break;
    }
}

bool CruiserFightTask::isFinished() {
    return m_state == State::IDLE;
}

void CruiserFightTask::selectPrimaryTarget() {
    WorldObject* currentTarget = m_primaryTarget.get();
    WorldObject* worldObject = boardComputer()->worldObject();
    const float currentDistance = currentTarget
        ? targetDistance()
        : std::numeric_limits<float>::infinity();
    const bool currentCanFireRockets = currentTarget && currentDistance < m_maxRocketDistance;

    Handle<WorldObject> nearestTarget;
    float nearestDistance = std::numeric_limits<float>::infinity();
    Handle<WorldObject> betterTarget;
    float betterDistance = std::numeric_limits<float>::infinity();

    for (Handle<WorldObject>& targetHandle : m_targets) {
        WorldObject* target = targetHandle.get();
        if (!target) {
            continue;
        }
        BenchmarkProfiler::instance().addCounter(
            BenchmarkCounter::PrimaryTargetCandidatesConsidered);

        const float distance =
            WorldObjectGeometryHelper::sphereToSphereDistance(worldObject, target);
        if (!currentTarget && distance < nearestDistance) {
            nearestDistance = distance;
            nearestTarget = targetHandle;
        }

        if (target == currentTarget || !currentTarget) {
            continue;
        }

        const bool candidateCanFireRockets = distance < m_maxRocketDistance;
        const bool materiallyCloser = distance < currentDistance * 0.75f;
        const bool betterCandidate = currentCanFireRockets
            ? candidateCanFireRockets && materiallyCloser
            : candidateCanFireRockets || materiallyCloser;
        if (betterCandidate &&
            distance < betterDistance) {
            betterDistance = distance;
            betterTarget = targetHandle;
        }
    }

    const Handle<WorldObject>& selectedTarget = currentTarget ? betterTarget : nearestTarget;
    if (selectedTarget.get() && selectedTarget.get() != currentTarget) {
        m_primaryTarget = selectedTarget;
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
