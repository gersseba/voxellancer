#include "gun.h"

#include <stdexcept>

#include "benchmark/benchmarkprofiler.h"

#include "utils/geometryhelper.h"
#include "utils/safenormalize.h"

#include "bullet.h"

#include "equipment/hardpoint.h"

#include "physics/physics.h"

#include "resource/worldobjectbuilder.h"

#ifndef VOXELLANCER_HEADLESS_SERVER
#include "sound/soundmanager.h"
#endif

#include "voxel/voxelclusterbounds.h"
#include "voxel/specialvoxels/hardpointvoxel.h"

#include "worldobject/worldobjectcomponents.h"
#include "worldobject/ship.h"

#include "collision/collisiondetector.h"

#include "geometry/capsule.h"

#include "factions/faction.h"
#include "factions/factionmatrix.h"
#include "factions/factionrelation.h"

#include "ai/character.h"

#include "worldtree/worldtreequery.h"
#include "worldtree/worldtreegeode.h"

#include "world/world.h"
#include "world/god.h"

#include "resource/clustercache.h"


Gun::Gun(const std::string& equipmentKey):
    Weapon(WeaponType::Gun, equipmentKey),
    m_bulletSpeed(100)
{
}

Gun::~Gun() = default;

float Gun::bulletSpeed() const {
    return m_bulletSpeed;
}

void Gun::setBulletSpeed(float bulletSpeed) {
    m_bulletSpeed = bulletSpeed;
}

const SoundProperties& Gun::fireSound() const {
    return m_fireSound;
}

void Gun::setFireSound(const SoundProperties& fireSound) {
    m_fireSound = fireSound;
}

void Gun::fireAtPoint(const glm::vec3& point, bool checkFriendlyFire,
    const std::shared_ptr<ShotRecord>& record) {
    if (!canFire()) {
        return;
    }
    WorldObject* owner = m_hardpoint->components()->worldObject();
    World* world = owner ? owner->world() : nullptr;
    if (world == nullptr) {
        throw std::logic_error("Gun owner is not attached to a world.");
    }

    BenchmarkProfiler::instance().addCounter(BenchmarkCounter::WeaponAimChecks);
    if (!m_hardpoint->inFieldOfAim(point)) {
        return;
    }

    if (!isBulletPathClear(point, checkFriendlyFire)) {
        return;
    }

    const auto& profile = m_profile;
    if (record && record->statistics) ++record->statistics->shots;
    for (int i = 0; i < profile.pellets; ++i) {
        Bullet *bullet = WorldObjectBuilder(projectileName(), *world).buildBullet();
        glm::vec3 aim = point;
        if (profile.pellets > 1) {
            const auto forward = safeNormalize(point - hardpoint()->voxel()->position(),
                glm::vec3(0, 0, -1));
            const auto right = safeNormalize(glm::cross(forward, glm::vec3(0, 1, 0)),
                glm::vec3(1, 0, 0));
            const auto up = glm::cross(right, forward);
            const float angle = glm::two_pi<float>() * i / profile.pellets;
            aim += (right * std::cos(angle) + up * std::sin(angle)) *
                glm::distance(point, hardpoint()->voxel()->position()) *
                std::tan(glm::radians(4.0f));
        }
        if (profile.scatter > 0) {
            const auto origin = hardpoint()->voxel()->position();
            const auto cell = hardpoint()->voxel()->cell();
            const std::uint32_t seed = m_scatterSequence++ * 0x9e3779b9U ^
                static_cast<std::uint32_t>(cell.x) * 73856093U ^
                static_cast<std::uint32_t>(cell.y) * 19349663U ^
                static_cast<std::uint32_t>(cell.z) * 83492791U;
            aim = origin + scatterDirection(aim - origin, profile.scatter, seed) *
                glm::distance(origin, aim);
        }
        setupBullet(bullet, aim);
        bullet->shotRecord = record;
        world->god().scheduleSpawn(bullet);
        BenchmarkProfiler::instance().addCounter(BenchmarkCounter::BulletsScheduled);
    }

#ifndef VOXELLANCER_HEADLESS_SERVER
    if (world->hasPresentation()) {
        SoundManager::current()->play(fireSound(), hardpoint()->voxel()->position());
    }
#endif

    onFired();
}

bool Gun::isBulletPathClear(const glm::vec3& point, bool checkFriendlyFire) {
    BenchmarkCpuScope scope(BenchmarkMetric::BulletLineOfFireQuery);
    BenchmarkProfiler::instance().addCounter(BenchmarkCounter::BulletLineOfFireQueries);
    WorldObject* owner = m_hardpoint->components()->worldObject();
    World* world = owner ? owner->world() : nullptr;
    if (world == nullptr) {
        throw std::logic_error("Gun owner is not attached to a world.");
    }

    glm::vec3 direction = glm::normalize(point - m_hardpoint->voxel()->position());
    Capsule capsuleToTarget = Capsule::fromTo(m_hardpoint->voxel()->position() +
        direction * bulletSpawnOffset(),
        point, m_bulletMaxWidth / 2);

    WorldTreeQuery fireDirectionQuery(
        &world->worldTree(),
        &capsuleToTarget,
        owner->collisionDetector().geode()->hint(),
        nullptr);

    const bool hasBlockingObject = fireDirectionQuery.anyIntersectingWorldObject(
        [owner, checkFriendlyFire](WorldObject* object) {
            if (object == owner) {
                return true;
            }

            if (checkFriendlyFire &&
                owner->objectType() == WorldObjectType::Ship &&
                object->objectType() == WorldObjectType::Ship &&
                static_cast<Ship*>(object)->character()->faction().relationTo(static_cast<Ship*>(owner)->character()->faction()).isFriendly())
            {
                return true;
            }

            return false;
        });
    return !hasBlockingObject;
}

void Gun::update(float deltaSec) {
    Weapon::update(deltaSec);
}

void Gun::setupBullet(Bullet* bullet, const glm::vec3& point) {
    Transform bulletTransform(bullet->transform());

    WorldObject* firingWorldObject = m_hardpoint->components()->worldObject();
    bullet->impactBudget *= firingWorldObject->info().weaponDamageMultiplier();

    glm::quat worldObjectOrientation = firingWorldObject->transform().orientation();
    glm::vec3 bulletDirection = safeNormalize(point - m_hardpoint->voxel()->position(), glm::vec3(0, 0, -1));
    glm::vec3 hardpointDirection = worldObjectOrientation * glm::vec3(0, 0, -1);
    glm::vec3 bulletUp = glm::cross(bulletDirection, hardpointDirection);

    bulletTransform.setOrientation(m_hardpoint->components()->worldObject()->transform().orientation());

    if (normalizeable(bulletUp)) {
        glm::vec3 rotationAxis = glm::normalize(bulletUp);
        float angle = GeometryHelper::angleBetween(bulletDirection, hardpointDirection);
        glm::quat bulletOrientation = glm::angleAxis(-angle, rotationAxis);
        bulletTransform.rotateWorld(bulletOrientation); //then rotate towards target
    }

    bulletTransform.setPosition(m_hardpoint->voxel()->position() +
        bulletDirection * bulletSpawnOffset());

    bullet->setTransform(bulletTransform);

    bullet->launchVelocity = firingWorldObject->physics().speed().directional();
    bullet->physics().setSpeed(Speed(
        bulletDirection * bulletSpeed() + (bullet->profile.enabled
            ? bullet->launchVelocity : glm::vec3(0)),
        bulletTransform.orientation() * glm::vec3(0, 0, 5.0f)
    ));

    bullet->setCreator(m_hardpoint->components()->worldObject());
    if (bullet->profile.physical && firingWorldObject->physics().mass() > 0) {
        const auto speed = firingWorldObject->physics().speed();
        firingWorldObject->physics().setSpeed(Speed(
            speed.directional() - bulletDirection * bulletSpeed() *
                bullet->profile.mass / firingWorldObject->physics().mass(),
            speed.angular()));
    }
}

void Gun::setBulletExtend() {
    Bullet* bullet = WorldObjectBuilder(projectileName()).buildBullet();

    m_bulletMaxWidth = glm::max(bullet->bounds().minimalGridAABB().extent(XAxis), bullet->bounds().minimalGridAABB().extent(YAxis))* bullet->transform().scale();
    m_bulletLength = bullet->bounds().minimalGridAABB().extent(ZAxis) * bullet->transform().scale();
    m_spawnDistance = glm::root_two<float>() * bullet->transform().scale();
    m_bulletLifetime = bullet->lifetime();

    delete bullet;
}

void Gun::onProjectileNameChanged() {
    m_profile = WeaponProfile::load(projectileName());
    setBulletExtend();
}

float Gun::bulletLifetime() const {
    return m_bulletLifetime;
}

float Gun::bulletSpawnOffset() const {
    const float clearance = m_profile.enabled ? m_hardpoint->components()->worldObject()->transform().scale() : 0;
    return m_bulletLength / 2.0f + m_spawnDistance + clearance;
}
