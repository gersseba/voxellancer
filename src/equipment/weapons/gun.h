#pragma once

#include <glm/glm.hpp>
#include <memory>

#include "equipment/weapon.h"
#include "equipment/weaponcatalog.h"

#include "sound/soundproperties.h"


class Bullet;
class Ray;
class WorldTreeQuery;
class Faction;
class Ship;
struct ShotRecord;

class Gun: public Weapon {
public:
    Gun(const std::string& equipmentKey);
    ~Gun();

    float bulletSpeed() const;
    void setBulletSpeed(float bulletSpeed);

    float bulletLifetime() const;
    float bulletSpawnOffset() const;
    const WeaponProfile& profile() const { return m_profile; }

    const SoundProperties& fireSound() const;
    void setFireSound(const SoundProperties& fireSound);

    virtual void fireAtPoint(const glm::vec3& point, bool checkFriendlyFire,
        const std::shared_ptr<ShotRecord>& record = {});
    virtual bool isBulletPathClear(const glm::vec3& point, bool checkFriendlyFire);

    virtual void update(float deltaSec) override;


protected:
    float m_bulletSpeed;
    WeaponProfile m_profile;
    Visuals m_visuals;
    SoundProperties m_fireSound;

    WorldObject* m_owner;

    float m_spawnDistance;
    float m_bulletLength;
    float m_bulletMaxWidth;
    float m_bulletLifetime;


    void setupBullet(Bullet* bullet, const glm::vec3& point);
    void setBulletExtend();
    virtual void onProjectileNameChanged() override;
};

