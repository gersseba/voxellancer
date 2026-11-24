#include <bandit/bandit.h>
#include <list>

#include "equipment/shield.h"
#include "equipment/shieldslot.h"
#include "property/propertydirectory.h"
#include "property/propertymanager.h"
#include "scenarios/shieldshowcasescenario.h"
#include "player.h"
#include "voxel/voxel.h"
#include "world/handler/damager.h"
#include "world/helper/damageimpact.h"
#include "world/world.h"
#include "worldobject/worldobjectcomponents.h"
#include "worldobject/worldobjectinfo.h"
#include "worldobject/ship.h"
#include "worldobject/worldobject.h"

namespace bandit {
go_bandit([]() {
    describe("Shield", []() {
        PropertyManager::instance()->reset();
        PropertyManager::instance()->load("data/config.ini");
        PropertyManager::instance()->load("data/voxels.ini", "voxels");

        it("permanently depletes after five registered hits", []() {
            Shield shield("test-shield");
            shield.setMaxHP(100.0f);
            shield.setHP(100.0f);
            shield.setRegeneration(100.0f);

            for (int hit = 0; hit < 4; ++hit) {
                AssertThat(shield.registerHit(), IsTrue());
                AssertThat(shield.depleted(), IsFalse());
            }
            AssertThat(shield.hp(), EqualsWithDelta(20.0f, 0.001f));

            AssertThat(shield.registerHit(), IsTrue());
            AssertThat(shield.depleted(), IsTrue());
            AssertThat(shield.hp(), Equals(0.0f));

            shield.update(10.0f);
            AssertThat(shield.hp(), Equals(0.0f));
            AssertThat(shield.registerHit(), IsFalse());
        });

        it("uses a configurable registered-hit capacity", []() {
            Shield shield("test-shield");
            shield.setMaxHP(100.0f);
            shield.setHP(100.0f);
            shield.setMaxHits(100);

            for (int hit = 0; hit < 99; ++hit) {
                AssertThat(shield.registerHit(), IsTrue());
                AssertThat(shield.depleted(), IsFalse());
            }
            AssertThat(shield.hp(), EqualsWithDelta(1.0f, 0.001f));
            AssertThat(shield.registerHit(), IsTrue());
            AssertThat(shield.depleted(), IsTrue());
            AssertThat(shield.hp(), Equals(0.0f));
        });

        it("tests projectile paths against the selected geometry", []() {
            Shield shield("test-shield");
            shield.setMaxHP(100.0f);
            shield.setHP(100.0f);
            WorldObject protectedObject;
            protectedObject.addVoxel(new Voxel(glm::ivec3(2, 0, 0)));
            glm::ivec3 impactCell(0);
            float hitParameter = 0.0f;

            AssertThat(shield.intersectsSegment(
                protectedObject,
                ShieldType::Hull,
                glm::vec3(-1.0f, 0.0f, 0.0f),
                glm::vec3(0.5f, 0.0f, 0.0f),
                0.1f,
                impactCell,
                hitParameter), IsFalse());
            AssertThat(shield.intersectsSegment(
                protectedObject,
                ShieldType::Hull,
                glm::vec3(0.0f, 0.0f, 0.0f),
                glm::vec3(1.5f, 0.0f, 0.0f),
                0.1f,
                impactCell,
                hitParameter), IsTrue());
            AssertThat(impactCell, Equals(glm::ivec3(2, 0, 0)));

            AssertThat(shield.intersectsSegment(
                protectedObject,
                ShieldType::Sphere,
                glm::vec3(-1.0f, 0.0f, 0.0f),
                glm::vec3(0.5f, 0.0f, 0.0f),
                0.1f,
                impactCell,
                hitParameter), IsFalse());
            AssertThat(shield.intersectsSegment(
                protectedObject,
                ShieldType::Sphere,
                glm::vec3(0.0f, 0.0f, 0.0f),
                glm::vec3(1.5f, 0.0f, 0.0f),
                0.1f,
                impactCell,
                hitParameter), IsTrue());
            AssertThat(shield.intersectsSegment(
                protectedObject,
                ShieldType::None,
                glm::vec3(0.0f, 0.0f, 0.0f),
                glm::vec3(1.5f, 0.0f, 0.0f),
                0.1f,
                impactCell,
                hitParameter), IsFalse());
        });

        it("checks nearby hull cells before scanning the full voxel map", []() {
            Shield shield("test-shield");
            shield.setMaxHP(100.0f);
            shield.setHP(100.0f);
            WorldObject protectedObject;
            // Voxel cells are non-negative; VoxelTree only grows toward +urb.
            for (int x = 0; x <= 8; ++x) {
                for (int y = 0; y <= 8; ++y) {
                    for (int z = 0; z <= 8; ++z) {
                        protectedObject.addVoxel(
                            new Voxel(glm::ivec3(x, y, z)));
                    }
                }
            }

            glm::ivec3 impactCell(0);
            float hitParameter = 0.0f;
            AssertThat(shield.intersectsSegment(
                protectedObject,
                ShieldType::Hull,
                glm::vec3(-10.0f, 4.0f, 4.0f),
                glm::vec3(10.0f, 4.0f, 4.0f),
                0.1f,
                impactCell,
                hitParameter), IsTrue());
            AssertThat(impactCell.x, Equals(0));
        });

        it("absorbs ship collision damage before it reaches hull voxels", []() {
            World world(World::Mode::Server);
            Ship ship(world);
            ship.setShieldType(ShieldType::Sphere);
            ship.addVoxel(new Voxel(glm::ivec3(0, 0, 0)));
            Voxel* voxel = ship.voxel(glm::ivec3(0, 0, 0));
            const float hullHP = voxel->hp();

            std::shared_ptr<ShieldSlot> slot =
                std::make_shared<ShieldSlot>(&ship.components(), 0);
            std::shared_ptr<Shield> shield =
                std::make_shared<Shield>("test-shield");
            shield->setMaxHP(100.0f);
            shield->setHP(100.0f);
            shield->setMaxHits(100);
            slot->setShield(shield);
            ship.components().addShieldSlot(slot);

            std::list<DamageImpact> impacts;
            impacts.emplace_back(
                &ship, voxel, glm::vec3(40.0f, 0.0f, 0.0f), 1.0f);
            Damager damager(world);
            damager.applyDamages(impacts);

            AssertThat(shield->hp(), EqualsWithDelta(60.0f, 0.001f));
            AssertThat(voxel->hp(), Equals(hullHP));
        });

        it("loads shield type from each ship definition", []() {
            PropertyManager::instance()->reset();
            PropertyManager::instance()->load("data/config.ini");
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            PropertyDirectory("data/worldobjects").read();
            PropertyDirectory("data/equipment/engines").read();
            PropertyDirectory("data/equipment/shields").read();
            PropertyDirectory("data/equipment/weapons").read();
            PropertyDirectory("data/equipment/projectiles").read();

            World world(World::Mode::Server);
            ShieldShowcaseScenario scenario(world);
            scenario.load();

            int hullShieldShips = 0;
            int sphereShieldShips = 0;
            int unshieldedShips = 0;
            for (Ship* ship : world.ships()) {
                switch (ship->shieldType()) {
                case ShieldType::Hull:
                    ++hullShieldShips;
                    break;
                case ShieldType::Sphere:
                    ++sphereShieldShips;
                    break;
                case ShieldType::None:
                    ++unshieldedShips;
                    break;
                }
            }
            AssertThat(world.ships().size(), Equals(static_cast<std::size_t>(3)));
            AssertThat(hullShieldShips, Equals(2));
            AssertThat(sphereShieldShips, Equals(1));
            AssertThat(unshieldedShips, Equals(0));
            for (Ship* ship : world.ships()) {
                if (ship->shieldType() == ShieldType::None) {
                    continue;
                }
                const std::shared_ptr<Shield> shield =
                    ship->components().shieldSlots().front()->shield();
                AssertThat(shield->maxHits(), Equals(100));
                AssertThat(shield->maxHP() > 0.0f, IsTrue());
                AssertThat(shield->hp() > 0.0f, IsTrue());
            }
        });

        it("does not allow target cycling to select the player ship", []() {
            PropertyManager::instance()->reset();
            PropertyManager::instance()->load("data/config.ini");
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            PropertyDirectory("data/worldobjects").read();
            PropertyDirectory("data/equipment/engines").read();
            PropertyDirectory("data/equipment/shields").read();
            PropertyDirectory("data/equipment/weapons").read();
            PropertyDirectory("data/equipment/projectiles").read();

            World world(World::Mode::Client);
            ShieldShowcaseScenario scenario(world);
            scenario.load();

            Ship* playerShip = world.player().ship();
            AssertThat(playerShip != nullptr, IsTrue());
            for (Ship* ship : world.ships()) {
                if (ship != playerShip) {
                    ship->info().setCanLockOn(false);
                }
            }

            world.player().selectTarget(true);
            AssertThat(playerShip->targetObject() == nullptr, IsTrue());
        });
    });
});
}
