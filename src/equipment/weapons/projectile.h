#pragma once

#include <cstdint>

#include "sound/soundproperties.h"

#include "worldobject/worldobject.h"
#include "equipment/weaponcatalog.h"
#include "equipment/weapons/shotstatistics.h"


class SoundProperties;

/**
 * Base class for everything shot by a weapon, characterised by
 *  - doesn't collide with creator
 *  - destroyed after lifetime is over
 */
class Projectile: public WorldObject {
public:
    Projectile();

    WorldObject* creator();
    void setCreator(WorldObject* creator);

    float lifetime() const;
    void setLifetime(float lifetime);

    const SoundProperties& hitSound() const;
    void setHitSound(const SoundProperties& hitSound);
    virtual std::uint32_t impactColor() const;

    virtual void update(float deltaSec) override;

    virtual void onCollision() override;
    void onShieldImpact();
    WeaponProfile profile;
    float impactBudget = 1.0f;
    bool impactCalibrated = false;
    float distanceTraveled = 0;
    glm::vec3 launchVelocity{0};
    std::shared_ptr<ShotRecord> shotRecord;
    virtual void onSpawnFail() override;


protected:
    WorldObject* m_creator;
    float m_lifetime;
    SoundProperties m_hitSound;
    float m_emissiveness;

    virtual void onLifetimeOver();
    virtual void spawnExplosion() = 0;
};
