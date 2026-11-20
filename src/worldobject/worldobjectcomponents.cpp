#include "worldobjectcomponents.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <stdexcept>

#include "equipment/engineslot.h"
#include "equipment/engine.h"
#include "equipment/hardpoint.h"
#include "equipment/shield.h"
#include "equipment/shieldslot.h"
#include "equipment/weapon.h"
#include "equipment/weapons/gun.h"
#include "equipment/weapons/rocketlauncher.h"
#include "worldobject/helper/componentsinfo.h"
#include "worldobject/helper/hardpointaimhelper.h"
#include "worldobject/worldobject.h"
#include "worldobject/worldobjectinfo.h"


WorldObjectComponents::WorldObjectComponents(WorldObject* worldObject):
    m_worldObject(worldObject),
    m_componentsInfo(new ComponentsInfo(this))
{
}

WorldObjectComponents::~WorldObjectComponents() = default;

WorldObject* WorldObjectComponents::worldObject() {
    return m_worldObject;
}

const WorldObject* WorldObjectComponents::worldObject() const {
    return m_worldObject;
}

void WorldObjectComponents::addEngineSlot(std::shared_ptr<EngineSlot> engineSlot) {
    m_engineSlots.push_back(engineSlot);
    engineSlot->addObserver(this);
}

void WorldObjectComponents::removeEngineSlot(EngineSlot* engineSlot) {
    m_engineSlots.remove_if([&](std::shared_ptr<EngineSlot> slot) { return slot.get() == engineSlot; });
    engineSlot->removeObserver(this);
    notifyObservers();
}

std::list<std::shared_ptr<EngineSlot>>& WorldObjectComponents::engineSlots() {
    return m_engineSlots;
}

EnginePower WorldObjectComponents::enginePower() const {
    EnginePower accumulated;
    for (std::shared_ptr<EngineSlot> engineSlot : m_engineSlots) {
        if (engineSlot->engine()) {
            accumulated += engineSlot->engine()->power();
        }
    }
    return accumulated;
}

Acceleration WorldObjectComponents::currentAcceleration() const {
    Acceleration accumulated;
    for (std::shared_ptr<EngineSlot> engineSlot : m_engineSlots) {
        if (engineSlot->engine()) {
            accumulated += engineSlot->engine()->currentAcceleration();
        }
    }
    return accumulated;

}

const EngineState& WorldObjectComponents::engineState() const {
    return m_engineState;
}

void WorldObjectComponents::setEngineState(const EngineState& engineState) {
    m_engineState = engineState;

    for (std::shared_ptr<EngineSlot> engineSlot : m_engineSlots) {
        if (engineSlot->engine()) {
            engineSlot->engine()->setState(engineState);
        }
    }
}

void WorldObjectComponents::addHardpoint(std::shared_ptr<Hardpoint> hardpoint) {
    m_hardpoints.push_back(hardpoint);
    hardpoint->addObserver(this);
}

void WorldObjectComponents::removeHardpoint(Hardpoint* hardpoint) {
    m_hardpoints.remove_if([&](std::shared_ptr<Hardpoint> hp) { return hp.get() == hardpoint; });
    hardpoint->removeObserver(this);
    notifyObservers();
}

std::list<std::shared_ptr<Hardpoint>>& WorldObjectComponents::hardpoints() {
    return m_hardpoints;
}

void WorldObjectComponents::fireAtPoint(const glm::vec3& point, bool checkFriendlyFire) {
    fireGuns(point, nullptr, checkFriendlyFire);
}

void WorldObjectComponents::fireGunsWithLead(WorldObject& target, bool checkFriendlyFire) {
    if (&target != m_worldObject && target.info().canLockOn())
        fireGuns(glm::vec3(0), &target, checkFriendlyFire);
}

void WorldObjectComponents::fireGuns(const glm::vec3& point, WorldObject* leadTarget, bool checkFriendlyFire) {
    if (m_hardpoints.empty()) return;
    // Rotate after spending, not empty attempts, so recharge timing cannot starve the same mount.
    const float energyBefore = m_weaponEnergy;
    auto next = std::next(m_hardpoints.begin(), m_fireOffset % m_hardpoints.size());
    for (std::size_t i = 0; i < m_hardpoints.size(); ++i) {
        const auto hardpoint = *next;
        if (++next == m_hardpoints.end()) next = m_hardpoints.begin();
        if (hardpoint->weapon() && hardpoint->weapon()->type() == WeaponType::Gun) {
            Gun& gun = dynamic_cast<Gun&>(*hardpoint->weapon().get());
            if (leadTarget) {
                HardpointAimHelper aim(hardpoint.get(), leadTarget, true);
                if (aim.isHittable()) gun.fireAtPoint(aim.point(), checkFriendlyFire);
            } else {
                gun.fireAtPoint(point, checkFriendlyFire);
            }
        }
    }
    if (m_weaponEnergy < energyBefore) m_fireOffset = (m_fireOffset+1) % m_hardpoints.size();
}

void WorldObjectComponents::configureWeaponEnergy(float capacity, float regeneration, float delay) {
    if (!std::isfinite(capacity) || !std::isfinite(regeneration) || !std::isfinite(delay) ||
        capacity < 0 || regeneration < 0 || delay < 0 ||
        (capacity > 0 ? regeneration <= 0 : regeneration != 0 || delay != 0))
        throw std::invalid_argument("Invalid weapon energy configuration.");
    m_weaponEnergyCapacity = capacity;
    m_weaponEnergyRegeneration = regeneration;
    m_weaponEnergyDelay = delay;
    m_fireOffset = 0;
    refillWeaponEnergy();
}

float WorldObjectComponents::weaponEnergy() const { return m_weaponEnergy; }
float WorldObjectComponents::weaponEnergyCapacity() const { return m_weaponEnergyCapacity; }
float WorldObjectComponents::weaponEnergyRegeneration() const { return m_weaponEnergyRegeneration; }
float WorldObjectComponents::weaponEnergyRechargeRemaining() const { return m_weaponEnergyRechargeRemaining; }

bool WorldObjectComponents::consumeWeaponEnergy(float amount) {
    if (!std::isfinite(amount) || amount < 0)
        throw std::invalid_argument("Weapon energy cost must be finite and nonnegative.");
    if (m_weaponEnergyCapacity == 0 || amount == 0) return true;
    if (amount > m_weaponEnergy + 0.00001f) return false;
    m_weaponEnergy = std::max(0.0f, m_weaponEnergy - amount);
    m_weaponEnergyRechargeRemaining = m_weaponEnergyDelay;
    return true;
}

void WorldObjectComponents::refillWeaponEnergy() {
    m_weaponEnergy = m_weaponEnergyCapacity;
    m_weaponEnergyRechargeRemaining = 0;
}

void WorldObjectComponents::fireAtObject(WorldObject* worldObject) {
    for (std::shared_ptr<Hardpoint> hardpoint : m_hardpoints) {
        if(!hardpoint->weapon()) {
            continue;
        }
        if (hardpoint->weapon()->type() == WeaponType::RocketLauncher) {
            RocketLauncher& rocketLauncher = dynamic_cast<RocketLauncher&>(*hardpoint->weapon());
            rocketLauncher.fireAtObject(worldObject);
        }
    }
}

void WorldObjectComponents::addShieldSlot(std::shared_ptr<ShieldSlot>& shieldSlot) {
    m_shieldSlots.push_back(shieldSlot);
    notifyObservers();
}

std::list<std::shared_ptr<ShieldSlot>>& WorldObjectComponents::shieldSlots() {
    return m_shieldSlots;
}

float WorldObjectComponents::compensateDamage(float damage) {
    for (std::shared_ptr<ShieldSlot>& shieldSlot : m_shieldSlots) {
        std::shared_ptr<Shield>& shield = shieldSlot->shield();
        if (!shield) {
            continue;
        }

        damage = shield->compensate(damage);
        if (damage == 0) {
            break;
        }
    }

    return damage;
}

void WorldObjectComponents::update(float deltaSec) {
    const float blocked = std::min(deltaSec, m_weaponEnergyRechargeRemaining);
    m_weaponEnergyRechargeRemaining -= blocked;
    m_weaponEnergy = std::min(m_weaponEnergyCapacity,
        m_weaponEnergy + (deltaSec-blocked)*m_weaponEnergyRegeneration);
    for (std::shared_ptr<Hardpoint>& hardpoint : m_hardpoints) {
        hardpoint->update(deltaSec);
    }

    for (std::shared_ptr<EngineSlot>& engineSlot : m_engineSlots) {
        engineSlot->update(deltaSec);
    }

    for (std::shared_ptr<ShieldSlot>& shieldSlot : m_shieldSlots) {
        shieldSlot->update(deltaSec);
    }
}

const ComponentsInfo& WorldObjectComponents::componentsInfo() const {
    return *m_componentsInfo;
}

void WorldObjectComponents::updateObserver() {
    notifyObservers();
}

