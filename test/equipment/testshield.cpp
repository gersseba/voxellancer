#include <bandit/bandit.h>
#include <list>

#include "equipment/shield.h"
#include "collision/shieldcontact.h"
#include "equipment/shieldslot.h"
#include "property/propertydirectory.h"
#include "property/propertymanager.h"
#include "property/property.h"
#include "scenarios/shieldshowcasescenario.h"
#include "player.h"
#include "physics/physics.h"
#include "voxel/voxel.h"
#include "voxel/voxelclusterbounds.h"
#include "world/handler/damager.h"
#include "world/helper/damageimpact.h"
#include "world/world.h"
#include "world/god.h"
#include "world/worldlogic.h"
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

        it("recovers a legacy shield after five registered hits and a quiet period", []() {
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

            shield.update(5.0f);
            AssertThat(shield.hp(), Equals(0.0f));
            AssertThat(shield.registerHit(), IsFalse());
            shield.update(0.1f);
            AssertThat(shield.hp(), EqualsWithDelta(10.0f, 0.001f));
            AssertThat(shield.registerHit(), IsTrue());
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

        it("encloses voxel corners so a projectile cannot reach the hull first", []() {
            Shield shield("test-shield");
            shield.setMaxHP(100);
            shield.setHP(100);
            WorldObject object;
            object.addVoxel(new Voxel(glm::ivec3(0)));
            glm::ivec3 cell(0);
            float t = 0;
            AssertThat(shield.intersectsSegment(object,ShieldType::Sphere,
                {-2,0.49f,0.49f},{2,0.49f,0.49f},0,cell,t),IsTrue());
            AssertThat(t < 0.375f,IsTrue());
            const auto sphere = object.bounds().minimalGridSphere();
            AssertThat(sphere.radius() >= glm::length(glm::vec3(0.5f)),IsTrue());
            object.transform().setPosition({-0.1f,-0.1f,-0.1f});
            const auto bounds = object.bounds().aabb(object.transform());
            AssertThat(bounds.llf().x < 0,IsTrue());
        });

        it("sweeps spherical shields against shields and hull voxels at high speed", []() {
            World world(World::Mode::Server);
            Ship first(world), second(world);
            first.addVoxel(new Voxel(glm::ivec3(0)));
            second.addVoxel(new Voxel(glm::ivec3(0)));
            const auto equip = [](Ship& ship) {
                ship.setShieldType(ShieldType::Sphere);
                auto slot = std::make_shared<ShieldSlot>(&ship.components(),0);
                auto shield = std::make_shared<Shield>("test");
                shield->setMaxHP(100);
                shield->setHP(100);
                slot->setShield(shield);
                ship.components().addShieldSlot(slot);
                return shield;
            };
            const auto a = equip(first);
            const auto b = equip(second);
            Transform start = first.transform(), end = start;
            start.setPosition({-10,1.7f,0});
            end.setPosition({10,1.7f,0});
            const auto sphereHit = sweepShieldContact(first,start,end,second);
            AssertThat(sphereHit.has_value(),IsTrue());
            AssertThat(sphereHit->time > 0 && sphereHit->time < 1,IsTrue());
            AssertThat(sphereHit->movingShield && sphereHit->otherShield,IsTrue());
            b->setHP(0);
            start.setPosition({-10,1.3f,0});
            end.setPosition({10,1.3f,0});
            const auto hullHit = sweepShieldContact(first,start,end,second);
            AssertThat(hullHit.has_value(),IsTrue());
            AssertThat(hullHit->movingShield && !hullHit->otherShield,IsTrue());
            AssertThat(hullHit->otherVoxel == second.voxel(glm::ivec3(0)),IsTrue());
            start.setPosition({-10,1.4f,0});
            end.setPosition({10,1.4f,0});
            AssertThat(sweepShieldContact(first,start,end,second).has_value(),IsFalse());
            a->setHP(0);
            AssertThat(sweepShieldContact(first,start,end,second).has_value(),IsFalse());
            b->refill();
            start.setPosition({-10,1.3f,0});
            end.setPosition({10,1.3f,0});
            first.setTransform(start);
            const auto movingHull = sweepShieldContact(first,start,end,second);
            AssertThat(movingHull.has_value(),IsTrue());
            AssertThat(!movingHull->movingShield && movingHull->otherShield,IsTrue());
            AssertThat(movingHull->movingVoxel == first.voxel(glm::ivec3(0)),IsTrue());
        });

        it("stops a fast ship at the shield boundary and damages both shields once", []() {
            World world(World::Mode::Server);
            world.worldLogic().setShieldHitEventReplicationEnabled(true);
            auto makeShip = [&](float x) {
                auto* ship = new Ship(world);
                ship->addVoxel(new Voxel(glm::ivec3(0),0xffffff,1,10000));
                ship->transform().setPosition({x,0,0});
                ship->setShieldType(ShieldType::Sphere);
                auto slot = std::make_shared<ShieldSlot>(&ship->components(),0);
                auto shield = std::make_shared<Shield>("test");
                shield->setMaxHP(10000);
                shield->setHP(10000);
                slot->setShield(shield);
                ship->components().addShieldSlot(slot);
                world.god().scheduleSpawn(ship);
                return std::make_pair(ship,shield);
            };
            const auto first = makeShip(-5), second = makeShip(0);
            world.god().spawn();
            first.first->physics().setSpeed(Speed({100,0,0},{0,0,0}));
            world.update(0.1f);
            AssertThat(first.second->hp() < 10000,IsTrue());
            AssertThat(second.second->hp(),EqualsWithDelta(first.second->hp(),0.01f));
            AssertThat(first.first->voxel(glm::ivec3(0))->hp(),Equals(10000.0f));
            AssertThat(second.first->voxel(glm::ivec3(0))->hp(),Equals(10000.0f));
            AssertThat(glm::distance(first.first->position(),second.first->position()) >=
                std::sqrt(3.0f)-0.001f,IsTrue());
            AssertThat(first.first->physics().speed().directional().x +
                second.first->physics().speed().directional().x,EqualsWithDelta(75.0f,0.01f));
            AssertThat(world.worldLogic().drainShieldHitEvents().size(),Equals(std::size_t(2)));
        });

        it("does not deliver shield-break overflow to a hull outside the contact", []() {
            World world(World::Mode::Server);
            auto* attacker = new Ship(world);
            auto* target = new Ship(world);
            for (auto* ship : {attacker,target})
                ship->addVoxel(new Voxel(glm::ivec3(0),0xffffff,1,10000));
            attacker->transform().setPosition({-5,0,0});
            attacker->setShieldType(ShieldType::Sphere);
            auto slot = std::make_shared<ShieldSlot>(&attacker->components(),0);
            auto shield = std::make_shared<Shield>("test");
            shield->setMaxHP(10);
            shield->setHP(10);
            slot->setShield(shield);
            attacker->components().addShieldSlot(slot);
            world.god().scheduleSpawn(attacker);
            world.god().scheduleSpawn(target);
            world.god().spawn();
            attacker->physics().setSpeed(Speed({100,0,0},{0,0,0}));
            world.update(0.1f);
            AssertThat(shield->hp(),Equals(0.0f));
            AssertThat(attacker->voxel(glm::ivec3(0))->hp(),Equals(10000.0f));
            AssertThat(target->voxel(glm::ivec3(0))->hp() < 10000.0f,IsTrue());
        });

        it("applies sustained thrust pressure over time, but not while resting", []() {
            const auto run = [](float dt, bool thrust) {
                World world(World::Mode::Server);
                auto* first = new Ship(world);
                auto* second = new Ship(world);
                std::shared_ptr<Shield> measured;
                for (auto* ship : {first,second}) {
                    ship->addVoxel(new Voxel(glm::ivec3(0),0xffffff,1,10000));
                    ship->setShieldType(ShieldType::Sphere);
                    auto slot = std::make_shared<ShieldSlot>(&ship->components(),0);
                    auto shield = std::make_shared<Shield>("test");
                    shield->setMaxHP(1000);
                    shield->setHP(1000);
                    slot->setShield(shield);
                    ship->components().addShieldSlot(slot);
                    if (ship == first) measured = shield;
                    world.god().scheduleSpawn(ship);
                }
                first->transform().setPosition({-std::sqrt(3.0f)-0.00001f,0,0});
                world.god().spawn();
                for (int i = 0; i < static_cast<int>(1/dt); ++i) {
                    first->physics().setAcceleration(Acceleration({thrust ? 4.0f : 0.0f,0,0},{0,0,0}));
                    second->physics().setSpeed(Speed({0,0,0},{0,0,0}));
                    world.worldLogic().update(dt);
                }
                return 1000-measured->hp();
            };
            const float sixty = run(1.0f/60,true);
            const float oneTwenty = run(1.0f/120,true);
            AssertThat(sixty > 1.5f,IsTrue());
            AssertThat(oneTwenty,EqualsWithDelta(sixty,0.15f));
            AssertThat(run(1.0f/60,false),Equals(0.0f));
        });

        it("can reach the hull after breaking an opposing spherical shield", []() {
            World world(World::Mode::Server);
            auto* attacker = new Ship(world);
            auto* target = new Ship(world);
            std::shared_ptr<Shield> attackerShield, targetShield;
            for (auto* ship : {attacker,target}) {
                ship->addVoxel(new Voxel(glm::ivec3(0),0xffffff,1,10000));
                ship->setShieldType(ShieldType::Sphere);
                auto slot = std::make_shared<ShieldSlot>(&ship->components(),0);
                auto shield = std::make_shared<Shield>("test");
                shield->setMaxHP(ship == attacker ? 10000 : 10);
                shield->refill();
                slot->setShield(shield);
                ship->components().addShieldSlot(slot);
                (ship == attacker ? attackerShield : targetShield) = shield;
                world.god().scheduleSpawn(ship);
            }
            attacker->transform().setPosition({-5,0,0});
            world.god().spawn();
            attacker->physics().setSpeed(Speed({100,0,0},{0,0,0}));
            world.worldLogic().update(0.1f);
            AssertThat(targetShield->hp(),Equals(0.0f));
            AssertThat(target->voxel(glm::ivec3(0))->hp(),Equals(10000.0f));
            target->updateTransformAndGeode({0,0,0},target->orientation());
            target->physics().setSpeed(Speed({0,0,0},{0,0,0}));
            attacker->physics().setSpeed(Speed({20,0,0},{0,0,0}));
            world.worldLogic().update(0.1f);
            AssertThat(attackerShield->hp() > 0,IsTrue());
            AssertThat(attacker->voxel(glm::ivec3(0))->hp(),Equals(10000.0f));
            AssertThat(target->voxel(glm::ivec3(0))->hp() < 10000.0f,IsTrue());
        });

        it("detects a rotating hull sweeping into a spherical shield", []() {
            World world(World::Mode::Server);
            auto* hull = new Ship(world);
            for (int x = 0; x < 5; ++x)
                hull->addVoxel(new Voxel({x,0,0},0xffffff,1,10000));
            hull->transform().setPosition({0,0,0});
            auto* protectedShip = new Ship(world);
            protectedShip->addVoxel(new Voxel({0,0,0},0xffffff,1,10000));
            protectedShip->transform().setPosition({0,3,0});
            protectedShip->setShieldType(ShieldType::Sphere);
            auto slot = std::make_shared<ShieldSlot>(&protectedShip->components(),0);
            auto shield = std::make_shared<Shield>("test");
            shield->setMaxHP(1000);
            shield->refill();
            slot->setShield(shield);
            protectedShip->components().addShieldSlot(slot);
            world.god().scheduleSpawn(hull);
            world.god().scheduleSpawn(protectedShip);
            world.god().spawn();
            hull->physics().setSpeed(Speed({0,0,0},{0,0,20}));
            world.worldLogic().update(0.1f);
            AssertThat(shield->hp() < 1000,IsTrue());
            AssertThat(hull->physics().speed().angular().z < 15,IsTrue());
        });

        it("rejects spawning ships whose spherical shields overlap", []() {
            World world(World::Mode::Server);
            auto makeShip = [&](float x) {
                auto* ship = new Ship(world);
                ship->addVoxel(new Voxel(glm::ivec3(0)));
                ship->transform().setPosition({x,0,0});
                ship->setShieldType(ShieldType::Sphere);
                auto slot = std::make_shared<ShieldSlot>(&ship->components(),0);
                auto shield = std::make_shared<Shield>("test");
                shield->setMaxHP(100);
                shield->refill();
                slot->setShield(shield);
                ship->components().addShieldSlot(slot);
                return ship;
            };
            auto* first = makeShip(0);
            auto* second = makeShip(1.5f);
            world.god().scheduleSpawn(first);
            world.god().scheduleSpawn(SpawnRequest(second,false));
            world.god().spawn();
            AssertThat(first->spawnState() == SpawnState::Spawned,IsTrue());
            AssertThat(second->spawnState() == SpawnState::Rejected,IsTrue());
            delete second;
        });

        it("destroys an exposed hull voxel without damaging the ramming ship's hull", []() {
            World world(World::Mode::Server);
            auto* attacker = new Ship(world);
            attacker->addVoxel(new Voxel({0,0,0},0xffffff,1,10000));
            attacker->transform().setPosition({-6,0,0});
            attacker->setShieldType(ShieldType::Sphere);
            auto slot = std::make_shared<ShieldSlot>(&attacker->components(),0);
            auto shield = std::make_shared<Shield>("test");
            shield->setMaxHP(10000);
            shield->refill();
            slot->setShield(shield);
            attacker->components().addShieldSlot(slot);
            auto* target = new Ship(world);
            target->addVoxel(new Voxel({0,0,0},0xffffff,1,1));
            target->addVoxel(new Voxel({1,0,0},0xffffff,1,10000));
            target->addVoxel(new Voxel({2,0,0},0xffffff,1,10000));
            target->transform().setPosition({0,0,0});
            world.god().scheduleSpawn(attacker);
            world.god().scheduleSpawn(target);
            world.god().spawn();
            attacker->physics().setSpeed(Speed({100,0,0},{0,0,0}));
            world.worldLogic().update(0.1f);
            AssertThat(target->voxel({0,0,0}) == nullptr,IsTrue());
            AssertThat(target->voxel({1,0,0}) != nullptr,IsTrue());
            AssertThat(attacker->voxel({0,0,0})->hp(),Equals(10000.0f));
            AssertThat(shield->hp() > 0,IsTrue());
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

        it("restarts a broken shield's quiet period when the hull is damaged", []() {
            World world(World::Mode::Server);
            Ship ship(world);
            ship.setShieldType(ShieldType::Sphere);
            ship.addVoxel(new Voxel(glm::ivec3(0, 0, 0)));
            auto slot = std::make_shared<ShieldSlot>(&ship.components(), 0);
            auto shield = std::make_shared<Shield>("test-shield");
            shield->setMaxHP(100);
            shield->setHP(100);
            shield->setRegeneration(10);
            shield->setRechargeDelay(2);
            slot->setShield(shield);
            ship.components().addShieldSlot(slot);
            shield->compensate(100);
            shield->update(4.0f);

            Damager damager(world);
            std::list<DamageImpact> impacts;
            impacts.emplace_back(&ship, ship.voxel(glm::ivec3(0)), glm::vec3(0), 0, true);
            damager.applyDamages(impacts);
            AssertThat(shield->rechargeRemaining(), Equals(1.0f));
            impacts.clear();
            impacts.emplace_back(&ship, ship.voxel(glm::ivec3(0)), glm::vec3(1,0,0), 0, true);
            damager.applyDamages(impacts);
            AssertThat(shield->rechargeRemaining(), Equals(5.0f));
            shield->update(4.99f);
            AssertThat(shield->hp(), Equals(0.0f));
            shield->update(0.11f);
            AssertThat(shield->hp(), EqualsWithDelta(1.0f,0.001f));
        });

        it("preserves a longer authored delay after a break", []() {
            Shield shield("test-shield");
            shield.setMaxHP(100);
            shield.setHP(100);
            shield.setRegeneration(10);
            shield.setRechargeDelay(7);
            shield.compensate(100);
            shield.update(5);
            shield.noteShipHit();
            shield.update(6.9f);
            AssertThat(shield.hp(),Equals(0.0f));
            shield.update(0.2f);
            AssertThat(shield.hp(),EqualsWithDelta(1.0f,0.001f));
        });

        it("selects the world shield shape without equipping unshielded ships", []() {
            PropertyManager::instance()->reset();
            PropertyManager::instance()->load("data/config.ini");
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            PropertyDirectory("data/worldobjects").read();
            PropertyDirectory("data/equipment/engines").read();
            PropertyDirectory("data/equipment/shields").read();
            PropertyDirectory("data/equipment/weapons").read();
            PropertyDirectory("data/equipment/projectiles").read();

            Property<std::string>("f302.general.shield_type").set("NONE");
            for (const std::string shape : {"SPHERE","HULL"}) {
                World world(World::Mode::Server);
                world.setShieldShape(shape);
                ShieldShowcaseScenario scenario(world);
                scenario.load();
                AssertThat(world.ships().size(), Equals(static_cast<std::size_t>(3)));
                int shielded = 0, unshielded = 0;
                for (Ship* ship : world.ships()) {
                    if (ship->shieldType() == ShieldType::None) {
                        ++unshielded;
                        continue;
                    }
                    ++shielded;
                    AssertThat(ship->shieldType() == world.shieldShape(),IsTrue());
                    const std::shared_ptr<Shield> shield =
                        ship->components().shieldSlots().front()->shield();
                    AssertThat(shield->maxHits(), Equals(100));
                    AssertThat(shield->maxHP() > 0.0f, IsTrue());
                    AssertThat(shield->hp() > 0.0f, IsTrue());
                }
                AssertThat(shielded, Equals(2));
                AssertThat(unshielded, Equals(1));
                world.setShieldShape(shape == "SPHERE" ? "HULL" : "SPHERE");
                for (Ship* ship : world.ships())
                    if (ship->shieldType() != ShieldType::None)
                        AssertThat(ship->shieldType() != world.shieldShape(),IsTrue());
                AssertThrows(std::invalid_argument,world.setShieldShape("OTHER"));
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
