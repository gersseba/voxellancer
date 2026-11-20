#include "boardcomputer.h"

#include "benchmark/benchmarkprofiler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

#include <glm/gtc/quaternion.hpp>

#include "collision/collisionfilter.h"

#include "utils/safenormalize.h"
#include "utils/geometryhelper.h"
#include "utils/staggeredphase.h"

#include "physics/physics.h"

#include "world/world.h"
#include "worldobject/ship.h"
#include "worldobject/worldobjectcomponents.h"
#include "worldobject/helper/componentsinfo.h"

#include "equipment/hardpoint.h"
#include "equipment/weapon.h"
#include "equipment/weapons/gun.h"
#include "utils/worldobjectgeometryhelper.h"
#include "voxel/specialvoxels/hardpointvoxel.h"


static const float s_minActDistance = 0.5f;
static const float s_minActAngle = glm::radians(2.0f);
static const float s_minAutoUpAngle = glm::radians(10.0f);

namespace {
    World& requireWorld(WorldObject* worldObject) {
        if (worldObject == nullptr || worldObject->world() == nullptr) {
            throw std::invalid_argument(
                "BoardComputer requires a world-owned object.");
        }
        return *worldObject->world();
    }
}

BoardComputer::BoardComputer(WorldObject* worldObject) :
    BoardComputer(worldObject, requireWorld(worldObject))
{
}

BoardComputer::BoardComputer(WorldObject* worldObject, World& world) :
    m_worldObject(worldObject),
    m_world(&world),
    m_overwriteEngineState(false)
{
}

WorldObject* BoardComputer::worldObject() {
    return m_worldObject;
}

void BoardComputer::seedRandom(std::uint64_t seed) {
    const std::uint32_t low = static_cast<std::uint32_t>(seed);
    const std::uint32_t high = static_cast<std::uint32_t>(seed >> 32);
    std::seed_seq sequence{low, high};
    m_randomEngine.seed(sequence);
}

float BoardComputer::randomFloat(float from, float to) {
    return std::uniform_real_distribution<float>(from, to)(m_randomEngine);
}

const EngineState& BoardComputer::engineState() const {
    return m_engineState;
}

void BoardComputer::setEngineState(const EngineState& state) {
    m_engineState = state;
    m_overwriteEngineState = true;
}

void BoardComputer::moveTo(const glm::vec3& position, bool decelerate) {
    glm::vec3 projectedPosition = m_worldObject->physics().projectedTransformIn(1.0f).position();
    glm::vec3 delta = position - projectedPosition;
    float distance = glm::length(delta);

    if (distance > s_minActDistance) {
        if (!decelerate && m_worldObject->physics().speed().directional() != glm::vec3(0)) {
            glm::vec3 currentPosition = m_worldObject->transform().position();
            float angleFromProjected = GeometryHelper::angleBetween(m_worldObject->physics().speed().directional(), delta);
            float angleFromCurrent = GeometryHelper::angleBetween(m_worldObject->physics().speed().directional(), position - currentPosition);

            if (angleFromCurrent < glm::quarter_pi<float>() && angleFromProjected > glm::half_pi<float>()) {
                // the projection is already past the target, but we don't want to deaccelerate
                // instead, project the target from our current position to a sphere around our position
                float projectionDistance = glm::length(projectedPosition - currentPosition);
                glm::vec3 fakePosition = currentPosition + safeNormalize(position - currentPosition, glm::vec3(0, 0, 0)) * 1.5f * projectionDistance;

                delta = fakePosition - projectedPosition;
            }
        }

        glm::vec3 direction = glm::inverse(m_worldObject->transform().orientation()) * safeNormalize(delta, glm::vec3(0, 0, 0));
        m_engineState.setDirectional(direction);
    }

    m_overwriteEngineState = true;
}

void BoardComputer::rotateTo(const glm::vec3& position, const glm::vec3& up) {
    glm::vec3 accumulatedEuler;

    // A guess (hack) where the WorldObject will point to in one second, in the local coordinate-sys
    glm::quat projectedOrientation = glm::inverse(m_worldObject->transform().orientation()) * m_worldObject->physics().projectedTransformIn(0.1f).orientation();
    glm::vec3 projectedDirection = projectedOrientation * glm::vec3(0, 0, -1);

    // Direction to the target, in the local coordinate-sys
    if (position == m_worldObject->transform().position()) {
        return;
    }

    auto deltaToTarget = safeNormalize(position - m_worldObject->transform().position());
    glm::vec3 targetDirection = deltaToTarget.valid() ?
                                    glm::inverse(m_worldObject->transform().orientation()) * deltaToTarget.get() :
                                    glm::vec3(0, 0, 1);

    // The rotation that needs to be performed, in the local coordinate-sys
    glm::quat rotation = GeometryHelper::quatFromTo(projectedDirection, targetDirection);

    if (glm::abs(glm::angle(rotation)) > s_minActAngle) {
        accumulatedEuler = glm::eulerAngles(rotation);

    }

    if (up != glm::vec3(0, 0, 0)){
        accumulatedEuler += rotateUpTo(up);
    } else {
        accumulatedEuler += rotateUpAuto(rotation);
    }

    accumulatedEuler = glm::sign(accumulatedEuler);
    m_engineState.setAngular(accumulatedEuler);

    m_overwriteEngineState = true;
}

glm::vec3 BoardComputer::rotateUpTo(const glm::vec3& up) {
    glm::vec3 upDirection = glm::vec3(0, 1, 0);
    glm::vec3 newUpDirection = glm::inverse(m_worldObject->transform().orientation()) * safeNormalize(up, glm::vec3(0, 1, 0));
    glm::quat upRotation = GeometryHelper::quatFromTo(upDirection, newUpDirection);

    if (glm::abs(glm::angle(upRotation)) > s_minActAngle) {
        glm::vec3 euler = glm::eulerAngles(upRotation);
        return euler * 0.5f;
    }

    return glm::vec3(0.0f);
}

glm::vec3 BoardComputer::rotateUpAuto(const glm::quat& rotation) {
    //make it look naturally, e.g. up is to the "inside" of the rotation
    if (glm::abs(glm::angle(rotation)) > s_minAutoUpAngle) {
        glm::vec3 upDirection = glm::vec3(0, 1, 0);
        glm::vec3 newUpDirection = glm::vec3(0, 0, 1) + (rotation * glm::vec3(0, 0, -1));
        glm::quat upRotation = GeometryHelper::quatFromTo(upDirection, newUpDirection);
        glm::vec3 euler = glm::eulerAngles(upRotation);

        return euler * 0.5f;
    }

    return glm::vec3(0.0f);
}

void BoardComputer::shootBullet(
    std::vector<Handle<WorldObject>>& targets
) {
    BenchmarkCpuScope scope(BenchmarkMetric::ShootingTargetSelection);
    shootBulletsAtTargets(&targets, nullptr, m_world->simulationTickIndex());
}

void BoardComputer::shootBullet(Handle<WorldObject>& target) {
    BenchmarkCpuScope scope(BenchmarkMetric::ShootingTargetSelection);
    shootBulletsAtTargets(nullptr, &target, m_world->simulationTickIndex());
}

void BoardComputer::shootBulletsAtTargets(
    std::vector<Handle<WorldObject>>* targets,
    Handle<WorldObject>* focusedTarget,
    std::uint64_t tick
) {
    constexpr std::uint32_t GunTargetSelectionIntervalTicks = 45;
    std::list<std::shared_ptr<Hardpoint>>& hardpoints = m_worldObject->components().hardpoints();
    const float maxBulletRange = m_worldObject->components().componentsInfo().maxBulletRange();

    m_gunTargetSelections.erase(
        std::remove_if(
            m_gunTargetSelections.begin(),
            m_gunTargetSelections.end(),
            [&hardpoints](const GunTargetSelection& selection) {
                return std::none_of(hardpoints.begin(), hardpoints.end(),
                    [&selection](const std::shared_ptr<Hardpoint>& hardpoint) {
                        return hardpoint && hardpoint.get() == selection.hardpoint &&
                            hardpoint->weapon() &&
                            hardpoint->weapon()->type() == WeaponType::Gun;
                    });
            }),
        m_gunTargetSelections.end());

    for (const std::shared_ptr<Hardpoint>& hardpoint : hardpoints) {
        if (!hardpoint || !hardpoint->weapon() ||
            hardpoint->weapon()->type() != WeaponType::Gun) {
            continue;
        }

        auto selectionIterator = std::find_if(
            m_gunTargetSelections.begin(),
            m_gunTargetSelections.end(),
            [&hardpoint](const GunTargetSelection& selection) {
                return selection.hardpoint == hardpoint.get();
            });
        if (selectionIterator == m_gunTargetSelections.end()) {
            m_gunTargetSelections.push_back(GunTargetSelection{});
            selectionIterator = m_gunTargetSelections.end() - 1;
            selectionIterator->hardpoint = hardpoint.get();
        }

        GunTargetSelection& selection = *selectionIterator;
        if (selection.weapon != hardpoint->weapon().get()) {
            selection.weapon = hardpoint->weapon().get();
            selection.target = Handle<WorldObject>();
            selection.scheduleKey = StaggeredPhase::keyForPosition(
                hardpoint->voxel()->position());
            selection.refreshCycle = 0;
            selection.initialized = false;
        }

        WorldObject* currentTarget = selection.target.get();
        if (!selection.initialized) {
            selection.nextRefreshTick = tick + StaggeredPhase::initialPhaseTicks(
                selection.scheduleKey, GunTargetSelectionIntervalTicks);
            selection.initialized = true;
        }
        if (tick >= selection.nextRefreshTick) {
            BenchmarkProfiler::instance().addCounter(BenchmarkCounter::GunTargetRefreshes);
            const Handle<WorldObject> selectedTarget = selectGunTarget(
                hardpoint.get(), targets, focusedTarget, selection.target);
            if (selectedTarget.get() && selectedTarget.get() != currentTarget) {
                BenchmarkProfiler::instance().addCounter(BenchmarkCounter::GunTargetsAssigned);
            }
            selection.target = selectedTarget;
            selection.nextRefreshTick = tick + StaggeredPhase::periodTicks(
                selection.scheduleKey,
                GunTargetSelectionIntervalTicks,
                selection.refreshCycle++);
        }

        WorldObject* target = selection.target.get();
        if (target && WorldObjectGeometryHelper::sphereToSphereDistance(
                m_worldObject, target) < maxBulletRange) {
            const glm::vec3 targetDirection = target->position() - m_worldObject->position();
            glm::vec3 inaccuracyNoise(
                randomFloat(0.0f, 1.0f),
                randomFloat(0.0f, 1.0f),
                randomFloat(0.0f, 1.0f)
            );
            inaccuracyNoise *= glm::length(targetDirection) / 30.0f;
            Gun* gun = static_cast<Gun*>(hardpoint->weapon().get());
            gun->fireAtPoint(target->position() + inaccuracyNoise, true);
        }
    }
}

Handle<WorldObject> BoardComputer::selectGunTarget(
    Hardpoint* hardpoint,
    std::vector<Handle<WorldObject>>* targets,
    Handle<WorldObject>* focusedTarget,
    Handle<WorldObject>& currentTarget
) {
    constexpr float BetterTargetDistanceRatio = 0.75f;
    WorldObject* current = currentTarget.get();
    const float maxBulletRange = m_worldObject->components().componentsInfo().maxBulletRange();
    const float currentDistance = current
        ? WorldObjectGeometryHelper::sphereToSphereDistance(m_worldObject, current)
        : std::numeric_limits<float>::infinity();
    bool currentEligible = current && currentDistance < maxBulletRange;
    if (currentEligible) {
        BenchmarkProfiler::instance().addCounter(BenchmarkCounter::WeaponAimChecks);
        currentEligible = hardpoint->inFieldOfAim(current->position());
    }

    Handle<WorldObject> bestTarget;
    float bestDistance = std::numeric_limits<float>::infinity();
    const auto considerTarget = [&](Handle<WorldObject>& targetHandle) {
        WorldObject* target = targetHandle.get();
        if (!target) {
            return;
        }
        BenchmarkProfiler::instance().addCounter(
            BenchmarkCounter::GunTargetCandidatesConsidered);

        const float distance =
            WorldObjectGeometryHelper::sphereToSphereDistance(m_worldObject, target);
        if (distance >= maxBulletRange) {
            return;
        }
        BenchmarkProfiler::instance().addCounter(BenchmarkCounter::WeaponAimChecks);
        if (!hardpoint->inFieldOfAim(target->position())) {
            return;
        }

        if ((!currentEligible || distance < currentDistance * BetterTargetDistanceRatio) &&
            distance < bestDistance) {
            bestDistance = distance;
            bestTarget = targetHandle;
        }
    };

    if (focusedTarget) {
        considerTarget(*focusedTarget);
    } else if (targets) {
        for (Handle<WorldObject>& target : *targets) {
            considerTarget(target);
        }
    }

    return bestTarget.get()
        ? bestTarget
        : (current ? currentTarget : Handle<WorldObject>());
}

void BoardComputer::shootRockets(WorldObject* target) {
    m_worldObject->components().fireAtObject(target);
}

void BoardComputer::update(float deltaSec) {
    if(m_overwriteEngineState) {
        m_worldObject->components().setEngineState(m_engineState);
    }
    m_engineState.clear();
    m_overwriteEngineState = false;
}
