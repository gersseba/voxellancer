#include "bullet.h"

#include "collision/collisionfilter.h"

#include "physics/physics.h"

#include "sound/soundmanager.h"

#include "voxeleffect/voxelexplosiongenerator.h"
#ifndef VOXELLANCER_HEADLESS_SERVER
#include "voxeleffect/combateffects.h"
#include "worldobject/worldobjectinfo.h"
#endif

#include "world/god.h"
#include "world/world.h"


Bullet::Bullet() {
    collisionFilter().setCollideableWith(WorldObjectType::Bullet, false);

    m_physics->setDirectionalDampening(unnamedProperty(0.0f));
    m_physics->setAngularDampening(unnamedProperty(0.0f));
}

WorldObjectType Bullet::objectType() const {
    return WorldObjectType::Bullet;
}

void Bullet::update(float deltaSec) {
    Projectile::update(deltaSec);
}

bool Bullet::passiveForCollisionDetection() {
    return true;
}

void Bullet::spawnExplosion() {
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (world() && world()->hasPresentation()) {
        if (const auto effect = CombatEffectSettings::gun(info().definitionKey())) {
            world()->combatEffects().impact(*effect,position(),-physics().speed().directional());
            return;
        }
    }
#endif
    VoxelExplosionGenerator generator(this);

    generator.setPosition(m_transform.position());
    generator.setRadius(m_transform.scale());
    generator.setScale(m_transform.scale() / 2.0f);
    generator.setCount(16);
    generator.setEmissiveness(0.4f);
    generator.setColor(0xFF0000);
    generator.setForce(0.6f);
    generator.setLifetime(0.7f, 0.2f);

    generator.spawn();
}

