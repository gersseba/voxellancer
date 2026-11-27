#include "weaponrangescenario.h"

#include <algorithm>
#include <array>
#include <memory>
#include <stdexcept>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ai/character.h"
#include "equipment/hardpoint.h"
#include "equipment/weapon.h"
#include "equipment/weaponcatalog.h"
#include "factions/factionmatrix.h"
#include "factions/factionrelation.h"
#include "resource/weaponbuilder.h"
#include "resource/worldobjectbuilder.h"
#include "voxel/specialvoxels/hardpointvoxel.h"
#include "world/god.h"
#include "world/world.h"
#include "worldobject/ship.h"
#include "worldobject/worldobjectcomponents.h"
#include "worldobject/worldobjectinfo.h"

#ifndef VOXELLANCER_HEADLESS_SERVER
#include "player.h"
#include "voxeleffect/voxelparticleengine.h"
#endif

namespace {
    struct LegacyGun {
        const char* key;
        const char* name;
    };
    const std::array<LegacyGun, 4> LegacyGuns{{
        {"gun", "Legacy Gun"}, {"ioncannon", "Ion Cannon"},
        {"railgun", "Legacy Railgun"}, {"snowcanon", "Snow Cannon"}
    }};
}

WeaponRangeScenario::WeaponRangeScenario(World& world) : BaseScenario(world) {
}

void WeaponRangeScenario::populateWorld() {
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (m_world->hasPresentation()) m_world->particleEngine().clear();
#endif
    auto& factions = m_world->factionMatrix();
    factions.getRelationToPlayer(factions.pirateFaction()).setFriendliness(-100);

    auto* target = worldObjectBuilder("c306").buildShip();
    target->transform().setPosition(glm::vec3(0, 0, -300));
    target->transform().setOrientation(
        glm::angleAxis(glm::half_pi<float>(), glm::vec3(0, 1, 0)));
    target->info().setName("C306 practice target");
    target->info().setShowOnHud(true);
    target->info().setCanLockOn(true);
    target->character()->setFaction(factions.pirateFaction());
    for (const auto& mount : target->components().hardpoints()) mount->setWeapon(nullptr);
    m_target = target->handle<Ship>();
    m_world->god().scheduleSpawn(target);

    auto* player = worldObjectBuilder("contractship").buildShip();
    player->info().setName("Weapon range fighter");
    player->info().setInvulnerable(true);
    player->character()->setFaction(factions.playerFaction());
    player->components().configureWeaponEnergy(0, 0, 0);
    for (const auto& mount : player->components().hardpoints()) mount->setWeapon(nullptr);
    m_player = player->handle<Ship>();
    m_world->god().scheduleSpawn(player);
    selectGun(0);
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (m_world->hasPresentation()) {
        m_world->player().setShip(player);
        m_world->player().setTarget(nullptr);
    }
#endif
}

void WeaponRangeScenario::selectGun(std::size_t index) {
    if (index >= GunCount) throw std::out_of_range("Unknown weapon range gun.");
    auto* player = m_player.get();
    if (!player) throw std::logic_error("Weapon range player ship is unavailable.");
    const std::string key = gunKey(index);
    const std::string name = gunName(index);
    for (const auto& mount : player->components().hardpoints()) {
        if (mount->voxel()->cell() == glm::ivec3(3, 2, 6)) {
            mount->setMountable(key, true);
            mount->setWeapon(std::shared_ptr<Weapon>(WeaponBuilder(key).build()));
            m_selectedGun = index;
            m_selectedName = name;
            m_noticeRemaining = 3.0f;
            return;
        }
    }
    throw std::logic_error("Weapon range fighter is missing its test hardpoint.");
}

std::string WeaponRangeScenario::gunKey(std::size_t index) {
    if (index >= GunCount) throw std::out_of_range("Unknown weapon range gun.");
    if (index < WeaponCatalogSize) {
        return catalogWeapon(static_cast<std::uint8_t>(index + 1)).key;
    }
    return LegacyGuns[index - WeaponCatalogSize].key;
}

std::string WeaponRangeScenario::gunName(std::size_t index) {
    if (index >= GunCount) throw std::out_of_range("Unknown weapon range gun.");
    if (index < WeaponCatalogSize)
        return catalogWeapon(static_cast<std::uint8_t>(index + 1)).name;
    return LegacyGuns[index - WeaponCatalogSize].name;
}

void WeaponRangeScenario::selectNumber(unsigned number) {
    if (number < 1 || number > 9) throw std::out_of_range("Unknown weapon range key.");
    selectGun(number == 9 && m_selectedGun == 8 ? 9 : number - 1);
}

void WeaponRangeScenario::update(float deltaSec) {
    m_noticeRemaining = std::max(0.0f, m_noticeRemaining - deltaSec);
}
