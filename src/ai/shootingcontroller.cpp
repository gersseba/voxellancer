#include "shootingcontroller.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <set>
#include <glm/gtx/quaternion.hpp>

#include "ai/boardcomputer.h"
#include "equipment/hardpoint.h"
#include "equipment/weapons/gun.h"
#include "equipment/shield.h"
#include "equipment/shieldslot.h"
#include "geometry/sphere.h"
#include "property/property.h"
#include "utils/safenormalize.h"
#include "utils/worldobjectgeometryhelper.h"
#include "voxel/specialvoxels/hardpointvoxel.h"
#include "voxel/voxelclusterbounds.h"
#include "world/world.h"
#include "worldobject/helper/hardpointaimhelper.h"
#include "worldobject/worldobject.h"
#include "worldobject/worldobjectcomponents.h"
#include "worldobject/ship.h"

void ShootingSkill::validate() const {
    for (float value : {lead,tracking,accuracy})
        if (!std::isfinite(value) || value < 0 || value > 1)
            throw std::invalid_argument("AI shooting skills must be finite values in [0,1].");
}

ShootingSkill ShootingSkill::defaults() {
    ShootingSkill skill{Property<float>("aiShooting.lead",0.5f),
        Property<float>("aiShooting.tracking",0.5f),
        Property<float>("aiShooting.accuracy",0.5f)};
    skill.validate();
    return skill;
}

ShootingController::ShootingController(BoardComputer* computer, const ShootingSkill& skill) :
    m_computer(computer), m_skill(skill), m_random(0),
    m_statistics(std::make_shared<ShotStatistics>())
{
    if (!computer || !computer->worldObject())
        throw std::invalid_argument("AI shooting requires an owning board computer.");
    if (!computer->worldObject()->world())
        throw std::invalid_argument("AI shooting requires an owner attached to a world.");
    skill.validate();
    const auto entity = computer->worldObject()->world()->entityId(computer->worldObject());
    if (entity) seed(entity->index);
}

void ShootingController::setSkill(const ShootingSkill& skill) {
    skill.validate();
    m_skill = skill;
}

void ShootingController::seed(std::uint32_t value) {
    m_random.seed(value);
    m_normal.reset();
    m_seeded = true;
}

std::optional<glm::vec3> ShootingController::aimPoint(const Hardpoint* mount) const {
    const auto found = std::find_if(m_aims.begin(),m_aims.end(),
        [mount](const auto& aim) { return aim.mount == mount && aim.target.valid(); });
    return found == m_aims.end() ? std::nullopt : std::optional<glm::vec3>(found->point);
}

void ShootingController::update(float deltaSec, std::vector<Handle<WorldObject>>& targets,
    WorldObject* preferredTarget, bool fire) {
    if (!std::isfinite(deltaSec) || deltaSec < 0)
        throw std::invalid_argument("AI shooting update duration must be finite and nonnegative.");
    auto* owner = m_computer->worldObject();
    if (!m_seeded) {
        // Scripts can attach tasks before the ship receives its entity ID.
        const auto entity = owner->world()->entityId(owner);
        if (entity) seed(entity->index);
    }
    auto& components = owner->components();
    auto& mounts = components.hardpoints();
    m_aims.erase(std::remove_if(m_aims.begin(),m_aims.end(),[&](const auto& aim) {
        return std::none_of(mounts.begin(),mounts.end(),[&](const auto& mount) {
            return mount.get() == aim.mount && mount->weapon().get() == aim.weapon;
        });
    }),m_aims.end());
    float maximumCost = 0;
    for (const auto& mount : mounts)
        if (auto* gun = dynamic_cast<Gun*>(mount->weapon().get()))
            if (components.weaponEnergyCapacity() == 0 ||
                gun->energyCost() <= components.weaponEnergyCapacity())
                maximumCost = std::max(maximumCost,gun->energyCost());
    if (components.weaponEnergyCapacity() > 0) {
        if (components.weaponEnergy() < maximumCost) m_recharging = true;
        if (components.weaponEnergy() >= std::max(maximumCost,
            components.weaponEnergyCapacity()*0.6f)) m_recharging = false;
    } else m_recharging = false;

    for (const auto& mount : mounts) {
        auto* gun = dynamic_cast<Gun*>(mount->weapon().get());
        if (!gun || (components.weaponEnergyCapacity() > 0 &&
            gun->energyCost() > components.weaponEnergyCapacity())) continue;
        auto found = std::find_if(m_aims.begin(),m_aims.end(),
            [&](const auto& aim) { return aim.mount == mount.get(); });
        if (found == m_aims.end()) {
            m_aims.push_back({mount.get(),gun,{},owner->orientation()*mount->direction(),{}});
            found = m_aims.end()-1;
        }
        auto& aim = *found;
        auto* previousTarget = aim.target.get();
        Handle<WorldObject> preferred = preferredTarget ? preferredTarget->handle<WorldObject>() :
            Handle<WorldObject>();
        if (preferred.valid() && std::any_of(targets.begin(),targets.end(),
                [&](const auto& target) { return target.get() == preferredTarget; }) &&
            mount->inFieldOfAim(preferredTarget->position())) {
            aim.target = preferred;
        } else {
            aim.target = m_computer->selectGunTarget(mount.get(),&targets,nullptr,aim.target);
        }
        auto* target = aim.target.get();
        if (!target || std::none_of(targets.begin(),targets.end(),
            [target](const auto& handle) { return handle.get() == target; })) {
            aim.target = {};
            continue;
        }
        if (previousTarget != target) aim.voxelCount = 0;
        HardpointAimHelper helper(mount.get(),target);
        const bool reachable = helper.isHittable();
        glm::vec3 desiredPoint = target->position();
        if (reachable) desiredPoint = glm::mix(desiredPoint,helper.point(),m_skill.lead);
        const auto origin = mount->voxel()->position();
        const auto desiredDirection = safeNormalize(desiredPoint-origin,aim.direction);
        if (m_skill.tracking == 1) aim.direction = desiredDirection;
        else {
            const float angle = std::acos(glm::clamp(glm::dot(aim.direction,desiredDirection),-1.0f,1.0f));
            const float step = glm::radians(5.0f+355.0f*m_skill.tracking*m_skill.tracking)*deltaSec;
            if (angle <= step) aim.direction = desiredDirection;
            else {
                const auto rotation = glm::rotation(aim.direction,desiredDirection);
                aim.direction = safeNormalize(glm::angleAxis(step,glm::axis(rotation))*aim.direction,
                    desiredDirection);
            }
        }
        const float distance = glm::distance(origin,desiredPoint);
        aim.point = origin+aim.direction*distance;
        const float range = gun->profile().enabled ? std::min(gun->profile().range,
            gun->bulletSpeed()*gun->bulletLifetime()) : gun->bulletSpeed()*gun->bulletLifetime();
        if (!fire || m_recharging || !gun->canFire() ||
            (WorldObjectGeometryHelper::sphereToSphereDistance(owner,target) >= range && !reachable) ||
            (m_skill.lead == 1 && !reachable)) continue;
        glm::vec3 shotPoint = aim.point;
        if (m_skill.accuracy < 1) {
            // An isotropic Gaussian has P(offset < radius) = 1-exp(-radius^2/(2*sigma^2)).
            const float probability = 0.1f+0.9f*m_skill.accuracy;
            float radius = 0;
            auto* ship = dynamic_cast<Ship*>(target);
            const bool sphereShield = ship && ship->shieldType() == ShieldType::Sphere &&
                std::any_of(target->components().shieldSlots().begin(),
                    target->components().shieldSlots().end(),[](const auto& slot) {
                        return slot->shield() && !slot->shield()->depleted();
                    });
            if (sphereShield) radius = target->bounds().sphere(target->transform()).radius();
            else {
                if (aim.voxelCount != target->voxelCount()) {
                    std::array<std::set<std::pair<int,int>>,3> projections;
                    for (const auto& entry : target->voxelMap()) {
                        const auto cell = entry.first;
                        projections[0].emplace(cell.y,cell.z);
                        projections[1].emplace(cell.x,cell.z);
                        projections[2].emplace(cell.x,cell.y);
                    }
                    aim.projectedAreas = glm::vec3(projections[0].size(),projections[1].size(),
                        projections[2].size());
                    aim.voxelCount = target->voxelCount();
                }
                // Blend axial silhouette areas for a continuous view-dependent size estimate.
                const auto localDirection = glm::abs(glm::inverse(target->orientation())*desiredDirection);
                const float area = glm::dot(aim.projectedAreas,localDirection);
                // Empirical calibration for the non-circular voxel silhouettes.
                radius = std::sqrt(area/glm::pi<float>())*target->transform().scale()*0.88f;
            }
            const float sigma = radius/std::sqrt(-2*std::log(1-probability));
            const auto right = safeNormalize(glm::cross(aim.direction,glm::vec3(0,1,0)),
                glm::vec3(1,0,0));
            const auto up = glm::cross(right,aim.direction);
            const float x = m_normal(m_random), y = m_normal(m_random);
            shotPoint += (right*x+up*y)*sigma;
        }
        auto record = std::make_shared<ShotRecord>();
        record->statistics = m_statistics;
        record->target = aim.target;
        gun->fireAtPoint(shotPoint,true,record);
    }
}
