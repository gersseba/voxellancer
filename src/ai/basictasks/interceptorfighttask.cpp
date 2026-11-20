#include "interceptorfighttask.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "ai/boardcomputer.h"
#include "equipment/hardpoint.h"
#include "equipment/weapons/gun.h"
#include "physics/physics.h"
#include "utils/safenormalize.h"
#include "voxel/voxelclusterbounds.h"
#include "worldobject/worldobjectcomponents.h"

InterceptorFightTask::InterceptorFightTask(BoardComputer* computer,
    const std::vector<Handle<WorldObject>>& targets) :
    FighterFightTask(computer, targets)
{
}

bool InterceptorFightTask::isFinished() {
    return std::none_of(m_targets.begin(), m_targets.end(),
        [](const auto& target) { return target.valid(); });
}

void InterceptorFightTask::update(float deltaSec) {
    if (updateTargets(30) || !m_primaryTarget.valid()) selectPrimaryTarget();
    auto* owner = boardComputer()->worldObject();
    auto* target = m_primaryTarget.get();
    if (!target || owner->physics().maxMass() <= 0) {
        boardComputer()->setEngineState(EngineState());
        return;
    }

    auto& components = owner->components();
    const auto inverse = glm::inverse(owner->orientation());
    const auto velocity = owner->physics().speed().directional();
    const auto targetVelocity = target->physics().speed().directional();
    const auto offset = target->position()-owner->position();
    const float distance = glm::length(offset);
    const auto direction = safeNormalize(offset, owner->orientation()*glm::vec3(0,0,-1));
    const auto power = components.enginePower();
    const float mass = owner->physics().maxMass();
    const auto thrust = power.directional()/mass;
    const auto angularThrust = power.angular()/mass;
    const float acceleration = std::max(0.1f, std::min({thrust.x,thrust.y,thrust.w}));
    const float cruise = std::min(120.0f, acceleration /
        std::max(0.1f,owner->physics().directionalDampening()));
    const float radii = owner->bounds().minimalGridSphere().radius()*owner->transform().scale() +
        target->bounds().minimalGridSphere().radius()*target->transform().scale();

    float range = std::numeric_limits<float>::max();
    for (const auto& mount : components.hardpoints()) {
        auto* gun = dynamic_cast<Gun*>(mount->weapon().get());
        if (!gun || (components.weaponEnergyCapacity() > 0 &&
                gun->energyCost() > components.weaponEnergyCapacity())) continue;
        const auto& profile = gun->profile();
        range = std::min(range, profile.enabled ? std::min(profile.range,
            gun->bulletSpeed()*gun->bulletLifetime()) : gun->bulletSpeed()*gun->bulletLifetime());
    }
    if (range == std::numeric_limits<float>::max()) range = 300;
    const float preferred = std::max(radii+60.0f,range*0.55f);
    const float closing = std::max(0.0f,glm::dot(velocity-targetVelocity,direction));
    const float safety = radii+30.0f;
    if (distance < safety+closing*closing/(2*acceleration)) m_separating = true;
    if (distance > safety+80 && closing < 5) m_separating = false;
    const auto tangent = safeNormalize(glm::cross(direction,glm::vec3(0,1,0)),
        glm::vec3(1,0,0));
    const float radial = m_separating ? -cruise :
        glm::clamp((distance-preferred)*0.8f,-cruise,cruise);
    const auto desiredVelocity = targetVelocity + direction*radial +
        tangent*(distance < range ? cruise*0.25f : 0);
    const auto requestedAcceleration = (desiredVelocity-velocity)/0.7f +
        desiredVelocity*owner->physics().directionalDampening();
    const auto localAcceleration = inverse*requestedAcceleration;
    glm::vec3 directional(0);
    for (int axis = 0; axis < 3; ++axis) {
        const float available = axis == 2 ? (localAcceleration.z < 0 ? thrust.z : thrust.w)
            : thrust[axis];
        if (available > 0) directional[axis] = glm::clamp(localAcceleration[axis]/available,-1.0f,1.0f);
    }

    const auto rotation = glm::rotation(glm::vec3(0,0,-1),
        inverse*direction);
    const auto desiredAngular = glm::axis(rotation)*std::min(glm::angle(rotation)*2.5f,1.8f);
    const auto angularAcceleration = (desiredAngular-inverse*owner->physics().speed().angular())/0.35f;
    glm::vec3 angular(0);
    for (int axis = 0; axis < 3; ++axis)
        if (angularThrust[axis] > 0)
            angular[axis] = glm::clamp(angularAcceleration[axis]/angularThrust[axis],-1.0f,1.0f);
    boardComputer()->setEngineState(EngineState(directional,angular));

}
