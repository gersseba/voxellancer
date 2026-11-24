#include "aimhelperhudget.h"

#include <glm/glm.hpp>

#include "utils/geometryhelper.h"

#include "worldobject/ship.h"

#include "equipment/weapon.h"
#include "equipment/hardpoint.h"

#include "worldobject/worldobjectcomponents.h"
#include "worldobject/helper/hardpointaimhelper.h"

#include "player.h"

#include "hud.h"
#include "aimhelperhudgetvoxels.h"


AimHelperHudget::AimHelperHudget(HUD* hud):
    CircularHudget(hud, 0.25f),
    m_voxels(new AimHelperHudgetVoxels(this)),
    m_lastTargetWorldObject(nullptr),
    m_distanceRange(m_hud->sphere().radius() * 2, m_hud->sphere().radius() * 10)
{

}

AimHelperHudget::~AimHelperHudget() = default;

bool AimHelperHudget::isLinedUp(const Ray& ray, const WorldObject& target) const {
    return m_lastTargetWorldObject == &target && isAt(ray);
}

void AimHelperHudget::update(float deltaSec) {
    Ship* ship = m_hud->player()->ship();

    if (!ship) {
        setVisible(false);
        return;
    }

    WorldObject* targetObject = ship->targetObject();

    if (!targetObject) {
        setVisible(false);
        m_lastTargetWorldObject = nullptr;
        return;
    }

    calculateTargetPoint(targetObject);
    if (!visible()) {
        m_lastTargetWorldObject = nullptr;
        return;
    }

    pointToWorldPoint(m_targetPoint);
    setRelativeDistance(1.0f);

    if (hovered()) {
        setDirectionAngle(directionAngle() + 12.0f * deltaSec);
    } else {
        setDirectionAngle(0.0f);
    }

    m_lastTargetWorldObject = targetObject;
}

void AimHelperHudget::draw() {
    m_voxels->draw();
}

void AimHelperHudget::calculateTargetPoint(WorldObject* targetObject) {
    Ship* ship = m_hud->player()->ship();

    if (!ship) {
        return;
    }

    for (std::shared_ptr<Hardpoint> hardpoint : ship->components().hardpoints()) {
        if (!hardpoint->weapon()) {
            continue;
        }
        if (hardpoint->weapon()->type() != WeaponType::Gun) {
            continue;
        }

        HardpointAimHelper aimHelper(hardpoint.get(), targetObject, true, true);

        if (aimHelper.isHittable() && hardpoint->inFieldOfAim(aimHelper.point())) {
            m_targetPoint = aimHelper.point();
            setVisible(true);
            return;
        }
    }
    setVisible(false);
}

bool AimHelperHudget::isAt(const Ray& ray) const {
    return m_visible && CircularHudget::isAt(ray);
}
