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

            break;
        case State::ENGAGE:
            if (angleToTarget(primaryTarget) > 45.0f) {
                boardComputer()->moveTo(worldObject->transform().position() + glm::vec3(0, 0, -1) * worldObject->transform().orientation());
                boardComputer()->rotateTo(primaryTarget->transform().position());
            } else {
                boardComputer()->rotateTo(primaryTarget->transform().position());
                boardComputer()->moveTo(primaryTarget->transform().position());
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

bool FighterFightTask::gunsEnabled() const {
    return m_state != State::EVADE;
}

WorldObject* FighterFightTask::rocketTarget() {
    auto* target = m_primaryTarget.get();
    return target && m_state == State::ENGAGE && angleToTarget(target) < 15.0f &&
        targetDistance() < componentsInfo().maxRocketRange() ? target : nullptr;
}

void FighterFightTask::selectPrimaryTarget() {
    if (useSharedSquadTarget()) {
        return;
    }

    WorldObject* currentTarget = m_primaryTarget.get();
    WorldObject* worldObject = boardComputer()->worldObject();
    const float maxBulletRange = componentsInfo().maxBulletRange();
    const float currentDistance = currentTarget
        ? targetDistance()
        : std::numeric_limits<float>::infinity();
    const bool currentInRange = currentTarget && currentDistance < maxBulletRange;

    Handle<WorldObject> betterTarget;
    float betterDistance = std::numeric_limits<float>::infinity();

    if (!currentTarget) {
        Handle<WorldObject> preferredTarget =
            nearestTarget(ShipType::Fighter, std::numeric_limits<float>::max());
        if (!preferredTarget.get()) {
            preferredTarget =
                nearestTarget(std::nullopt, std::numeric_limits<float>::max());
        }
        if (preferredTarget.get()) {
            setPrimaryTarget(preferredTarget);
        }
        return;
    }

    Ship* currentShip = dynamic_cast<Ship*>(currentTarget);
    if (currentShip && currentShip->shipType() != ShipType::Fighter) {
        Handle<WorldObject> preferredTarget =
            nearestTarget(ShipType::Fighter, std::numeric_limits<float>::max());
        if (preferredTarget.get()) {
            betterTarget = preferredTarget;
        }
    } else {
        betterTarget = nearestTarget(ShipType::Fighter, currentDistance * 0.75f);
        WorldObject* target = betterTarget.get();
        if (target) {
            betterDistance =
                WorldObjectGeometryHelper::sphereToSphereDistance(worldObject, target);
            const bool candidateCanEngage = betterDistance < maxBulletRange &&
                angleToTarget(target) <= 45.0f;
            if (currentInRange && !candidateCanEngage) {
                betterTarget = Handle<WorldObject>();
            }
        }
    }

    if (betterTarget.get() && betterTarget.get() != currentTarget) {
        setPrimaryTarget(betterTarget);
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
