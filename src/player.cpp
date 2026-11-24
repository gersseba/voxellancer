#include "player.h"

#include "ai/character.h"

#include "camera/cameradolly.h"
#include "camera/camerahead.h"

#include "factions/factionmatrix.h"

#include "gamestate/game.h"
#include "gamestate/gameplay/gameplay.h"

#include "ui/hud/hud.h"
#include "ui/hud/hudget.h"
#include "ui/hud/aimhelperhudget.h"
#include "ui/hud/crosshair.h"
#include "ui/targetselector.h"

#include "utils/aimer.h"
#include "utils/safenormalize.h"

#include "physics/physics.h"

#include "world/world.h"

#include "worldobject/ship.h"
#include "worldobject/worldobjectcomponents.h"
#include "worldobject/worldobjectinfo.h"

#include "equipment/hardpoint.h"
#include "equipment/weapon.h"
#include "equipment/weapons/gun.h"


Player::Player():
    m_aimer(new Aimer(nullptr)),
    m_hud(new HUD(this)),
    m_cameraDolly(new CameraDolly()),
    m_targetSelector(new TargetSelector(this))
{

}

Player::~Player() = default;

Ship* Player::ship() {
    return m_ship.get();
}

void Player::setShip(Ship* ship) {
    m_ship = makeHandle(ship);
    m_ship->character()->setFaction(World::instance()->factionMatrix().playerFaction());
    m_ship->info().setShowOnHud(false);
    m_cameraDolly->followWorldObject(ship);
    m_aimer->setWorldObject(ship);
}

void Player::update(float deltaSec) {
    m_cameraDolly->update(deltaSec);
    m_hud->update(deltaSec);

    if (m_ship.valid()) {
        m_ship->components().setEngineState(m_engineState);
    }
}

CameraHead& Player::cameraHead() {
    return m_cameraDolly->cameraHead();
}

HUD& Player::hud() {
    return *m_hud;
}

void Player::fire() {
    if (ship()) {
        auto* target = m_ship->targetObject();
        const Ray crosshair = Ray::fromTo(cameraHead().position(), m_hud->crossHair().worldPosition());
        const auto aim = m_aimer->aim(crosshair);
        if (!aim.hitVoxel && target && Property<bool>("vfx.drawhud") &&
            m_hud->aimHelper().isLinedUp(crosshair, *target))
            m_ship->components().fireGunsWithLead(*target, false);
        else
            m_ship->components().fireGunsAtCrosshair(aim.point, false);
    }
}

glm::vec3 Player::aimPoint() {
    const glm::vec3 direction = safeNormalize(
        m_hud->crossHair().worldPosition() - cameraHead().position(),
        cameraHead().orientation() * glm::vec3(0.0f, 0.0f, -1.0f));
    return m_aimer->aim(Ray(cameraHead().position(), direction)).point;
}

void Player::move(const glm::vec3& vec) {
    m_engineState.setDirectional(vec);
}

void Player::rotate(const glm::vec3& euler) {
    m_engineState.setAngular(euler);
}

void Player::selectTarget(bool next) {
    m_targetSelector->selectTarget(next);
}

void Player::setTarget(WorldObject* target) {
    m_ship->setTargetObject(target);
    m_hud->setTarget(target);
}

