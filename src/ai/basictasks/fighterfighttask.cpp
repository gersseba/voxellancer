#include "fighterfighttask.h"

#include <limits>

#include "ai/boardcomputer.h"
#include "benchmark/benchmarkprofiler.h"
#include "utils/geometryhelper.h"
#include "utils/safenormalize.h"
#include "utils/worldobjectgeometryhelper.h"
#include "voxel/voxelclusterbounds.h"
#include "worldobject/ship.h"
#include "worldobject/helper/componentsinfo.h"


FighterFightTask::FighterFightTask(BoardComputer* boardComputer, const std::vector<Handle<WorldObject>>& targets) :
    FightTaskImplementation(boardComputer, targets)
{
    m_state = State::IDLE;
    m_minEnemyDistance = 100.0f;
    m_stateChanged = false;
}

void FighterFightTask::update(float deltaSec) {
    if (updateTargets(30)) {
        selectPrimaryTarget();
    }
    updateState();

    WorldObject* worldObject = boardComputer()->worldObject();
    WorldObject* primaryTarget = m_primaryTarget.get();
    if (!primaryTarget) {
        return;
    }

    switch (m_state) {
        case State::IDLE:
            return;
        case State::APPROACH:
            if (angleToTarget(primaryTarget) > 45.0f) {
                boardComputer()->rotateTo(primaryTarget->transform().position());
            } else {
                boardComputer()->rotateTo(primaryTarget->transform().position());
                boardComputer()->moveTo(primaryTarget->transform().position());
            }

            if (targetDistance() < componentsInfo().maxBulletRange()) {
                boardComputer()->shootBullet(m_primaryTarget);
            }
            break;
        case State::ENGAGE:
            if (angleToTarget(primaryTarget) > 45.0f) {
                boardComputer()->moveTo(worldObject->transform().position() + glm::vec3(0, 0, -1) * worldObject->transform().orientation());
                boardComputer()->rotateTo(primaryTarget->transform().position());
            } else {
                boardComputer()->rotateTo(primaryTarget->transform().position());
                boardComputer()->moveTo(primaryTarget->transform().position());
                if (angleToTarget(primaryTarget) < 15.0f && targetDistance() < componentsInfo().maxRocketRange()) {
                    boardComputer()->shootRockets(primaryTarget);
                }
            }

            if (targetDistance() < componentsInfo().maxBulletRange()) {
                boardComputer()->shootBullet(m_primaryTarget);
            }
            break;
        case State::EVADE:
            boardComputer()->rotateTo(m_positionBehindTarget);
            boardComputer()->moveTo(m_positionBehindTarget);
            break;
    }

}

bool FighterFightTask::isFinished() {
    return m_state == State::IDLE;
}

void FighterFightTask::selectPrimaryTarget() {
    WorldObject* currentTarget = m_primaryTarget.get();
    WorldObject* worldObject = boardComputer()->worldObject();
    const float maxBulletRange = componentsInfo().maxBulletRange();
    const float currentDistance = currentTarget
        ? targetDistance()
        : std::numeric_limits<float>::infinity();
    const bool currentInRange = currentTarget && currentDistance < maxBulletRange;

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

        const bool candidateCanEngage = distance < maxBulletRange &&
            angleToTarget(target) <= 45.0f;
        const bool materiallyCloser = distance < currentDistance * 0.75f;
        const bool betterCandidate = currentInRange
            ? candidateCanEngage && materiallyCloser
            : candidateCanEngage || materiallyCloser;
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

void FighterFightTask::updateState() {
    if (!m_primaryTarget.get()) {
        if (m_state != State::IDLE) {
            setState(State::IDLE);
        }
        return;
    }

    if (m_state != State::IDLE && m_targets.empty()) {
        setState(State::IDLE);
    }

    switch (m_state) {
        case State::IDLE:
            if (m_targets.empty() || !m_primaryTarget.get()) {
                return;
            } else {
                setState(State::APPROACH);
            }
            break;
        case State::APPROACH:
            if (targetDistance() < componentsInfo().maxBulletRange()) {
                setState(State::ENGAGE);
            }
            break;
        case State::ENGAGE:
            if (targetDistance() < m_minEnemyDistance) {
                setState(State::EVADE);
                m_positionBehindTarget = findRandomEvasionPoint();
            }
            break;
        case State::EVADE:
            if (pointDistance(m_positionBehindTarget) < 20 && targetDistance() < m_minEnemyDistance) {
                m_positionBehindTarget = findRandomEvasionPoint();
            }
            if (pointDistance(m_positionBehindTarget) > 20)
                break;
            if (targetDistance() < m_minEnemyDistance) {
                break;
            }
            if (targetDistance() < componentsInfo().maxBulletRange()) {
                setState(State::ENGAGE);
            } else {
                setState(State::APPROACH);
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

float FighterFightTask::pointDistance(const glm::vec3& point) {
    WorldObject* worldObject = boardComputer()->worldObject();
    return glm::length(worldObject->transform().position() - point) - worldObject->bounds().minimalGridSphere().radius() * worldObject->transform().scale();
}

glm::vec3 FighterFightTask::findRandomEvasionPoint() {
    WorldObject* worldObject = boardComputer()->worldObject();
    glm::vec3 point(
        boardComputer()->randomFloat(-0.25f, 0.25f),
        boardComputer()->randomFloat(-0.25f, 0.25f),
        -1.0f
    );
    point *= 3 * m_minEnemyDistance;
    point = point * worldObject->transform().orientation();
    point += worldObject->transform().position();
    return point;
}

void FighterFightTask::setState(State newState) {
    m_stateChanged = true;
    m_state = newState;
}

float FighterFightTask::angleToTarget(WorldObject* target) {
    WorldObject* worldObject = boardComputer()->worldObject();
    glm::vec3 shipDirection = glm::vec3(0, 0, -1);
    glm::vec3 targetDirection = glm::inverse(worldObject->transform().orientation()) *
        safeNormalize(target->transform().position() - worldObject->transform().position(),
            glm::vec3(0, 0, 1));
    float angle = GeometryHelper::angleBetween(shipDirection, targetDirection);
    return glm::degrees(angle);
}
