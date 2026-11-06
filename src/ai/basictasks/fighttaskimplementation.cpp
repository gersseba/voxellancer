#include "fighttaskimplementation.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unordered_set>

#include "ai/boardcomputer.h"
#include "ai/squad.h"
#include "ai/squadlogic.h"
#include "benchmark/benchmarkprofiler.h"
#include "collision/collisionfilter.h"
#include "geometry/sphere.h"
#include "utils/staggeredphase.h"
#include "utils/worldobjectgeometryhelper.h"
#include "world/world.h"
#include "worldobject/worldobject.h"
#include "worldobject/worldobjectcomponents.h"
#include "worldobject/helper/componentsinfo.h"
#include "worldtree/worldtree.h"
#include "worldtree/worldtreegeode.h"
#include "worldtree/worldtreenode.h"
#include "worldtree/worldtreequery.h"

namespace {
    constexpr float InitialTargetSearchRadius = 250.0f;
}

FightTaskImplementation::FightTaskImplementation(BoardComputer* boardComputer, const std::vector<Handle<WorldObject>>& targets) :
    AiTask(boardComputer),
    m_targets(targets),
    m_nextTargetSelectionTick(0),
    m_targetSelectionInitialized(false),
    m_targetSelectionKey(StaggeredPhase::keyForPosition(
        boardComputer->worldObject()->transform().position())),
    m_targetSelectionCycle(0)
{
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
        setPrimaryTarget(Handle<WorldObject>());
    }
}

WorldObject* FightTaskImplementation::primaryTarget() {
    return m_primaryTarget.get();
}

Handle<WorldObject> FightTaskImplementation::nearestTarget(
    std::optional<ShipType> preferredType,
    float maxDistance)
{
    WorldObject* owner = boardComputer()->worldObject();
    World* world = owner ? owner->world() : nullptr;
    if (world == nullptr) {
        throw std::logic_error("Fight task owner is not attached to a world.");
    }
    if (m_targets.empty() || maxDistance < 0.0f) {
        return Handle<WorldObject>();
    }

    std::unordered_set<const WorldObject*> targetSet;
    targetSet.reserve(m_targets.size());
    for (const Handle<WorldObject>& target : m_targets) {
        if (const WorldObject* object = target.get()) {
            targetSet.insert(object);
        }
    }
    if (targetSet.empty()) {
        return Handle<WorldObject>();
    }

    const IAABB& worldBounds = world->worldTree().root()->aabb();
    const glm::vec3 lower(worldBounds.llf());
    const glm::vec3 upper(worldBounds.urb());
    const glm::vec3 position = owner->transform().position();
    float worldRadius = 0.0f;
    for (int corner = 0; corner < 8; ++corner) {
        const glm::vec3 point(
            (corner & 1) ? upper.x : lower.x,
            (corner & 2) ? upper.y : lower.y,
            (corner & 4) ? upper.z : lower.z);
        worldRadius = std::max(worldRadius, glm::length(point - position));
    }

    const float searchLimit = std::min(maxDistance, worldRadius);
    if (searchLimit <= 0.0f) {
        return Handle<WorldObject>();
    }

    CollisionFilter shipFilter(owner, 0);
    shipFilter.setCollideableWith(WorldObjectType::Ship, true);

    float radius = std::min(InitialTargetSearchRadius, searchLimit);
    while (true) {
        const Sphere searchSphere(position, radius);
        const std::unordered_set<WorldTreeGeode*> nearby =
            WorldTreeQuery(
                &world->worldTree(),
                &searchSphere,
                WorldTreeHint(),
                &shipFilter).nearGeodes();

        Handle<WorldObject> nearest;
        float nearestDistance = std::numeric_limits<float>::infinity();
        for (WorldTreeGeode* geode : nearby) {
            WorldObject* target = geode->worldObject();
            if (targetSet.find(target) == targetSet.end()) {
                continue;
            }
            Ship* targetShip = dynamic_cast<Ship*>(target);
            if (targetShip == nullptr ||
                (preferredType && targetShip->shipType() != *preferredType)) {
                continue;
            }
            const float distance =
                WorldObjectGeometryHelper::sphereToSphereDistance(owner, target);
            if (distance > radius || distance > maxDistance) {
                continue;
            }
            BenchmarkProfiler::instance().addCounter(
                BenchmarkCounter::PrimaryTargetCandidatesConsidered);
            if (distance < nearestDistance) {
                nearestDistance = distance;
                nearest = target->handle<WorldObject>();
            }
        }
        if (nearest.get()) {
            return nearest;
        }
        if (radius >= searchLimit) {
            break;
        }
        radius = std::min(radius * 2.0f, searchLimit);
    }
    return Handle<WorldObject>();
}

bool FightTaskImplementation::useSharedSquadTarget() {
    Ship* ship = dynamic_cast<Ship*>(boardComputer()->worldObject());
    if (ship == nullptr || !ship->squadLogic()->inSquad()) {
        return false;
    }

    Squad* squad = ship->squadLogic()->squad().get();
    if (ship->squadLogic()->isSquadLeader()) {
        return false;
    }
    Ship* leader = squad->leader();
    if (leader && leader->shipType() != ship->shipType()) {
        return false;
    }

    WorldObject* sharedTarget = squad->sharedPrimaryTarget();
    if (sharedTarget && std::any_of(
            m_targets.begin(), m_targets.end(),
            [sharedTarget](const Handle<WorldObject>& target) {
                return target.get() == sharedTarget;
            })) {
        m_primaryTarget = sharedTarget->handle<WorldObject>();
    } else {
        m_primaryTarget = Handle<WorldObject>();
    }
    return true;
}

void FightTaskImplementation::setPrimaryTarget(
    const Handle<WorldObject>& target)
{
    m_primaryTarget = target;
    Ship* ship = dynamic_cast<Ship*>(boardComputer()->worldObject());
    if (ship && ship->squadLogic()->inSquad() &&
        ship->squadLogic()->isSquadLeader()) {
        ship->squadLogic()->squad()->setSharedPrimaryTarget(target);
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
        setPrimaryTarget(Handle<WorldObject>());
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
