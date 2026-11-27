#pragma once

#include <list>
#include <memory>

#include "equipment/enginepower.h"
#include "equipment/enginestate.h"

#include "worldobjectinfo.h"

#include "utils/observer.h"
#include "utils/observable.h"


class EngineSlot;
class Hardpoint;
class HardpointVoxel;
class ShieldSlot;
class WorldObject;
class ComponentsInfo;

/**
 * Module of the WorldObject that is responsible for managing all
 * the components of a WorldObject (Engines, Weapons) and their respective
 * slots.
 * Also provides functions to trigger actions or retrieve values from
 * a whole category of components. (like, fire all weapons, set all engines)
 */
class WorldObjectComponents : public Observable, public Observer {
public:
    WorldObjectComponents(WorldObject* worldObject);
    ~WorldObjectComponents();

    WorldObject* worldObject();
    const WorldObject* worldObject() const;

    void addEngineSlot(std::shared_ptr<EngineSlot> engineSlot);
    void removeEngineSlot(EngineSlot* engineSlot);

    /**
     * Access EngineSlots either by index in the model or all of them
     * at a time
     */
    std::shared_ptr<EngineSlot> engineSlot(int index);
    std::list<std::shared_ptr<EngineSlot>>& engineSlots();

    /**
     * Poll all installed engines for accumulated power
     */
    EnginePower enginePower() const;

    /**
     * Acceleration of all engines accumulated;
     */
    Acceleration currentAcceleration() const;

    /**
     * Set relative state on all installed engines,
     * the value returned by engineState() is the last set and doesn't react on
     * possible indivdual changes of engines;
     */
    const EngineState& engineState() const;
    void setEngineState(const EngineState& engineState);


    void addHardpoint(std::shared_ptr<Hardpoint> hardpoint);
    void removeHardpoint(Hardpoint* hardpoint);

    /**
     * Access Hardpoints either by index in the model or all of them
     * at a time
     */
    std::shared_ptr<Hardpoint> hardpoint(int index);
    std::list<std::shared_ptr<Hardpoint>>& hardpoints();

    /**
     * Fire all installed weapons either at a point or a WorldObject
     * What of both will actually trigger an action depends on the weapons
     * aimType
     */
    void fireAtPoint(const glm::vec3& point, bool checkFriendlyFire);
    void fireGunsAtCrosshair(const glm::vec3& point, bool checkFriendlyFire);
    void fireGunsWithLead(WorldObject& target, bool checkFriendlyFire);

    void fireAtObject(WorldObject* worldObject);
    void configureWeaponEnergy(float capacity, float regeneration, float delay);
    float weaponEnergy() const;
    float weaponEnergyCapacity() const;
    float weaponEnergyRegeneration() const;
    float weaponEnergyRechargeRemaining() const;
    bool consumeWeaponEnergy(float amount);
    float effectiveGunEnergyCost(float amount) const;
    void setWeaponEnergyUpgradeBonuses(float regenerationMultiplier, float shieldedDiscount);
    void refillWeaponEnergy();

    /**
     * Add and query shields
     */
    void addShieldSlot(std::shared_ptr<ShieldSlot>& shieldSlot);

    std::list<std::shared_ptr<ShieldSlot>>& shieldSlots();

    /**
     * Make shield absorb damage. Return damage that couldn't be absorbed
     * and thus is to be applied to the voxels.
     */
    float compensateDamage(float damage);
    void noteShipHit(float tickDuration);


    /**
     * Update all components
     */
    void update(float deltaSec);

    const ComponentsInfo& componentsInfo() const;


protected:
    void fireGuns(const glm::vec3& point, WorldObject* leadTarget, bool checkFriendlyFire,
        bool compensateVelocity = false);
    WorldObject* m_worldObject;

    std::list<std::shared_ptr<EngineSlot>> m_engineSlots;
    std::list<std::shared_ptr<Hardpoint>> m_hardpoints;
    std::list<std::shared_ptr<ShieldSlot>> m_shieldSlots;

    std::unique_ptr<ComponentsInfo> m_componentsInfo;

    EngineState m_engineState;
    float m_weaponEnergy = 0, m_weaponEnergyCapacity = 0, m_weaponEnergyRegeneration = 0;
    float m_weaponEnergyDelay = 0, m_weaponEnergyRechargeRemaining = 0;
    float m_energyRegenerationMultiplier = 1, m_shieldedEnergyDiscount = 0;
    std::size_t m_fireOffset = 0;


    virtual void updateObserver();
};

