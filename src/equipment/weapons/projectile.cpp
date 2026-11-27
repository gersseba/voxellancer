#include "projectile.h"

#include <stdexcept>

#include "collision/collisionfilterignoringcreator.h"

#ifndef VOXELLANCER_HEADLESS_SERVER
#include "sound/soundmanager.h"
#endif

#include "world/god.h"
#include "world/world.h"
#include "world/worldlogic.h"
#include "physics/physics.h"
#include "utils/safenormalize.h"

#include "worldobject/worldobjectinfo.h"


Projectile::Projectile():
    m_creator(nullptr),
    m_lifetime(0.0f)
{
    m_info->setShowOnHud(false);
    m_info->setCanLockOn(false);
}

WorldObject* Projectile::creator() {
    return m_creator;
}

void Projectile::setCreator(WorldObject* creator) {
    m_creator = creator;

    CollisionFilterIgnoringCreator* newCollisionFilter = new CollisionFilterIgnoringCreator(
        this,
        m_creator,
        collisionFilter().collisionMask()
    );

    setCollisionFilter(newCollisionFilter);
}

float Projectile::lifetime() const {
    return m_lifetime;
}

void Projectile::setLifetime(float lifetime) {
    m_lifetime = lifetime;
}

const SoundProperties& Projectile::hitSound() const {
    return m_hitSound;
}

void Projectile::setHitSound(const SoundProperties& hitSound) {
    m_hitSound = hitSound;
}

std::uint32_t Projectile::impactColor() const {
    return 0xff0000;
}

void Projectile::update(float deltaSec) {
    WorldObject::update(deltaSec);

    m_lifetime -= deltaSec;

    if (m_lifetime <= 0.0f) {
        World* owningWorld = world();
        if (owningWorld == nullptr) {
            throw std::logic_error("Projectile is not attached to a world.");
        }
        owningWorld->god().scheduleRemoval(this);
        onLifetimeOver();
    }
}

void Projectile::onLifetimeOver() {

}

void Projectile::onCollision() {
    World* owningWorld = world();
    if (owningWorld == nullptr) {
        throw std::logic_error("Projectile is not attached to a world.");
    }
    owningWorld->worldLogic().recordProjectileImpact(*this,position(),
        safeNormalize(-physics().speed().directional(),glm::vec3(0,1,0)),1);
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (owningWorld->hasPresentation()) {
        SoundManager::current()->play(hitSound(), position());
    }
#endif

    owningWorld->god().scheduleRemoval(this);
    spawnExplosion();
}

void Projectile::onShieldImpact() {
    World* owningWorld = world();
    if (owningWorld == nullptr) {
        throw std::logic_error("Projectile is not attached to a world.");
    }
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (owningWorld->hasPresentation()) {
        SoundManager::current()->play(hitSound(), position());
    }
#endif

    owningWorld->god().scheduleRemoval(this);
}

void Projectile::onSpawnFail() {
    spawnExplosion();
}
