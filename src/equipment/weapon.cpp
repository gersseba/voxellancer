#include "weapon.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "utils/geometryhelper.h"

#include "voxel/specialvoxels/hardpointvoxel.h"

#include "equipment/hardpoint.h"
#include "equipment/weapons/projectile.h"
#include "world/world.h"
#include "world/worldlogic.h"
#include "worldobject/worldobject.h"
#include "worldobject/worldobjectcomponents.h"


Weapon::Weapon(WeaponType type, const std::string& equipmentKey):
    Equipment(equipmentKey),
    m_type(type),
    m_cooldown(0.0f),
    m_cooldownTime(0.0f),
    m_hardpoint(nullptr)
{
}

Hardpoint* Weapon::hardpoint() {
    return m_hardpoint;
}

void Weapon::setHardpoint(Hardpoint* hardpoint) {
    m_hardpoint = hardpoint;
}

float Weapon::cooldownTime() const {
    return m_cooldownTime;
}

void Weapon::setCooldownTime(float cooldownTime) {
    m_cooldownTime = cooldownTime;
}

const Visuals& Weapon::visuals() const {
    return m_visuals;
}

void Weapon::setVisuals(const Visuals& visuals) {
    m_visuals = visuals;
}

const std::string Weapon::projectileName() const {
    return m_projectileName;
}

WeaponType Weapon::type() const {
    return m_type;
}

void Weapon::update(float deltaSec) {
    m_cooldown = std::max(m_cooldown - deltaSec, -deltaSec);
}

bool Weapon::canFire() {
    if (m_cooldown > 0) return false;
    if (!m_hardpoint || !m_hardpoint->components()) return true;
    const auto& components = *m_hardpoint->components();
    return components.weaponEnergyCapacity() == 0 ||
        components.weaponEnergy() + 0.00001f >= m_energyCost;
}

float Weapon::energyCost() const { return m_energyCost; }

void Weapon::setEnergyCost(float cost) {
    if (!std::isfinite(cost) || cost < 0)
        throw std::invalid_argument("Weapon energy cost must be finite and nonnegative.");
    m_energyCost = cost;
}

void Weapon::onFired() {
    WorldObject* owner = m_hardpoint && m_hardpoint->components()
        ? m_hardpoint->components()->worldObject()
        : nullptr;
    World* world = owner ? owner->world() : nullptr;
    if (owner && !owner->components().consumeWeaponEnergy(m_energyCost))
        throw std::logic_error("Weapon fired without its required energy.");
    m_cooldown += cooldownTime();
    if (world != nullptr && m_hardpoint->voxel() != nullptr) {
        world->worldLogic().recordWeaponFired(
            owner,
            m_hardpoint->group(),
            m_hardpoint->voxel()->cell());
    }
}

void Weapon::onProjectileNameChanged() {

}

void Weapon::setProjectileName(const std::string& name) {
    m_projectileName = name;
    onProjectileNameChanged();
}
