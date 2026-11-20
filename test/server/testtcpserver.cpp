#include <bandit/bandit.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

#include "ai/basictasks/fighttask.h"
#include "ai/character.h"
#include "client/tcpclient.h"
#include "client/inputpredictor.h"
#include "equipment/engineslot.h"
#include "equipment/hardpoint.h"
#include "equipment/shield.h"
#include "equipment/shieldslot.h"
#include "equipment/shieldcatalog.h"
#include "equipment/weapons/gun.h"
#include "equipment/weapons/rocket.h"
#include "geometry/speed.h"
#include "missions/missionsystem.h"
#include "missions/mission.h"
#include "physics/physics.h"
#include "property/property.h"
#include "property/propertydirectory.h"
#include "property/propertymanager.h"
#include "resource/worldobjectbuilder.h"
#include "scenarios/battlescenario.h"
#include "server/tcpserver.h"
#include "simulation/simulationsession.h"
#include "simulation/contractrun.h"
#include "simulation/contractlevel.h"
#include "utils/simulationtickrunner.h"
#include "voxel/voxel.h"
#include "voxel/specialvoxels/hardpointvoxel.h"
#include "voxel/specialvoxels/crucialvoxel.h"
#include "equipment/weapons/bullet.h"
#include "equipment/weaponcatalog.h"
#include "resource/weaponbuilder.h"
#include "resource/shieldbuilder.h"
#include "world/handler/mover.h"
#include "world/god.h"
#include "world/helper/damageimpact.h"
#include "world/world.h"
#include "worldobject/ship.h"
#include "world/worldlogic.h"
#include "worldobject/worldobjectcomponents.h"

using namespace bandit;

namespace {
    void loadSimulationData() {
        PropertyManager::instance()->load("data/config.ini");
        PropertyManager::instance()->load("data/voxels.ini", "voxels");
        PropertyDirectory("data/worldobjects").read();
        PropertyDirectory("data/equipment/engines").read();
        PropertyDirectory("data/equipment/shields").read();
        PropertyDirectory("data/equipment/weapons").read();
        PropertyDirectory("data/equipment/projectiles").read();
    }

    struct WorldResetGuard {
        ~WorldResetGuard() {
            World::reset(false);
        }
    };

    void shootProfile(World& world, const std::string& key, const glm::vec3& origin,
        float deltaSec = 0.1f) {
        auto* bullet = WorldObjectBuilder(key, world).buildBullet();
        bullet->transform().setPosition(origin);
        bullet->physics().setSpeed(Speed(glm::vec3(0,0,-bullet->profile.speed),glm::vec3(0)));
        world.god().scheduleSpawn(bullet);
        world.god().spawn();
        Mover(world).moveProfiledProjectiles(deltaSec);
        world.god().remove();
    }

    WorldObject* blockTarget(World& world, int count, bool armor = false) {
        auto* target = new WorldObject();
        target->setWorld(world);
        for (int z = 0; z < count; ++z) {
            auto* voxel = new Voxel(glm::ivec3(10,10,20-z),0x778899,armor ? 3 : 1,
                armor ? 150 : 50);
            voxel->setArmored(armor);
            target->addVoxel(voxel);
        }
        world.god().scheduleSpawn(target);
        world.god().spawn();
        return target;
    }
}

go_bandit([]() {
    describe("TCP simulation client and server", []() {
        it("spends contract energy once per volley and regenerates only after the firing delay", []() {
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            auto& world = *World::instance(World::Mode::Server);
            const auto actor = ContractRun::createPlayers(world,1).front();
            auto& components = world.worldObject(actor)->components();
            const auto slot = components.hardpoints().front();
            slot->setWeapon(std::shared_ptr<Weapon>(WeaponBuilder("scattercannon").build()));
            auto* gun = static_cast<Gun*>(slot->weapon().get());
            const auto aim = slot->voxel()->position()+glm::vec3(0,0,-100);
            components.configureWeaponEnergy(15,10,1);
            gun->fireAtPoint(slot->voxel()->position()+glm::vec3(0,0,100),false);
            AssertThat(components.weaponEnergy(),Equals(15.0f));
            gun->fireAtPoint(aim,false);
            world.god().spawn();
            AssertThat(components.weaponEnergy(),Equals(0.0f));
            const auto projectiles = world.worldObjects().size();
            AssertThat(projectiles,Equals(std::size_t(9)));
            gun->update(10);
            gun->fireAtPoint(aim,false);
            world.god().spawn();
            AssertThat(world.worldObjects().size(),Equals(projectiles));
            components.update(0.5f);
            AssertThat(components.weaponEnergy(),Equals(0.0f));
            components.update(1.0f);
            AssertThat(components.weaponEnergy(),EqualsWithDelta(5.0f,0.0001f));
            components.update(10);
            AssertThat(components.weaponEnergy(),Equals(15.0f));
            gun->fireAtPoint(aim,false);
            world.god().spawn();
            AssertThat(world.worldObjects().size(),Equals(projectiles+8));
            AssertThat(components.weaponEnergy(),Equals(0.0f));
        });

        it("shares contract energy across mounts and fairly rotates firing priority", []() {
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            auto& world = *World::instance(World::Mode::Server);
            world.worldLogic().setWeaponEventReplicationEnabled(true);
            const auto actor = ContractRun::createPlayers(world,1).front();
            auto& components = world.worldObject(actor)->components();
            std::vector<std::shared_ptr<Hardpoint>> mounts(
                components.hardpoints().begin(),components.hardpoints().end());
            AssertThat(mounts.size() >= 2,IsTrue());
            components.configureWeaponEnergy(4,4,0);
            components.fireAtPoint(glm::vec3(0,0,-1000),false);
            world.god().spawn();
            AssertThat(components.weaponEnergy(),Equals(0.0f));
            AssertThat(mounts[0]->weapon()->canFire(),IsFalse());
            components.fireAtPoint(glm::vec3(0,0,-1000),false);
            components.update(1);
            components.fireAtPoint(glm::vec3(0,0,-1000),false);
            world.god().spawn();
            AssertThat(components.weaponEnergy(),Equals(0.0f));
            AssertThat(world.worldObjects().size(),Equals(std::size_t(3)));
            const auto events = world.worldLogic().drainWeaponFiredEvents();
            AssertThat(events.size(),Equals(std::size_t(2)));
            AssertThat(events[0].cell != events[1].cell,IsTrue());
        });

        it("recovers broken contract shield builds after damage delays and accounts for partial ticks", []() {
            loadSimulationData();
            for (std::uint8_t id = 1; id <= ShieldCatalogSize; ++id) {
                const auto profile = catalogShield(id);
                std::unique_ptr<Shield> shield(ShieldBuilder(profile.key).build());
                AssertThat(shield->compensate(profile.capacity+100),EqualsWithDelta(100.0f,0.01f));
                shield->update(profile.rechargeDelay*0.5f);
                AssertThat(shield->hp(),Equals(0.0f));
                shield->update(profile.rechargeDelay*0.5f+0.25f);
                AssertThat(shield->hp(),EqualsWithDelta(profile.regeneration*0.25f,0.001f));
                shield->compensate(1);
                const auto hp = shield->hp();
                shield->compensate(0);
                shield->update(profile.rechargeDelay);
                AssertThat(shield->hp(),EqualsWithDelta(hp,0.001f));
                shield->update(1000);
                AssertThat(shield->hp(),Equals(profile.capacity));
                shield->compensate(100);
                shield->refill();
                AssertThat(shield->hp(),Equals(profile.capacity));
                AssertThat(shield->rechargeRemaining(),Equals(0.0f));
            }
        });

        it("resolves contract pulse surface damage and bounded kinetic penetration", []() {
            loadSimulationData();
            for (const auto& test : std::vector<std::pair<std::string,int>>{
                    {"pulselaser_bullet",1},{"pulselasermk2_bullet",1},
                    {"heavyautocannon_bullet",2},{"kineticrailgun_bullet",6}}) {
                World::reset(false);
                WorldResetGuard resetGuard;
                auto& world = *World::instance(World::Mode::Server);
                auto* target = blockTarget(world,8);
                const auto origin = target->voxel(glm::ivec3(10,10,20))->position()+glm::vec3(0,0,10);
                shootProfile(world,test.first,origin);
                AssertThat(target->voxelCount(),Equals(8-test.second));
            }
        });

        it("slows contract penetrators before sweeping the remainder of the same tick", []() {
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            auto& world = *World::instance(World::Mode::Server);
            auto* target = blockTarget(world,2);
            target->removeVoxel(target->voxel(glm::ivec3(10,10,19)));
            target->addVoxel(new Voxel(glm::ivec3(10,10,15),0x778899,1,50));
            const auto origin = target->voxel(glm::ivec3(10,10,20))->position()+glm::vec3(0,0,10);
            shootProfile(world,"heavyautocannon_bullet",origin,0.037f);
            AssertThat(target->voxelCount(),Equals(1));
            AssertThat(target->voxel(glm::ivec3(10,10,15))->hp(),Equals(50.0f));
            Mover(world).moveProfiledProjectiles(0.01f);
            world.god().remove();
            AssertThat(target->voxelCount(),Equals(0));
        });

        it("spends contract rail energy across armor and normal blocks without duplication", []() {
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            auto& world = *World::instance(World::Mode::Server);
            auto* target = blockTarget(world,4);
            target->removeVoxel(target->voxel(glm::ivec3(10,10,20)));
            auto* armor = new Voxel(glm::ivec3(10,10,20),0x778899,3,150);
            armor->setArmored(true);
            target->addVoxel(armor);
            const auto origin = armor->position()+glm::vec3(0,0,10);
            shootProfile(world,"kineticrailgun_bullet",origin);
            AssertThat(target->voxelCount(),Equals(2));
            AssertThat(target->voxel(glm::ivec3(10,10,18)) != nullptr,IsTrue());
        });

        it("keeps contract plasma damage on exposed surfaces rather than hidden layers", []() {
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            auto& world = *World::instance(World::Mode::Server);
            auto* target = blockTarget(world,3);
            const auto origin = target->voxel(glm::ivec3(10,10,20))->position()+glm::vec3(0,0,10);
            shootProfile(world,"plasmamortar_bullet",origin);
            AssertThat(target->voxelCount(),Equals(2));
            AssertThat(target->voxel(glm::ivec3(10,10,19))->hp(),Equals(50.0f));
        });

        it("enforces contract projectile travel distance before a distant hull", []() {
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            auto& world = *World::instance(World::Mode::Server);
            auto* target = blockTarget(world,1);
            const auto origin = target->voxel(glm::ivec3(10,10,20))->position()+glm::vec3(0,0,650);
            auto* bullet = WorldObjectBuilder("pulselaser_bullet",world).buildBullet();
            bullet->setLifetime(10);
            bullet->transform().setPosition(origin);
            bullet->physics().setSpeed(Speed(glm::vec3(0,0,-bullet->profile.speed),glm::vec3(0)));
            world.god().scheduleSpawn(bullet);
            world.god().spawn();
            Mover(world).moveProfiledProjectiles(2.0f);
            world.god().remove();
            AssertThat(target->voxelCount(),Equals(1));
            AssertThat(target->voxel(glm::ivec3(10,10,20))->hp(),Equals(50.0f));
            AssertThat(world.worldObjects().size(),Equals(std::size_t(1)));
        });

        it("absorbs contract energy once and passes only shield overflow to hull", []() {
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            auto& world = *World::instance(World::Mode::Server);
            auto* target = new Ship(world);
            target->setWorld(world);
            target->setShieldType(ShieldType::Sphere);
            auto* voxel = new Voxel(glm::ivec3(10,10,20),0x778899,1,50);
            target->addVoxel(voxel);
            auto slot = std::make_shared<ShieldSlot>(&target->components(),0);
            slot->setMountable("thinshield",true);
            slot->setShield(std::shared_ptr<Shield>(ShieldBuilder("thinshield").build()));
            target->components().addShieldSlot(slot);
            world.god().scheduleSpawn(target);
            world.god().spawn();
            const auto origin = voxel->position()+glm::vec3(0,0,10);
            shootProfile(world,"pulselaser_bullet",origin);
            AssertThat(slot->shield()->hp(),Equals(19000.0f));
            AssertThat(voxel->hp(),Equals(50.0f));
            target->info().setInvulnerable(true);
            shootProfile(world,"pulselaser_bullet",origin);
            AssertThat(slot->shield()->hp(),Equals(19000.0f));
            target->info().setInvulnerable(false);
            slot->shield()->setHP(250);
            shootProfile(world,"pulselaser_bullet",origin);
            AssertThat(slot->shield()->hp(),Equals(0.0f));
            AssertThat(voxel->hp(),EqualsWithDelta(12.5f,0.001f));
        });

        it("fires eight contract scatter pellets with one shared volley budget", []() {
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            auto& world = *World::instance(World::Mode::Server);
            const auto pilot = ContractRun::createPlayers(world,1).front();
            auto* ship = world.worldObject(pilot);
            const auto aim = ship->position()+glm::vec3(0,0,-1000);
            bool fired = false;
            for (const auto& slot : ship->components().hardpoints()) {
                slot->setWeapon(std::shared_ptr<Weapon>(WeaponBuilder("scattercannon").build()));
                auto* gun = static_cast<Gun*>(slot->weapon().get());
                if (slot->inFieldOfAim(aim) && gun->isBulletPathClear(aim,false)) {
                    gun->fireAtPoint(aim,false);
                    fired = true;
                    break;
                }
            }
            AssertThat(fired,IsTrue());
            world.god().spawn();
            unsigned pellets = 0;
            float shield = 0, normal = 0;
            for (auto* object : world.worldObjects()) {
                if (auto* bullet = dynamic_cast<Bullet*>(object)) {
                    ++pellets;
                    shield += bullet->profile.shield;
                    normal += bullet->profile.normal;
                }
            }
            AssertThat(pellets,Equals(8u));
            AssertThat(shield,Equals(1000.0f));
            AssertThat(normal,Equals(160.0f));
        });

        it("aims a contract pilot's primary fire from TCP independently of steering", []() {
            using namespace voxellancer::protocol;
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);
            const auto pilot = ContractRun::createPlayers(*world, 1).front();
            auto* ship = static_cast<Ship*>(world->worldObject(pilot));
            const auto orientation = glm::angleAxis(0.7f, glm::vec3(0, 1, 0));
            ship->updateTransformAndGeode(glm::vec3(120, 25, -80), orientation);
            SimulationSession session(*world);
            TcpServer server(*world, session, 0, 650.0f, pilot);
            TcpClient client;
            AssertThat(client.connectLocal(server.port()), IsTrue());
            for (int attempt = 0; attempt < 100 && !client.actor(); ++attempt) {
                server.poll();
                AssertThat(client.poll(), IsTrue());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(client.actor().has_value(), IsTrue());
            InputCommand command{1};
            command.tick = session.tickIndex() + 10;
            command.buttons = FirePrimary;
            command.firePoint = glm::vec3(70, 25, -300);
            const auto target = ship->position() + orientation * command.firePoint;
            AssertThat(client.sendInput(command), IsTrue());
            WorldObject* bullet = nullptr;
            for (int attempt = 0; attempt < 100 && !bullet; ++attempt) {
                AssertThat(client.poll(), IsTrue());
                server.poll();
                session.advance(SimulationTickRunner::TickDurationSeconds);
                server.poll();
                for (WorldObject* object : world->worldObjects()) {
                    if (object->objectType() == WorldObjectType::Bullet) {
                        bullet = object;
                        break;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(bullet != nullptr, IsTrue());
            const auto direction = glm::normalize(bullet->physics().speed().directional());
            AssertThat(glm::dot(direction, glm::normalize(target - bullet->position())) >
                0.99999f, IsTrue());
            AssertThat(glm::dot(direction, orientation * glm::vec3(0, 0, -1)) <
                0.99f, IsTrue());
            AssertThat(ship->orientation() == orientation, IsTrue());
        });

        it("leads a crossing target separately for each contract gun and ignores stale targets", []() {
            using namespace voxellancer::protocol;
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            auto& world = *World::instance(World::Mode::Server);
            const auto pilots = ContractRun::createPlayers(world, 2);
            auto& shooter = *world.worldObject(pilots[0]);
            auto& target = *world.worldObject(pilots[1]);
            const glm::quat forward(1, 0, 0, 0);
            shooter.updateTransformAndGeode(glm::vec3(0), forward);
            target.updateTransformAndGeode(glm::vec3(0, 0, -300), forward);
            auto& mounts = shooter.components().hardpoints();
            AssertThat(mounts.size() >= 2, IsTrue());
            for (const auto& mount : mounts) mount->setWeapon(nullptr);
            mounts.front()->setWeapon(std::shared_ptr<Weapon>(WeaponBuilder("pulselaser").build()));
            mounts.back()->setWeapon(std::shared_ptr<Weapon>(WeaponBuilder("pulselasermk2").build()));

            SimulationSession session(world);
            InputCommand command{1};
            command.tick = session.tickIndex();
            command.buttons = FirePrimary | FireWithLead;
            command.firePoint = glm::vec3(-60, 0, -300); // Not the assisted aim.
            command.target = pilots[1];
            ++command.target.generation;
            AssertThat(session.submitInputCommand(pilots[0], command), Equals(InputCommandResult::Queued));
            session.advance(SimulationTickRunner::TickDurationSeconds);
            AssertThat(world.worldObjects().size(), Equals(std::size_t(2)));

            shooter.physics().setSpeed(Speed(glm::vec3(-10, 0, 0), glm::vec3(0)));
            target.physics().setSpeed(Speed(glm::vec3(80, 0, 0), glm::vec3(0)));
            const glm::vec3 targetStart = target.position();
            const glm::vec3 targetVelocity = target.physics().speed().directional();
            ++command.sequence;
            command.tick = session.tickIndex();
            command.target = pilots[1];
            AssertThat(session.submitInputCommand(pilots[0], command), Equals(InputCommandResult::Queued));
            session.advance(SimulationTickRunner::TickDurationSeconds);

            std::vector<glm::vec3> directions;
            for (auto* object : world.worldObjects()) {
                if (object->objectType() != WorldObjectType::Bullet) continue;
                const auto velocity = object->physics().speed().directional();
                const auto spawn = object->position() -
                    velocity * static_cast<float>(SimulationTickRunner::TickDurationSeconds);
                const auto relative = velocity - targetVelocity;
                const float time = glm::dot(targetStart - spawn, relative) / glm::dot(relative, relative);
                AssertThat(time > 0.0f, IsTrue());
                AssertThat(glm::distance(spawn + velocity * time,
                    targetStart + targetVelocity * time) < 0.1f, IsTrue());
                directions.push_back(glm::normalize(velocity));
            }
            AssertThat(directions.size(), Equals(std::size_t(2)));
            AssertThat(glm::distance(directions[0], directions[1]) > 0.02f, IsTrue());
        });

        it("keeps all authored contract and legacy gun speeds within the launch speed band", []() {
            loadSimulationData();
            std::size_t guns = 0;
            for (const auto& entry : std::filesystem::directory_iterator("data/equipment/weapons")) {
                if (entry.path().extension() != ".ini") continue;
                const auto key = entry.path().stem().string();
                if (Property<std::string>::get(key + ".general.type") != "gun") continue;
                const auto projectile = Property<std::string>::get(key + ".general.bullet");
                const float speed = Property<float>::get(projectile + ".general.speed");
                AssertThat(speed >= 375.0f && speed <= 625.0f, IsTrue());
                ++guns;
            }
            AssertThat(guns >= WeaponCatalogSize, IsTrue());
        });

        it("protects contract player shields and hull while leaving enemies damageable", []() {
            using namespace voxellancer::protocol;
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);
            const auto players = ContractRun::createPlayers(*world, 1);
            ContractRun run(*world, players);
            const auto pilot = players.front();
            auto* ship = static_cast<Ship*>(world->worldObject(pilot));
            Voxel* voxel = ship->voxelMap().begin()->second;
            const float hp = voxel->hp();
            const auto shield = ship->components().shieldSlots().front()->shield();
            const float shieldHP = shield->hp();
            const int count = ship->voxelCount();
            AssertThat(run.status(pilot).invulnerable, IsTrue());
            world->worldLogic().processDamageImpacts({
                DamageImpact(ship, voxel, glm::vec3(1000000.0f, 0.0f, 0.0f), 1.0f)
            });
            AssertThat(voxel->hp(), Equals(hp));
            AssertThat(shield->hp(), Equals(shieldHP));
            AssertThat(ship->voxelCount(), Equals(count));

            run.act(pilot, {1, 0, ContractActionKind::DisableInvulnerability});
            AssertThat(run.status(pilot).invulnerable, IsFalse());
            shield->setHP(10.0f);
            world->worldLogic().processDamageImpacts({
                DamageImpact(ship, voxel, glm::vec3(5.0f, 0.0f, 0.0f), 1.0f)
            });
            AssertThat(shield->hp(), Equals(5.0f));
            shield->setHP(0.0f);
            const float damage = std::min(hp * 0.5f, 2.0f);
            world->worldLogic().processDamageImpacts({
                DamageImpact(ship, voxel, glm::vec3(damage, 0.0f, 0.0f), 1.0f)
            });
            AssertThat(voxel->hp(), Equals(hp - damage));
            run.act(pilot, {2, 0, ContractActionKind::Ready});
            // A preference change must survive a concurrent mission transition.
            run.act(pilot, {3, 0, ContractActionKind::EnableInvulnerability});
            AssertThat(run.status(pilot).result, Equals(ContractResult::Accepted));
            AssertThat(run.status(pilot).invulnerable, IsTrue());
            world->worldLogic().processDamageImpacts({
                DamageImpact(ship, voxel, glm::vec3(damage, 0.0f, 0.0f), 1.0f)
            });
            AssertThat(voxel->hp(), Equals(hp - damage));
            run.act(pilot, {1, 1, ContractActionKind::DisableInvulnerability});
            AssertThat(run.status(pilot).result, Equals(ContractResult::StaleRequest));
            AssertThat(run.status(pilot).invulnerable, IsTrue());
            for (Ship* enemy : world->ships()) {
                if (enemy == ship) continue;
                AssertThat(enemy->info().invulnerable(), IsFalse());
                enemy->setShieldType(ShieldType::None);
                Voxel* target = enemy->voxelMap().begin()->second;
                const float before = target->hp();
                world->worldLogic().processDamageImpacts({
                    DamageImpact(enemy, target, glm::vec3(before * 0.5f, 0.0f, 0.0f), 1.0f)
                });
                AssertThat(target->hp(), Equals(before * 0.5f));
            }
        });

        it("loads distinct contract attacks with authored enemies shields and debris", []() {
            using namespace voxellancer::protocol;
            loadSimulationData();
            for (std::uint8_t id = 1; id <= 3; ++id) {
                World::reset(false);
                WorldResetGuard resetGuard;
                auto& world = *World::instance(World::Mode::Server);
                const auto players = ContractRun::createPlayers(world,1);
                ContractRun run(world,players);
                run.act(players[0],{1,0,ContractActionKind::SelectNode,0,glm::ivec3(0),id});
                run.act(players[0],{2,run.status(players[0]).mission,ContractActionKind::Ready});
                const auto level = contractLevel(id);
                AssertThat(run.status(players[0]).remaining,Equals(std::uint16_t(level.enemiesPerPlayer)));
                AssertThat(world.worldObjects().size(),
                    Equals(std::size_t(1+level.enemiesPerPlayer+level.cover)));
                for (auto* ship : world.ships()) {
                    if (world.entityId(ship) == players[0]) continue;
                    if (id == 3) {
                        AssertThat(ship->components().shieldSlots().empty(),IsFalse());
                        AssertThat(ship->components().shieldSlots().front()->shield()->hp() > 0,IsTrue());
                    }
                    const auto key = id == 1 ? "pulselaser" : id == 2 ? "heavyautocannon" : "plasmamortar";
                    AssertThat(std::any_of(ship->components().hardpoints().begin(),
                        ship->components().hardpoints().end(),[&](const auto& slot) {
                            return slot->weapon() && slot->weapon()->equipmentKey() == key;
                        }),IsTrue());
                }
            }
        });

        it("validates shared contract destinations and requires fresh co-op readiness", []() {
            using namespace voxellancer::protocol;
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            auto& world = *World::instance(World::Mode::Server);
            const auto players = ContractRun::createPlayers(world,2);
            ContractRun run(world,players);
            run.act(players[0],{1,0,ContractActionKind::Ready});
            run.act(players[1],{1,0,ContractActionKind::SelectNode,0,glm::ivec3(0),2});
            AssertThat(run.status(players[0]).readyPlayers,Equals(std::uint8_t(0)));
            run.act(players[0],{2,0,ContractActionKind::Ready});
            AssertThat(run.status(players[0]).result,Equals(ContractResult::StaleRequest));
            AssertThat(run.status(players[0]).acknowledgedSequence,Equals(std::uint64_t(2)));
            const auto revision = run.status(players[0]).mission;
            run.act(players[0],{3,revision,ContractActionKind::Ready});
            run.act(players[1],{2,revision,ContractActionKind::Ready});
            auto shield = world.worldObject(players[0])->components().shieldSlots().front()->shield();
            shield->setHP(42);
            for (auto* ship : world.ships())
                if (std::find(players.begin(),players.end(),*world.entityId(ship)) == players.end())
                    world.god().scheduleRemoval(ship);
            world.god().remove();
            run.update();
            run.act(players[0],{4,run.status(players[0]).mission,ContractActionKind::ClaimReward,
                0,glm::ivec3(0),NoRouteNode,RewardChoice::Credits});
            run.act(players[1],{3,run.status(players[1]).mission,ContractActionKind::ClaimReward,
                0,glm::ivec3(0),NoRouteNode,RewardChoice::Credits});
            auto state = run.status(players[0]);
            AssertThat(state.phase,Equals(ContractPhase::Route));
            AssertThat(shield->hp(),Equals(42.0f));
            std::uint8_t base = NoRouteNode, encounter = NoRouteNode;
            for (std::size_t i = 0; i < state.route.size(); ++i)
                if (state.route[state.currentNode].links & (1u << i)) {
                    if (state.route[i].kind == RouteNodeKind::Base) base = static_cast<std::uint8_t>(i);
                    if (state.route[i].kind == RouteNodeKind::Encounter) encounter = static_cast<std::uint8_t>(i);
                }
            run.act(players[0],{5,state.mission,ContractActionKind::SelectNode,0,glm::ivec3(0),16});
            AssertThat(run.status(players[0]).result,Equals(ContractResult::InvalidRoute));
            run.act(players[0],{6,state.mission,ContractActionKind::SelectNode,0,glm::ivec3(0),encounter});
            AssertThat(run.status(players[0]).result,Equals(ContractResult::EncounterUnavailable));
            AssertThat(run.status(players[0]).mission,Equals(state.mission));
            run.act(players[0],{7,state.mission,ContractActionKind::SelectNode,0,glm::ivec3(0),base});
            const auto baseRevision = run.status(players[0]).mission;
            run.act(players[0],{8,baseRevision,ContractActionKind::Ready});
            run.act(players[1],{4,baseRevision,ContractActionKind::Ready});
            AssertThat(run.status(players[0]).phase,Equals(ContractPhase::Station));
            AssertThat(shield->hp(),Equals(shield->maxHP()));
            AssertThat(run.status(players[0]).credits,Equals(state.credits));
        });

        it("keeps every contract route playable and completes a finite run without replay rewards", []() {
            using namespace voxellancer::protocol;
            loadSimulationData();
            for (int attempt = 0; attempt < 4; ++attempt) {
                World::reset(false);
                WorldResetGuard resetGuard;
                auto& world = *World::instance(World::Mode::Server);
                const auto players = ContractRun::createPlayers(world,1);
                const auto pilot = players[0];
                ContractRun run(world,players);
                const auto graph = run.status(pilot).route;
                std::uint32_t reachable = 1;
                for (std::size_t i = 0; i < graph.size(); ++i) {
                    if (graph[i].kind == RouteNodeKind::Encounter) continue;
                    std::uint32_t usable = 0;
                    for (std::size_t j = 0; j < graph.size(); ++j)
                        if ((graph[i].links & (1u << j)) && graph[j].kind != RouteNodeKind::Encounter)
                            usable |= 1u << j;
                    AssertThat(graph[i].row == 6 || usable != 0,IsTrue());
                    if (reachable & (1u << i)) reachable |= usable;
                }
                AssertThat((reachable & (1u << (graph.size()-1))) != 0,IsTrue());
                std::uint64_t sequence = 0;
                std::uint32_t earned = 0;
                for (int step = 0; step < 6; ++step) {
                    auto state = run.status(pilot);
                    int selected = -1;
                    for (std::size_t i = 0; i < graph.size(); ++i)
                        if ((graph[state.currentNode].links & (1u << i)) &&
                            graph[i].kind != RouteNodeKind::Encounter) {
                            if (selected < 0 || graph[i].kind == RouteNodeKind::Base ||
                                (graph[selected].kind != RouteNodeKind::Base && graph[i].lane == 1))
                                selected = static_cast<int>(i);
                        }
                    AssertThat(selected >= 0,IsTrue());
                    run.act(pilot,{++sequence,state.mission,ContractActionKind::SelectNode,0,
                        glm::ivec3(0),static_cast<std::uint8_t>(selected)});
                    run.act(pilot,{++sequence,run.status(pilot).mission,ContractActionKind::Ready});
                    if (run.inMission()) {
                        earned += run.status(pilot).reward;
                        for (auto* ship : world.ships())
                            if (world.entityId(ship) != pilot) world.god().scheduleRemoval(ship);
                        world.god().remove();
                        run.update();
                        run.act(pilot,{++sequence,run.status(pilot).mission,ContractActionKind::ClaimReward,
                            0,glm::ivec3(0),NoRouteNode,RewardChoice::Credits});
                    }
                    AssertThat(world.worldObjects().size(),Equals(std::size_t(1)));
                }
                auto state = run.status(pilot);
                AssertThat(state.phase,Equals(ContractPhase::Complete));
                AssertThat(state.selectedNode,Equals(NoRouteNode));
                AssertThat(state.credits,Equals(earned));
                run.update();
                run.act(pilot,{++sequence,state.mission,ContractActionKind::Ready});
                AssertThat(run.status(pilot).result,Equals(ContractResult::WrongPhase));
                AssertThat(run.status(pilot).credits,Equals(earned));
                run.act(pilot,{++sequence,state.mission,ContractActionKind::NewRun});
                const auto restarted = run.status(pilot);
                AssertThat(restarted.phase,Equals(ContractPhase::Station));
                AssertThat(restarted.run,Equals(std::uint32_t(2)));
                AssertThat(restarted.attacksCleared,Equals(std::uint16_t(0)));
                AssertThat(restarted.credits,Equals(std::uint32_t(0)));
                AssertThat(restarted.viewActor != pilot,IsTrue());
                AssertThat(world.worldObject(restarted.viewActor) != nullptr,IsTrue());
            }
        });

        it("plays the contract station reward and upgrade loop for a solo pilot", []() {
            using namespace voxellancer::protocol;
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);
            const auto players = ContractRun::createPlayers(*world, 1);
            ContractRun run(*world, players);
            const auto pilot = players.front();
            std::uint64_t sequence = 0;
            const auto act = [&](ContractActionKind kind, std::uint8_t weapon = 0,
                    glm::ivec3 cell = glm::ivec3(0), std::uint8_t node = NoRouteNode) {
                run.act(pilot,{++sequence,run.status(pilot).mission,kind,weapon,cell,node});
            };
            std::uint32_t earned = 0;
            const auto finish = [&]() {
                earned += run.status(pilot).reward;
                for (Ship* enemy : world->ships())
                    if (world->entityId(enemy) != pilot) world->god().scheduleRemoval(enemy);
                world->god().remove();
                run.update();
                run.act(pilot,{++sequence,run.status(pilot).mission,ContractActionKind::ClaimReward,
                    0,glm::ivec3(0),NoRouteNode,RewardChoice::Credits});
            };
            const auto initialOffer = run.status(pilot).offer;
            act(ContractActionKind::BuyGun);
            AssertThat(run.status(pilot).result, Equals(ContractResult::InsufficientCredits));
            AssertThat(run.status(pilot).offer,Equals(initialOffer));
            act(ContractActionKind::SelectNode,0,glm::ivec3(0),2);
            act(ContractActionKind::Ready);
            AssertThat(run.inMission(), IsTrue());
            act(ContractActionKind::BuyShield);
            AssertThat(run.status(pilot).result, Equals(ContractResult::WrongPhase));
            finish();
            run.update();
            AssertThat(run.inMission(), IsFalse());
            AssertThat(run.status(pilot).credits, Equals(earned));
            AssertThat(run.status(pilot).phase, Equals(ContractPhase::Route));
            act(ContractActionKind::BuyGun);
            AssertThat(run.status(pilot).result,Equals(ContractResult::WrongPhase));
            const auto travel = [&](RouteNodeKind kind) {
                const auto state = run.status(pilot);
                int chosen = -1;
                for (std::size_t i = 0; i < state.route.size(); ++i)
                    if ((state.route[state.currentNode].links & (1u << i)) &&
                            state.route[i].kind == kind) {
                        chosen = static_cast<int>(i);
                        if (state.route[i].lane == 1) break;
                    }
                AssertThat(chosen >= 0,IsTrue());
                act(ContractActionKind::SelectNode,0,glm::ivec3(0),static_cast<std::uint8_t>(chosen));
                act(ContractActionKind::Ready);
            };
            travel(RouteNodeKind::Attack); finish();
            travel(RouteNodeKind::Attack); finish();
            auto& reactor = world->worldObject(pilot)->components();
            AssertThat(reactor.consumeWeaponEnergy(80),IsTrue());
            reactor.shieldSlots().front()->shield()->compensate(1000);
            travel(RouteNodeKind::Base);
            AssertThat(run.status(pilot).phase,Equals(ContractPhase::Station));
            AssertThat(run.status(pilot).weaponEnergy,Equals(100.0f));
            AssertThat(run.status(pilot).shieldHP,Equals(20000.0f));
            const auto offer = run.status(pilot).offer;
            const auto price = run.status(pilot).gunPrice;
            act(ContractActionKind::BuyGun);
            AssertThat(run.status(pilot).credits, Equals(earned-price));
            AssertThat(run.status(pilot).gun, IsTrue());
            AssertThat(run.status(pilot).inventory[offer-1],Equals(std::uint16_t(1)));
            auto* ship = static_cast<Ship*>(world->worldObject(pilot));
            for (const auto& slot : ship->components().hardpoints()) {
                AssertThat(slot->weapon()->equipmentKey(),Equals(std::string("pulselaser")));
            }
            auto slot = ship->components().hardpoints().front();
            const std::uint8_t previous = offer == 1 ? 2 : 1;
            slot->setWeapon(std::shared_ptr<Weapon>(WeaponBuilder(catalogWeapon(previous).key).build()));
            act(ContractActionKind::EquipGun,offer,slot->voxel()->cell());
            AssertThat(run.status(pilot).result,Equals(ContractResult::Accepted));
            AssertThat(slot->weapon()->equipmentKey(),Equals(catalogWeapon(offer).key));
            AssertThat(run.status(pilot).inventory[offer-1],Equals(std::uint16_t(0)));
            AssertThat(run.status(pilot).inventory[previous-1],Equals(std::uint16_t(1)));
            act(ContractActionKind::EquipGun,previous,glm::ivec3(255));
            AssertThat(run.status(pilot).result,Equals(ContractResult::InvalidSlot));
            AssertThat(run.status(pilot).inventory[previous-1],Equals(std::uint16_t(1)));
            run.act(pilot,{sequence,run.status(pilot).mission,ContractActionKind::BuyGun});
            AssertThat(run.status(pilot).result, Equals(ContractResult::StaleRequest));
            act(ContractActionKind::BuyGun);
            AssertThat(run.status(pilot).result, Equals(ContractResult::AlreadyOwned));
            act(ContractActionKind::BuyShield);
            AssertThat(run.status(pilot).shield, IsTrue());
            AssertThat(run.status(pilot).credits, Equals(earned-price-100));
            AssertThat(ship->components().shieldSlots().front()->shield()->maxHP(),
                Equals(30000.0f));
            act(ContractActionKind::Ready);
            act(ContractActionKind::EquipGun,previous,slot->voxel()->cell());
            AssertThat(run.status(pilot).result,Equals(ContractResult::WrongPhase));
            finish();
            AssertThat(run.status(pilot).gun,IsTrue());
            AssertThat(run.status(pilot).inventory[previous-1],Equals(std::uint16_t(1)));
            AssertThat(run.status(pilot).credits, Equals(earned-price-100));
        });

        it("synchronizes two contract pilots and keeps purchases personal", []() {
            using namespace voxellancer::protocol;
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);
            SimulationSession session(*world);
            const auto players = ContractRun::createPlayers(*world, 2);
            TcpServer server(*world, session, 0, 650.0f, std::nullopt, 2);
            server.enableContracts(players);
            TcpClient first;
            TcpClient second;
            AssertThat(first.connectLocal(server.port()), IsTrue());
            AssertThat(second.connectLocal(server.port()), IsTrue());
            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::seconds(2);
            while (server.clientCount() < players.size() &&
                std::chrono::steady_clock::now() < deadline) {
                server.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(server.clientCount(), Equals(players.size()));
            server.startWorldForPlayers(players);
            const auto exchange = [&]() {
                for (int i = 0; i < 12; ++i) {
                    session.advance(SimulationTickRunner::TickDurationSeconds);
                    server.poll();
                    AssertThat(first.poll(), IsTrue());
                    AssertThat(second.poll(), IsTrue());
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            };
            exchange();
            AssertThat(first.contractStatus().has_value(), IsTrue());
            AssertThat(second.contractStatus().has_value(), IsTrue());
            AssertThat(first.contractStatus()->invulnerable, IsTrue());
            AssertThat(second.contractStatus()->invulnerable, IsTrue());
            AssertThat(first.sendContractAction(ContractActionKind::DisableInvulnerability), IsTrue());
            exchange();
            AssertThat(first.contractStatus()->invulnerable, IsFalse());
            AssertThat(second.contractStatus()->invulnerable, IsTrue());
            InputCommand stationInput;
            stationInput.sequence = 1;
            stationInput.tick = first.snapshotTick() + 2;
            stationInput.moveZ = -32768;
            stationInput.buttons = FirePrimary;
            AssertThat(first.sendInput(stationInput), IsTrue());
            const auto stationPosition = world->worldObject(players[0])->position();
            exchange();
            AssertThat(world->worldObject(players[0])->position() == stationPosition, IsTrue());
            AssertThat(world->worldObjects().size(), Equals(std::size_t(2)));
            AssertThat(first.sendContractAction(ContractActionKind::Ready), IsTrue());
            exchange();
            AssertThat(first.contractStatus()->phase, Equals(ContractPhase::Station));
            AssertThat(second.contractStatus()->readyPlayers, Equals(std::uint8_t(1)));
            AssertThat(second.sendContractAction(ContractActionKind::SelectNode,0,glm::ivec3(0),2),IsTrue());
            exchange();
            AssertThat(first.contractStatus()->selectedNode,Equals(std::uint8_t(2)));
            AssertThat(first.contractStatus()->readyPlayers,Equals(std::uint8_t(0)));
            AssertThat(first.sendContractAction(ContractActionKind::Ready), IsTrue());
            AssertThat(second.sendContractAction(ContractActionKind::Ready), IsTrue());
            exchange();
            AssertThat(first.contractStatus()->phase, Equals(ContractPhase::Mission));
            AssertThat(first.contractStatus()->remaining, Equals(std::uint16_t(4)));
            for (const auto* client : {&first, &second}) {
                const auto& contacts = client->replicaState();
                AssertThat(std::count_if(contacts.begin(), contacts.end(),
                    [](const auto& contact) { return contact.hostile; }), Equals(4));
                for (const auto& contact : contacts) {
                    if (std::find(players.begin(), players.end(), contact.id) != players.end()) {
                        AssertThat(contact.hostile, IsFalse());
                    }
                }
            }
            AssertThat(first.sendContractAction(ContractActionKind::EnableInvulnerability), IsTrue());
            exchange();
            AssertThat(first.contractStatus()->invulnerable, IsTrue());
            for (Ship* ship : world->ships()) {
                if (std::find(players.begin(), players.end(), *world->entityId(ship)) == players.end()) {
                    world->god().scheduleRemoval(ship);
                }
            }
            world->god().remove();
            exchange();
            const auto claimCredits = [&]() {
                for (auto* client : {&first,&second})
                    AssertThat(client->sendContractAction(ContractActionKind::ClaimReward,0,
                        glm::ivec3(0),NoRouteNode,RewardChoice::Credits),IsTrue());
                exchange();
            };
            claimCredits();
            std::uint32_t earned = 150;
            AssertThat(first.contractStatus()->credits, Equals(earned));
            AssertThat(second.contractStatus()->credits, Equals(earned));
            AssertThat(first.contractStatus()->phase,Equals(ContractPhase::Route));
            AssertThat(std::none_of(first.replicaState().begin(), first.replicaState().end(),
                [](const auto& contact) { return contact.hostile; }), IsTrue());
            const auto travel = [&](RouteNodeKind kind) {
                const auto state = *first.contractStatus();
                int chosen = -1;
                for (std::size_t i = 0; i < state.route.size(); ++i)
                    if ((state.route[state.currentNode].links & (1u << i)) &&
                            state.route[i].kind == kind) {
                        chosen = static_cast<int>(i);
                        if (state.route[i].lane == 1) break;
                    }
                AssertThat(chosen >= 0,IsTrue());
                AssertThat(first.sendContractAction(ContractActionKind::SelectNode,0,glm::ivec3(0),
                    static_cast<std::uint8_t>(chosen)),IsTrue());
                exchange();
                AssertThat(second.contractStatus()->selectedNode,Equals(static_cast<std::uint8_t>(chosen)));
                AssertThat(first.sendContractAction(ContractActionKind::Ready),IsTrue());
                AssertThat(second.sendContractAction(ContractActionKind::Ready),IsTrue());
                exchange();
            };
            const auto finish = [&]() {
                earned += first.contractStatus()->reward;
                for (Ship* ship : world->ships())
                    if (std::find(players.begin(),players.end(),*world->entityId(ship)) == players.end())
                        world->god().scheduleRemoval(ship);
                world->god().remove();
                exchange();
                claimCredits();
            };
            travel(RouteNodeKind::Attack); finish();
            travel(RouteNodeKind::Attack); finish();
            travel(RouteNodeKind::Base);
            AssertThat(first.contractStatus()->phase,Equals(ContractPhase::Station));
            const auto offer = first.contractStatus()->offer;
            const auto price = first.contractStatus()->gunPrice;
            AssertThat(first.sendContractAction(ContractActionKind::BuyGun), IsTrue());
            exchange();
            AssertThat(first.contractStatus()->gun, IsTrue());
            AssertThat(first.contractStatus()->credits, Equals(earned-price));
            AssertThat(first.contractStatus()->inventory[offer-1],Equals(std::uint16_t(1)));
            AssertThat(second.contractStatus()->gun, IsFalse());
            AssertThat(second.contractStatus()->credits, Equals(earned));
            auto slot = world->worldObject(*first.actor())->components().hardpoints().front();
            const std::uint8_t previous = offer == 1 ? 2 : 1;
            slot->setWeapon(std::shared_ptr<Weapon>(WeaponBuilder(catalogWeapon(previous).key).build()));
            exchange();
            AssertThat(first.sendContractAction(ContractActionKind::EquipGun,offer,
                slot->voxel()->cell()),IsTrue());
            exchange();
            AssertThat(first.contractStatus()->result,Equals(ContractResult::Accepted));
            AssertThat(first.contractStatus()->inventory[offer-1],Equals(std::uint16_t(0)));
            AssertThat(first.contractStatus()->inventory[previous-1],Equals(std::uint16_t(1)));
            const auto otherInventory = second.contractStatus()->inventory;
            AssertThat(std::all_of(otherInventory.begin(),otherInventory.end(),
                [](auto count) { return count == 0; }),IsTrue());
            AssertThat(first.sendContractAction(ContractActionKind::BuyShield,0,glm::ivec3(0),
                NoRouteNode,RewardChoice::None,3),IsTrue());
            exchange();
            AssertThat(first.contractStatus()->shieldBuild,Equals(std::uint8_t(3)));
            AssertThat(first.contractStatus()->shieldCapacity,Equals(16000.0f));
            AssertThat(second.contractStatus()->shieldBuild,Equals(std::uint8_t(0)));
            AssertThat(second.contractStatus()->shieldCapacity,Equals(20000.0f));
            for (std::uint8_t build : {1,2}) {
                AssertThat(second.sendContractAction(ContractActionKind::BuyShield,0,glm::ivec3(0),
                    NoRouteNode,RewardChoice::None,build),IsTrue());
                exchange();
                AssertThat(second.contractStatus()->result,Equals(ContractResult::Accepted));
            }
            AssertThat(second.contractStatus()->shieldBuild,Equals(std::uint8_t(2)));
            AssertThat(second.contractStatus()->shieldCapacity,Equals(45000.0f));
            AssertThat(second.contractStatus()->credits,Equals(earned-250));
            AssertThat(first.contractStatus()->shieldCapacity,Equals(16000.0f));
            AssertThat(second.sendContractAction(ContractActionKind::BuyShield,0,glm::ivec3(0),
                NoRouteNode,RewardChoice::None,2),IsTrue());
            exchange();
            AssertThat(second.contractStatus()->result,Equals(ContractResult::AlreadyOwned));
            AssertThat(second.contractStatus()->credits,Equals(earned-250));
            AssertThat(first.sendContractAction(ContractActionKind::Ready), IsTrue());
            AssertThat(second.sendContractAction(ContractActionKind::Ready), IsTrue());
            exchange();
            AssertThat(first.contractStatus()->phase, Equals(ContractPhase::Mission));
            const auto& contents = first.entityContents();
            const auto own = std::find_if(contents.begin(), contents.end(),
                [&first](const auto& content) { return content.id == *first.actor(); });
            AssertThat(own != contents.end(), IsTrue());
            AssertThat(std::any_of(own->equipment.begin(), own->equipment.end(),
                [offer](const auto& item) { return item.equipmentKey == catalogWeapon(offer).key; }), IsTrue());
            InputCommand firing{2,session.tickIndex()+2};
            firing.buttons = FirePrimary;
            AssertThat(first.sendInput(firing),IsTrue());
            exchange();
            AssertThat(first.contractStatus()->weaponEnergy < 100.0f,IsTrue());
            AssertThat(first.contractStatus()->weaponEnergy >= 0.0f,IsTrue());
            AssertThat(second.contractStatus()->weaponEnergy,Equals(100.0f));
            for (const auto actor : players) world->god().scheduleRemoval(world->worldObject(actor));
            world->god().remove();
            exchange();
            AssertThat(first.contractStatus()->phase,Equals(ContractPhase::Defeat));
            AssertThat(first.sendContractAction(ContractActionKind::NewRun),IsTrue());
            AssertThat(second.sendContractAction(ContractActionKind::NewRun),IsTrue());
            exchange();
            for (const auto* client : {&first,&second}) {
                AssertThat(client->contractStatus()->phase,Equals(ContractPhase::Station));
                AssertThat(client->contractStatus()->shieldBuild,Equals(std::uint8_t(0)));
                AssertThat(client->contractStatus()->shieldCapacity,Equals(20000.0f));
                AssertThat(client->contractStatus()->weaponEnergy,Equals(100.0f));
            }
        });

        it("makes contract rewards personal exclusive and once-only and applies both run upgrades", []() {
            using namespace voxellancer::protocol;
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            auto& world = *World::instance(World::Mode::Server);
            const auto players = ContractRun::createPlayers(world,2);
            ContractRun run(world,players);
            std::uint64_t sequences[]{0,0};
            const auto act = [&](int player, ContractActionKind kind, RewardChoice choice = RewardChoice::None) {
                run.act(players[player],{++sequences[player],run.status(players[player]).mission,
                    kind,0,glm::ivec3(0),NoRouteNode,choice});
            };
            const auto shield = world.worldObject(players[0])->components().shieldSlots().front()->shield();
            const float initialCapacity = shield->maxHP();
            shield->setHP(initialCapacity*0.25f);
            for (int step = 0; step < 2; ++step) {
                const auto route = run.status(players[0]);
                std::uint8_t attack = NoRouteNode;
                for (std::size_t i = 0; i < route.route.size(); ++i)
                    if ((route.route[route.currentNode].links & (1u << i)) &&
                        route.route[i].kind == RouteNodeKind::Attack &&
                        (attack == NoRouteNode || route.route[i].lane == 1))
                        attack = static_cast<std::uint8_t>(i);
                AssertThat(attack != NoRouteNode,IsTrue());
                run.act(players[0],{++sequences[0],route.mission,ContractActionKind::SelectNode,
                    0,glm::ivec3(0),attack});
                act(0,ContractActionKind::Ready);
                act(1,ContractActionKind::Ready);
                for (auto* object : world.ships())
                    if (run.isEnemy(*world.entityId(object))) world.god().scheduleRemoval(object);
                world.god().remove();
                run.update();
                const auto reward = run.status(players[0]);
                AssertThat(reward.phase,Equals(ContractPhase::Reward));
                AssertThat(reward.credits,Equals(std::uint32_t(0)));
                act(0,ContractActionKind::Ready);
                AssertThat(run.status(players[0]).result,Equals(ContractResult::WrongPhase));
                act(0,ContractActionKind::ClaimReward,RewardChoice::Upgrade);
                const auto upgraded = run.status(players[0]);
                AssertThat(upgraded.pendingRewards,Equals(std::uint8_t(1)));
                AssertThat(upgraded.rewardClaimed,IsTrue());
                AssertThat(upgraded.phase,Equals(ContractPhase::Reward));
                const auto upgradeCount = upgraded.damageUpgrades+upgraded.shieldUpgrades;
                act(0,ContractActionKind::ClaimReward,RewardChoice::Credits);
                AssertThat(run.status(players[0]).result,Equals(ContractResult::RewardAlreadyClaimed));
                AssertThat(run.status(players[0]).credits,Equals(std::uint32_t(0)));
                AssertThat(run.status(players[0]).damageUpgrades+run.status(players[0]).shieldUpgrades,
                    Equals(upgradeCount));
                const auto weapon = run.status(players[1]).rewardWeapon;
                act(1,ContractActionKind::ClaimReward,RewardChoice::Weapon);
                AssertThat(run.status(players[1]).inventory[weapon-1] > 0,IsTrue());
                AssertThat(run.status(players[1]).weaponsFound,Equals(static_cast<std::uint16_t>(step+1)));
                AssertThat(run.status(players[1]).damageUpgrades,Equals(std::uint8_t(0)));
                AssertThat(run.status(players[0]).phase,Equals(ContractPhase::Route));
            }
            auto state = run.status(players[0]);
            AssertThat(state.damageUpgrades,Equals(std::uint8_t(1)));
            AssertThat(state.shieldUpgrades,Equals(std::uint8_t(1)));
            AssertThat(std::abs(shield->maxHP() - initialCapacity*1.2f) < 0.01f,IsTrue());
            AssertThat(std::abs(shield->hp()/shield->maxHP()-0.25f) < 0.0001f,IsTrue());
            auto* ship = world.worldObject(players[0]);
            auto slot = ship->components().hardpoints().front();
            auto* target = blockTarget(world,1,true);
            target->updateTransformAndGeode(target->position()+slot->voxel()->position()+
                glm::vec3(0,0,-80)-target->voxel(glm::ivec3(10,10,20))->position(),glm::quat(1,0,0,0));
            auto* gun = static_cast<Gun*>(slot->weapon().get());
            gun->fireAtPoint(slot->voxel()->position()+glm::vec3(0,0,-100),false);
            world.god().spawn();
            int projectiles = 0;
            for (auto* object : world.worldObjects()) {
                if (object->objectType() != WorldObjectType::Bullet) continue;
                ++projectiles;
                AssertThat(std::abs(static_cast<Bullet*>(object)->impactBudget-1.15f) < 0.0001f,IsTrue());
            }
            AssertThat(projectiles > 0,IsTrue());
            Mover(world).moveProfiledProjectiles(0.3f);
            world.god().remove();
            AssertThat(std::abs(target->voxel(glm::ivec3(10,10,20))->hp()-127.0f) < 0.001f,IsTrue());
            for (auto player : players) world.god().scheduleRemoval(world.worldObject(player));
            world.god().remove();
            run.update();
            state = run.status(players[0]);
            AssertThat(state.phase,Equals(ContractPhase::Defeat));
            act(0,ContractActionKind::DisableInvulnerability);
            act(0,ContractActionKind::NewRun);
            AssertThat(run.status(players[0]).phase,Equals(ContractPhase::Defeat));
            act(1,ContractActionKind::NewRun);
            const auto fresh = run.status(players[0]);
            AssertThat(fresh.phase,Equals(ContractPhase::Station));
            AssertThat(fresh.alive,IsTrue());
            AssertThat(fresh.invulnerable,IsFalse());
            AssertThat(fresh.damageUpgrades+fresh.shieldUpgrades,Equals(0));
            AssertThat(run.status(players[1]).weaponsFound,Equals(std::uint16_t(0)));
            const auto inventory = run.status(players[1]).inventory;
            AssertThat(std::all_of(inventory.begin(),inventory.end(),[](auto n) { return n == 0; }),IsTrue());
            AssertThat(world.worldObject(fresh.viewActor)->info().weaponDamageMultiplier(),Equals(1.0f));
            AssertThat(world.worldObject(fresh.viewActor)->components().shieldSlots().front()->shield()->maxHP(),
                Equals(initialCapacity));
            run.act(players[0],{++sequences[0],state.mission,ContractActionKind::NewRun});
            AssertThat(run.status(players[0]).result,Equals(ContractResult::StaleRequest));
            AssertThat(run.status(players[0]).run,Equals(std::uint32_t(2)));
        });

        it("keeps dead contract pilots connected through bases and restarts a wiped party on the same sockets", []() {
            using namespace voxellancer::protocol;
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            auto& world = *World::instance(World::Mode::Server);
            SimulationSession session(world);
            const auto players = ContractRun::createPlayers(world,2);
            TcpServer server(world,session,0,650.0f,std::nullopt,2);
            server.enableContracts(players);
            TcpClient first, second;
            AssertThat(first.connectLocal(server.port()),IsTrue());
            AssertThat(second.connectLocal(server.port()),IsTrue());
            const auto deadline = std::chrono::steady_clock::now()+std::chrono::seconds(2);
            while (server.clientCount() < 2 && std::chrono::steady_clock::now() < deadline) {
                server.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(server.clientCount(),Equals(std::size_t(2)));
            server.startWorldForPlayers(players);
            const auto exchange = [&]() {
                for (int i = 0; i < 12; ++i) {
                    session.advance(SimulationTickRunner::TickDurationSeconds);
                    server.poll();
                    AssertThat(first.poll(),IsTrue());
                    AssertThat(second.poll(),IsTrue());
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            };
            exchange();
            AssertThat(first.sendContractAction(ContractActionKind::SelectNode,0,glm::ivec3(0),2),IsTrue());
            exchange();
            for (auto* client : {&first,&second})
                AssertThat(client->sendContractAction(ContractActionKind::Ready),IsTrue());
            exchange();
            world.god().scheduleRemoval(world.worldObject(players[0]));
            world.god().remove();
            exchange();
            AssertThat(first.contractStatus()->phase,Equals(ContractPhase::Mission));
            AssertThat(first.contractStatus()->alive,IsFalse());
            AssertThat(first.contractStatus()->viewActor,Equals(players[1]));
            AssertThat(server.clientCount(),Equals(std::size_t(2)));
            InputCommand spectatorInput{1,first.snapshotTick()+2};
            spectatorInput.moveX = 32767;
            AssertThat(first.sendInput(spectatorInput),IsTrue());
            exchange();
            const auto rejected = first.drainAcknowledgements();
            AssertThat(std::any_of(rejected.begin(),rejected.end(),[](const auto& ack) {
                return ack.sequence == 1 && ack.result == InputCommandResult::Invalid;
            }),IsTrue());
            for (auto* ship : world.ships())
                if (world.entityId(ship) != players[1]) world.god().scheduleRemoval(ship);
            world.god().remove();
            exchange();
            AssertThat(first.contractStatus()->phase,Equals(ContractPhase::Reward));
            AssertThat(first.contractStatus()->pendingRewards,Equals(std::uint8_t(1)));
            AssertThat(first.sendContractAction(ContractActionKind::ClaimReward,0,glm::ivec3(0),
                NoRouteNode,RewardChoice::Credits),IsTrue());
            exchange();
            AssertThat(first.contractStatus()->result,Equals(ContractResult::Unavailable));
            AssertThat(second.sendContractAction(ContractActionKind::ClaimReward,0,glm::ivec3(0),
                NoRouteNode,RewardChoice::Credits),IsTrue());
            exchange();
            auto state = *second.contractStatus();
            std::uint8_t base = NoRouteNode;
            for (std::size_t i = 0; i < state.route.size(); ++i)
                if ((state.route[state.currentNode].links & (1u << i)) &&
                    state.route[i].kind == RouteNodeKind::Base) base = static_cast<std::uint8_t>(i);
            AssertThat(base != NoRouteNode,IsTrue());
            AssertThat(second.sendContractAction(ContractActionKind::SelectNode,0,glm::ivec3(0),base),IsTrue());
            exchange();
            AssertThat(second.sendContractAction(ContractActionKind::Ready),IsTrue());
            exchange();
            AssertThat(first.contractStatus()->phase,Equals(ContractPhase::Station));
            AssertThat(first.contractStatus()->alive,IsFalse());
            AssertThat(world.worldObject(players[0]) == nullptr,IsTrue());
            AssertThat(first.contractStatus()->credits,Equals(std::uint32_t(0)));
            AssertThat(second.sendContractAction(ContractActionKind::Ready),IsTrue());
            exchange();
            AssertThat(second.contractStatus()->phase,Equals(ContractPhase::Mission));
            world.god().scheduleRemoval(world.worldObject(players[1]));
            world.god().remove();
            exchange();
            AssertThat(first.contractStatus()->phase,Equals(ContractPhase::Defeat));
            AssertThat(first.contractStatus()->viewActor.valid(),IsFalse());
            AssertThat(first.sendContractAction(ContractActionKind::NewRun),IsTrue());
            exchange();
            AssertThat(first.contractStatus()->readyPlayers,Equals(std::uint8_t(1)));
            AssertThat(second.contractStatus()->phase,Equals(ContractPhase::Defeat));
            AssertThat(second.sendContractAction(ContractActionKind::NewRun),IsTrue());
            exchange();
            AssertThat(server.clientCount(),Equals(std::size_t(2)));
            for (auto* client : {&first,&second}) {
                AssertThat(client->contractStatus()->run,Equals(std::uint32_t(2)));
                AssertThat(client->contractStatus()->alive,IsTrue());
                AssertThat(client->contractStatus()->phase,Equals(ContractPhase::Station));
                const auto actor = client->contractStatus()->viewActor;
                AssertThat(actor != *client->actor(),IsTrue());
                AssertThat(std::any_of(client->replicaState().begin(),client->replicaState().end(),
                    [actor](const auto& snapshot) { return snapshot.id == actor; }),IsTrue());
                AssertThat(client->sendContractAction(ContractActionKind::Ready),IsTrue());
            }
            exchange();
            const auto actor = first.contractStatus()->viewActor;
            const auto origin = world.worldObject(actor)->position();
            InputCommand input{2,session.tickIndex()+2};
            input.moveX = 32767;
            AssertThat(first.sendInput(input),IsTrue());
            exchange();
            AssertThat(world.worldObject(actor)->position().x > origin.x,IsTrue());
            const auto accepted = first.drainAcknowledgements();
            AssertThat(std::any_of(accepted.begin(),accepted.end(),[](const auto& ack) {
                return ack.sequence == 2 && ack.result == InputCommandResult::Accepted;
            }),IsTrue());
        });

        it("ends a solo contract on death and starts a fresh run without a revive action", []() {
            using namespace voxellancer::protocol;
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            auto& world = *World::instance(World::Mode::Server);
            const auto players = ContractRun::createPlayers(world,1);
            ContractRun run(world,players);
            SimulationSession session(world);
            run.act(players[0],{1,0,ContractActionKind::Ready});
            session.advance(SimulationTickRunner::TickDurationSeconds);
            world.god().scheduleRemoval(world.worldObject(players[0]));
            world.god().remove();
            run.update();
            const auto lost = run.status(players[0]);
            AssertThat(lost.phase,Equals(ContractPhase::Defeat));
            AssertThat(lost.alive,IsFalse());
            AssertThat(lost.remaining,Equals(std::uint16_t(0)));
            AssertThat(lost.elapsedTicks,Equals(std::uint64_t(1)));
            session.advance(SimulationTickRunner::TickDurationSeconds);
            run.update();
            AssertThat(run.status(players[0]).elapsedTicks,Equals(lost.elapsedTicks));
            run.act(players[0],{2,lost.mission,ContractActionKind::Ready});
            AssertThat(run.status(players[0]).result,Equals(ContractResult::Unavailable));
            run.act(players[0],{3,lost.mission,ContractActionKind::NewRun});
            const auto fresh = run.status(players[0]);
            AssertThat(fresh.phase,Equals(ContractPhase::Station));
            AssertThat(fresh.alive,IsTrue());
            AssertThat(fresh.run,Equals(std::uint32_t(2)));
            AssertThat(fresh.creditsEarned,Equals(std::uint32_t(0)));
            AssertThat(fresh.elapsedTicks,Equals(std::uint64_t(0)));
            AssertThat(world.worldObjects().size(),Equals(std::size_t(1)));
        });

        it("continues a contract after a pilot disconnects without paying premature rewards", []() {
            using namespace voxellancer::protocol;
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);
            const auto players = ContractRun::createPlayers(*world, 2);
            ContractRun run(*world, players);
            run.act(players[0], {1, 0, ContractActionKind::Ready});
            run.act(players[1], {1, 0, ContractActionKind::Ready});
            run.removePlayer(players[1]);
            run.update();
            AssertThat(run.inMission(), IsTrue());
            AssertThat(run.status(players[0]).credits, Equals(std::uint32_t(0)));
            AssertThat(run.status(players[0]).ready, IsFalse());
            AssertThat(run.status(players[0]).remaining, Equals(std::uint16_t(2)));
        });

        it("completes a contract through authoritative combat damage", []() {
            using namespace voxellancer::protocol;
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);
            SimulationSession session(*world);
            const auto players = ContractRun::createPlayers(*world, 1);
            ContractRun run(*world, players);
            run.act(players.front(), {1, 0, ContractActionKind::Ready});
            Ship* enemy = nullptr;
            for (Ship* ship : world->ships()) {
                if (world->entityId(ship) != players.front()) {
                    enemy = ship;
                }
            }
            AssertThat(enemy != nullptr, IsTrue());
            enemy->character()->setTask(nullptr);
            InputCommand input;
            input.sequence = 1;
            input.tick = 1;
            input.buttons = FirePrimary | FireSecondary;
            input.target = *world->entityId(enemy);
            input.firePoint = enemy->crucialVoxel()->position() -
                world->worldObject(players.front())->position();
            AssertThat(session.submitInputCommand(players.front(), input),
                Equals(InputCommandResult::Queued));
            for (int tick = 0; tick < 1800 && run.inMission(); ++tick) {
                session.advance(SimulationTickRunner::TickDurationSeconds);
                run.update();
            }
            AssertThat(run.inMission(), IsFalse());
            AssertThat(run.status(players.front()).phase,Equals(ContractPhase::Reward));
            AssertThat(run.status(players.front()).credits, Equals(std::uint32_t(0)));
            AssertThat(run.status(players.front()).result, Equals(ContractResult::MissionCompleted));
            run.act(players.front(),{2,run.status(players.front()).mission,ContractActionKind::ClaimReward,
                0,glm::ivec3(0),NoRouteNode,RewardChoice::Credits});
            AssertThat(run.status(players.front()).credits,Equals(std::uint32_t(100)));
        });
        it("assigns a local battle client to the scenario player ship", []() {
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);
            SimulationSession session(*world);
            BattleScenario scenario(*world);
            scenario.load();

            const auto isPlayerTarget = [&scenario](const auto& target) {
                return target.get() == scenario.playerShip();
            };
            std::size_t fleetShipsTargetingPlayer = 0;
            for (Ship* ship : world->ships()) {
                if (ship == scenario.playerShip()) {
                    continue;
                }
                const auto task = std::dynamic_pointer_cast<FightTask>(
                    ship->character()->task());
                if (task && std::any_of(
                        task->targets().begin(),
                        task->targets().end(),
                        isPlayerTarget)) {
                    ++fleetShipsTargetingPlayer;
                }
            }
            AssertThat(fleetShipsTargetingPlayer, Equals(static_cast<std::size_t>(6)));

            const auto playerEntity = world->entityId(scenario.playerShip());
            AssertThat(playerEntity.has_value(), IsTrue());

            TcpServer server(
                *world,
                session,
                0,
                650.0f,
                playerEntity);
            TcpClient client;
            AssertThat(client.connectLocal(server.port()), IsTrue());
            server.poll();
            AssertThat(client.poll(), IsTrue());
            AssertThat(client.actor().has_value(), IsTrue());
            AssertThat(client.actor()->index, Equals(playerEntity->index));
            AssertThat(
                client.actor()->generation,
                Equals(playerEntity->generation));
        });
        it("waits for the target lobby before assigning player ships", []() {
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);
            SimulationSession session(*world);
            TcpServer server(*world, session, 0, 250.0f, std::nullopt, 2);
            TcpClient firstClient;
            TcpClient secondClient;
            AssertThat(firstClient.connectLocal(server.port()), IsTrue());
            AssertThat(secondClient.connectLocal(server.port()), IsTrue());

            server.poll();
            AssertThat(firstClient.poll(), IsTrue());
            AssertThat(secondClient.poll(), IsTrue());
            AssertThat(server.clientCount(), Equals(static_cast<std::size_t>(2)));
            AssertThat(server.worldStarted(), IsFalse());
            AssertThat(session.tickIndex(), Equals(static_cast<std::uint64_t>(0)));
            AssertThat(world->worldObjects().empty(), IsTrue());
            AssertThat(firstClient.actor().has_value(), IsFalse());
            AssertThat(secondClient.actor().has_value(), IsFalse());
            AssertThat(firstClient.lobbyStatus().has_value(), IsTrue());
            AssertThat(firstClient.lobbyStatus()->connectedPlayers,
                Equals(static_cast<std::uint16_t>(2)));
            AssertThat(firstClient.lobbyStatus()->targetPlayers,
                Equals(static_cast<std::uint16_t>(2)));
            AssertThat(firstClient.lobbyStatus()->worldStarted, IsFalse());
            bool rejectedEarlyStart = false;
            try {
                server.startWorldForPlayers({});
            } catch (const std::logic_error&) {
                rejectedEarlyStart = true;
            }
            AssertThat(rejectedEarlyStart, IsTrue());
            AssertThat(world->worldObjects().empty(), IsTrue());
            AssertThat(session.tickIndex(), Equals(static_cast<std::uint64_t>(0)));

            Ship* firstShip = WorldObjectBuilder("mox", *world).buildShip();
            firstShip->transform().setPosition(glm::vec3(0.0f, 0.0f, 0.0f));
            Ship* secondShip = WorldObjectBuilder("mox", *world).buildShip();
            secondShip->transform().setPosition(glm::vec3(1000.0f, 0.0f, 0.0f));
            Ship* nearFirstShip = WorldObjectBuilder("mox", *world).buildShip();
            nearFirstShip->transform().setPosition(glm::vec3(100.0f, 0.0f, 0.0f));
            Ship* nearSecondShip = WorldObjectBuilder("mox", *world).buildShip();
            nearSecondShip->transform().setPosition(glm::vec3(900.0f, 0.0f, 0.0f));
            Ship* distantShip = WorldObjectBuilder("mox", *world).buildShip();
            distantShip->transform().setPosition(glm::vec3(5000.0f, 0.0f, 0.0f));
            world->god().scheduleSpawn(firstShip);
            world->god().scheduleSpawn(secondShip);
            world->god().scheduleSpawn(nearFirstShip);
            world->god().scheduleSpawn(nearSecondShip);
            world->god().scheduleSpawn(distantShip);
            world->god().spawn();
            const auto firstActor = world->entityId(firstShip);
            const auto secondActor = world->entityId(secondShip);
            const auto nearFirst = world->entityId(nearFirstShip);
            const auto nearSecond = world->entityId(nearSecondShip);
            const auto distant = world->entityId(distantShip);
            AssertThat(firstActor.has_value(), IsTrue());
            AssertThat(secondActor.has_value(), IsTrue());
            AssertThat(nearFirst.has_value(), IsTrue());
            AssertThat(nearSecond.has_value(), IsTrue());
            AssertThat(distant.has_value(), IsTrue());
            server.startWorldForPlayers({*firstActor, *secondActor});
            server.poll();
            AssertThat(firstClient.poll(), IsTrue());
            AssertThat(secondClient.poll(), IsTrue());

            AssertThat(server.worldStarted(), IsTrue());
            AssertThat(firstClient.actor().has_value(), IsTrue());
            AssertThat(secondClient.actor().has_value(), IsTrue());
            AssertThat(*firstClient.actor(), Equals(*firstActor));
            AssertThat(*secondClient.actor(), Equals(*secondActor));
            AssertThat(firstClient.lobbyStatus()->worldStarted, IsTrue());
            AssertThat(secondClient.lobbyStatus()->worldStarted, IsTrue());
            AssertThat(session.tickIndex(), Equals(static_cast<std::uint64_t>(0)));

            for (int tick = 0; tick < 3; ++tick) {
                session.advance(SimulationTickRunner::TickDurationSeconds);
                server.poll();
            }
            AssertThat(firstClient.poll(), IsTrue());
            AssertThat(secondClient.poll(), IsTrue());
            const auto containsEntity = [](
                const std::vector<voxellancer::protocol::EntitySnapshot>& state,
                voxellancer::protocol::EntityId entity) {
                return std::any_of(
                    state.begin(),
                    state.end(),
                    [entity](const auto& snapshot) {
                        return snapshot.id == entity;
                    });
            };
            const auto& firstState = firstClient.replicaState();
            const auto& secondState = secondClient.replicaState();
            AssertThat(containsEntity(firstState, *firstActor), IsTrue());
            AssertThat(containsEntity(firstState, *nearFirst), IsTrue());
            AssertThat(containsEntity(firstState, *secondActor), IsFalse());
            AssertThat(containsEntity(firstState, *nearSecond), IsFalse());
            AssertThat(containsEntity(firstState, *distant), IsFalse());
            AssertThat(containsEntity(secondState, *secondActor), IsTrue());
            AssertThat(containsEntity(secondState, *nearSecond), IsTrue());
            AssertThat(containsEntity(secondState, *firstActor), IsFalse());
            AssertThat(containsEntity(secondState, *nearFirst), IsFalse());
            AssertThat(containsEntity(secondState, *distant), IsFalse());
        });
        it("requires an owning world when building rockets", []() {
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);

            bool rejectedWorldlessBuild = false;
            try {
                WorldObjectBuilder("moxtar").buildRocket();
            } catch (const std::logic_error&) {
                rejectedWorldlessBuild = true;
            }
            AssertThat(rejectedWorldlessBuild, IsTrue());

            std::unique_ptr<Rocket> rocket(
                WorldObjectBuilder("moxtar", *world).buildRocket()
            );
            AssertThat(rocket->world() == world, IsTrue());
        });

        it("replicates snapshots, applies input, and removes disconnected actors", []() {
            PropertyManager::instance()->load("data/config.ini");
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);

            Ship* ship = new Ship(*world);
            ship->transform().setScale(2.0f);
            ship->addVoxel(new Voxel(glm::ivec3(0), 0xffffff));
            world->god().scheduleSpawn(ship);
            world->god().spawn();

            SimulationSession session(*world);
            TcpServer server(*world, session, 0);
            TcpClient client;
            AssertThat(client.connectLocal(server.port()), IsTrue());
            for (int attempt = 0;
                 attempt < 100 &&
                    (!client.actor() || client.snapshotSequence() == 0);
                 ++attempt) {
                server.poll();
                AssertThat(client.poll(), IsTrue());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            AssertThat(server.clientCount(), Equals(static_cast<std::size_t>(1)));
            AssertThat(client.actor().has_value(), IsTrue());
            AssertThat(client.snapshotSequence() > 0, IsTrue());
            AssertThat(*client.actor(), Equals(*world->entityId(ship)));
            AssertThat(client.replicaState().size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(client.replicaState().front().scale, Equals(2.0f));
            AssertThat(client.entityContents().size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(client.entityContents().front().voxels.size(),
                Equals(static_cast<std::size_t>(1)));
            const auto statistics = server.clientStatistics();
            AssertThat(statistics.size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(statistics.front().bytesSent > 0, IsTrue());
            AssertThat(statistics.front().visibleEntityCount,
                Equals(static_cast<std::size_t>(1)));
            const std::uint64_t beforeResyncSequence = client.snapshotSequence();
            AssertThat(client.requestSnapshotResync(), IsTrue());
            AssertThat(client.poll(), IsTrue());
            server.poll();
            AssertThat(client.poll(), IsTrue());
            AssertThat(client.snapshotSequence() > beforeResyncSequence, IsTrue());
            AssertThat(client.replicaState().size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(client.entityContents().size(), Equals(static_cast<std::size_t>(1)));
            const std::uint64_t initialSnapshotSequence = client.snapshotSequence();

            const std::uint64_t commandTick = client.snapshotTick() + 4;
            const voxellancer::protocol::InputCommand command{
                1, commandTick, 8192
            };
            AssertThat(client.sendInput(command), IsTrue());

            bool inputAccepted = false;
            for (int attempt = 0; attempt < 100 && !inputAccepted; ++attempt) {
                AssertThat(client.poll(), IsTrue());
                server.poll();
                session.advance(SimulationTickRunner::TickDurationSeconds);
                server.poll();
                AssertThat(client.poll(), IsTrue());
                for (const auto& acknowledgement : client.drainAcknowledgements()) {
                    if (acknowledgement.sequence == command.sequence) {
                        inputAccepted = acknowledgement.result ==
                            voxellancer::protocol::InputCommandResult::Accepted;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(inputAccepted, IsTrue());

            for (int tick = 0;
                 tick < 30 &&
                    (client.snapshotSequence() <= initialSnapshotSequence ||
                      client.snapshotTick() <= commandTick);
                 ++tick) {
                session.advance(SimulationTickRunner::TickDurationSeconds);
                server.poll();
                AssertThat(client.poll(), IsTrue());
            }
            AssertThat(client.snapshotSequence() > initialSnapshotSequence, IsTrue());
            AssertThat(client.snapshotTick() > commandTick, IsTrue());
            AssertThat(client.replicaState().size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(
                ship->components().engineState().directional().x,
                EqualsWithDelta(8192.0f / 32767.0f, 1e-6f)
            );
            AssertThat(
                client.replicaState().front().engineState.x,
                EqualsWithDelta(
                    ship->components().engineState().directional().x,
                    1.0f / 127.0f));

            client.disconnect();
            for (int attempt = 0; attempt < 100 && server.clientCount() != 0; ++attempt) {
                server.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(server.clientCount(), Equals(static_cast<std::size_t>(0)));
            session.advance(SimulationTickRunner::TickDurationSeconds);
            AssertThat(world->worldObject(*client.actor()) == nullptr, IsTrue());
        });

        it("rejects input that arrives after its simulation tick", []() {
            PropertyManager::instance()->load("data/config.ini");
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);

            Ship* ship = new Ship(*world);
            ship->addVoxel(new Voxel(glm::ivec3(0), 0xffffff));
            world->god().scheduleSpawn(ship);
            world->god().spawn();

            SimulationSession session(*world);
            TcpServer server(*world, session, 0);
            TcpClient client;
            AssertThat(client.connectLocal(server.port()), IsTrue());
            server.poll();
            AssertThat(client.poll(), IsTrue());
            AssertThat(client.actor().has_value(), IsTrue());

            const voxellancer::protocol::InputCommand lateCommand{
                1, session.tickIndex() + 1, 8192
            };
            AssertThat(client.sendInput(lateCommand), IsTrue());
            AssertThat(client.poll(), IsTrue());
            for (int tick = 0; tick < 5; ++tick) {
                session.advance(SimulationTickRunner::TickDurationSeconds);
            }

            server.poll();
            session.advance(SimulationTickRunner::TickDurationSeconds);
            server.poll();
            AssertThat(client.poll(), IsTrue());
            const auto acknowledgements = client.drainAcknowledgements();
            AssertThat(acknowledgements.size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(acknowledgements.front().sequence, Equals(lateCommand.sequence));
            AssertThat(
                acknowledgements.front().result,
                Equals(voxellancer::protocol::InputCommandResult::TooLate)
            );
            client.disconnect();
        });

        it("applies accepted future input only at its scheduled tick", []() {
            PropertyManager::instance()->load("data/config.ini");
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);

            Ship* ship = new Ship(*world);
            ship->addVoxel(new Voxel(glm::ivec3(0), 0xffffff));
            world->god().scheduleSpawn(ship);
            world->god().spawn();

            SimulationSession session(*world);
            TcpServer server(*world, session, 0);
            TcpClient client;
            AssertThat(client.connectLocal(server.port()), IsTrue());
            for (int attempt = 0;
                 attempt < 100 &&
                    (!client.actor() || client.snapshotSequence() == 0);
                 ++attempt) {
                server.poll();
                AssertThat(client.poll(), IsTrue());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(client.actor().has_value(), IsTrue());

            voxellancer::protocol::InputCommand command;
            command.sequence = 1;
            command.tick = session.tickIndex() + 20;
            command.moveX = 8192;
            AssertThat(client.sendInput(command), IsTrue());

            bool inputAccepted = false;
            for (int attempt = 0; attempt < 100 && !inputAccepted; ++attempt) {
                server.poll();
                session.advance(SimulationTickRunner::TickDurationSeconds);
                server.poll();
                AssertThat(client.poll(), IsTrue());
                for (const auto& acknowledgement : client.drainAcknowledgements()) {
                    if (acknowledgement.sequence == command.sequence) {
                        inputAccepted = acknowledgement.result ==
                            voxellancer::protocol::InputCommandResult::Accepted;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(inputAccepted, IsTrue());
            AssertThat(session.tickIndex() < command.tick, IsTrue());
            AssertThat(
                ship->components().engineState().directional().x,
                Equals(0.0f));

            while (session.tickIndex() <= command.tick) {
                const bool scheduledTick = session.tickIndex() == command.tick;
                session.advance(SimulationTickRunner::TickDurationSeconds);
                server.poll();
                AssertThat(client.poll(), IsTrue());
                if (!scheduledTick) {
                    AssertThat(
                        ship->components().engineState().directional().x,
                        Equals(0.0f));
                }
            }
            AssertThat(
                ship->components().engineState().directional().x,
                EqualsWithDelta(8192.0f / 32767.0f, 1e-6f));
        });

        it("predicts the same collision-free movement as the server", []() {
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);

            Ship* ship =
                WorldObjectBuilder("basicship", *world).buildShip();
            ship->setAiUpdatePhase(static_cast<std::uint32_t>(
                (world->simulationTickIndex() + 4) % 6));
            world->god().scheduleSpawn(ship);
            world->god().spawn();

            SimulationSession session(*world);
            const auto entity = world->entityId(ship);
            AssertThat(entity.has_value(), IsTrue());
            std::vector<voxellancer::protocol::EntitySnapshot> initialState;
            AssertThat(session.createSnapshotState(initialState), IsTrue());
            const auto initialActor = std::find_if(
                initialState.begin(),
                initialState.end(),
                [entity](const auto& snapshot) {
                    return snapshot.id == *entity;
                });
            AssertThat(initialActor != initialState.end(), IsTrue());

            voxellancer::protocol::InputCommand command;
            command.sequence = 1;
            command.tick = session.tickIndex() + 2;
            command.moveX = 16384;
            command.moveZ = -32768;
            AssertThat(
                session.submitInputCommand(*entity, command),
                Equals(voxellancer::protocol::InputCommandResult::Queued));
            const auto predicted = InputPredictor().predict(
                *initialActor,
                session.tickIndex(),
                command.tick + 2,
                {command},
                ship->components().enginePower(),
                Property<float>(
                    "physics.globalDirectionalDampening").get(),
                Property<float>(
                    "physics.globalAngularDampening").get());
            AssertThat(predicted.has_value(), IsTrue());

            while (session.tickIndex() < command.tick + 2) {
                session.advance(SimulationTickRunner::TickDurationSeconds);
            }
            std::vector<voxellancer::protocol::EntitySnapshot> serverState;
            AssertThat(session.createSnapshotState(serverState), IsTrue());
            const auto serverActor = std::find_if(
                serverState.begin(),
                serverState.end(),
                [entity](const auto& snapshot) {
                    return snapshot.id == *entity;
                });
            AssertThat(serverActor != serverState.end(), IsTrue());
            AssertThat(
                glm::length(serverActor->position - predicted->position),
                EqualsWithDelta(0.0f, 1e-4f));
            AssertThat(
                glm::length(
                    serverActor->directionalVelocity -
                    predicted->directionalVelocity),
                EqualsWithDelta(0.0f, 1e-4f));
            AssertThat(
                glm::length(serverActor->engineState - predicted->engineState),
                EqualsWithDelta(0.0f, 1e-6f));
            AssertThat(
                std::abs(glm::dot(
                    serverActor->orientation,
                    predicted->orientation)),
                EqualsWithDelta(1.0f, 1e-4f));
        });

        it("rebases local prediction after an authoritative ship collision", []() {
            PropertyManager::instance()->load("data/config.ini");
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            World::reset(false);
            WorldResetGuard resetGuard;
            World world(World::Mode::Server);

            Ship* movingShip = new Ship(world);
            movingShip->addVoxel(
                new Voxel(glm::ivec3(0), 0xffffff, 1.0f, 100.0f));
            movingShip->physics().setSpeed(
                Speed(glm::vec3(30.0f, 0.0f, 0.0f), glm::vec3(0.0f)));
            movingShip->setAiUpdatePhase(
                std::numeric_limits<std::uint32_t>::max());

            Ship* obstacle = new Ship(world);
            obstacle->transform().setPosition(glm::vec3(2.0f, 0.0f, 0.0f));
            obstacle->addVoxel(
                new Voxel(glm::ivec3(0), 0xffffff, 1.0f, 100.0f));
            obstacle->setAiUpdatePhase(
                std::numeric_limits<std::uint32_t>::max());
            world.god().scheduleSpawn(movingShip);
            world.god().scheduleSpawn(obstacle);
            world.god().spawn();

            SimulationSession session(world);
            std::vector<voxellancer::protocol::EntitySnapshot> initialState;
            AssertThat(session.createSnapshotState(initialState), IsTrue());
            const auto actorId = world.entityId(movingShip);
            AssertThat(actorId.has_value(), IsTrue());
            const auto initialActor = std::find_if(
                initialState.begin(),
                initialState.end(),
                [actorId](const auto& snapshot) {
                    return snapshot.id == *actorId;
                });
            AssertThat(initialActor != initialState.end(), IsTrue());

            const float directionalDampening =
                Property<float>("physics.globalDirectionalDampening").get();
            const float angularDampening =
                Property<float>("physics.globalAngularDampening").get();
            const auto predictedWithoutCollision = InputPredictor().predict(
                *initialActor,
                session.tickIndex(),
                session.tickIndex() + 8,
                {},
                movingShip->components().enginePower(),
                directionalDampening,
                angularDampening);
            AssertThat(predictedWithoutCollision.has_value(), IsTrue());

            for (int tick = 0; tick < 8; ++tick) {
                session.advance(SimulationTickRunner::TickDurationSeconds);
            }
            AssertThat(
                world.worldLogic().voxelCollisionPairCount() > 0,
                IsTrue());

            std::vector<voxellancer::protocol::EntitySnapshot> serverState;
            AssertThat(session.createSnapshotState(serverState), IsTrue());
            const auto serverActor = std::find_if(
                serverState.begin(),
                serverState.end(),
                [actorId](const auto& snapshot) {
                    return snapshot.id == *actorId;
                });
            AssertThat(serverActor != serverState.end(), IsTrue());
            AssertThat(
                glm::length(
                    serverActor->position -
                    predictedWithoutCollision->position) > 0.25f,
                IsTrue());

            const auto reconciled = InputPredictor().predict(
                *serverActor,
                session.tickIndex(),
                session.tickIndex(),
                {},
                movingShip->components().enginePower(),
                directionalDampening,
                angularDampening);
            AssertThat(reconciled.has_value(), IsTrue());
            AssertThat(reconciled->position, Equals(serverActor->position));
            AssertThat(
                reconciled->directionalVelocity,
                Equals(serverActor->directionalVelocity));
        });

        it("assigns isolated actors to multiple local clients", []() {
            PropertyManager::instance()->load("data/config.ini");
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            World::reset(false);
            WorldResetGuard resetGuard;
            World world(World::Mode::Server);

            Ship* firstShip = new Ship(world);
            firstShip->addVoxel(new Voxel(glm::ivec3(0), 0xffffff));
            Ship* secondShip = new Ship(world);
            secondShip->transform().setPosition(glm::vec3(100.0f, 0.0f, 0.0f));
            secondShip->addVoxel(new Voxel(glm::ivec3(0), 0xffffff));
            world.god().scheduleSpawn(firstShip);
            world.god().scheduleSpawn(secondShip);
            world.god().spawn();

            SimulationSession session(world);
            TcpServer server(world, session, 0);
            TcpClient firstClient;
            TcpClient secondClient;
            AssertThat(firstClient.connectLocal(server.port()), IsTrue());
            AssertThat(secondClient.connectLocal(server.port()), IsTrue());

            for (int attempt = 0;
                 attempt < 100 &&
                    (!firstClient.actor() || !secondClient.actor());
                 ++attempt) {
                server.poll();
                AssertThat(firstClient.poll(), IsTrue());
                AssertThat(secondClient.poll(), IsTrue());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            AssertThat(firstClient.actor().has_value(), IsTrue());
            AssertThat(secondClient.actor().has_value(), IsTrue());
            AssertThat(*firstClient.actor() != *secondClient.actor(), IsTrue());
            AssertThat(firstClient.replicaState().size(), Equals(
                static_cast<std::size_t>(2)));
            AssertThat(secondClient.replicaState().size(), Equals(
                static_cast<std::size_t>(2)));

            voxellancer::protocol::InputCommand command;
            command.sequence = 1;
            command.tick = firstClient.snapshotTick() + 4;
            command.moveX = 8192;
            AssertThat(firstClient.sendInput(command), IsTrue());
            bool inputAccepted = false;
            for (int attempt = 0; attempt < 100 && !inputAccepted; ++attempt) {
                AssertThat(firstClient.poll(), IsTrue());
                AssertThat(secondClient.poll(), IsTrue());
                server.poll();
                session.advance(SimulationTickRunner::TickDurationSeconds);
                server.poll();
                AssertThat(firstClient.poll(), IsTrue());
                AssertThat(secondClient.poll(), IsTrue());
                for (const auto& acknowledgement :
                    firstClient.drainAcknowledgements()) {
                    if (acknowledgement.sequence == command.sequence) {
                        inputAccepted = acknowledgement.result ==
                            voxellancer::protocol::InputCommandResult::Accepted;
                    }
                }
                AssertThat(
                    secondClient.drainAcknowledgements().empty(),
                    IsTrue());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(inputAccepted, IsTrue());

            for (int attempt = 0;
                 attempt < 30 && firstClient.snapshotTick() <= command.tick;
                 ++attempt) {
                session.advance(SimulationTickRunner::TickDurationSeconds);
                server.poll();
                AssertThat(firstClient.poll(), IsTrue());
                AssertThat(secondClient.poll(), IsTrue());
            }
            AssertThat(firstClient.snapshotTick() > command.tick, IsTrue());

            Ship* controlledShip = static_cast<Ship*>(
                world.worldObject(*firstClient.actor()));
            Ship* otherShip = static_cast<Ship*>(
                world.worldObject(*secondClient.actor()));
            AssertThat(
                controlledShip->components().engineState().directional().x,
                EqualsWithDelta(8192.0f / 32767.0f, 1e-6f));
            AssertThat(
                otherShip->components().engineState().directional().x,
                Equals(0.0f));

            firstClient.disconnect();
            for (int attempt = 0; attempt < 100 && server.clientCount() != 1; ++attempt) {
                server.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(server.clientCount(), Equals(static_cast<std::size_t>(1)));
            session.advance(SimulationTickRunner::TickDurationSeconds);
            server.poll();
            AssertThat(
                world.worldObject(*secondClient.actor()) != nullptr,
                IsTrue());
            AssertThat(secondClient.poll(), IsTrue());

            secondClient.disconnect();
            for (int attempt = 0; attempt < 100 && server.clientCount() != 0; ++attempt) {
                server.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(server.clientCount(), Equals(static_cast<std::size_t>(0)));
        });

        it("replicates initial and changed equipment state", []() {
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            World world(World::Mode::Server);

            Ship* ship = WorldObjectBuilder("basicship", world).buildShip();
            ship->info().setName("Scenario display name");
            world.god().scheduleSpawn(ship);
            world.god().spawn();

            SimulationSession session(world);
            TcpServer server(world, session, 0);
            TcpClient client;
            AssertThat(client.connectLocal(server.port()), IsTrue());
            for (int attempt = 0;
                 attempt < 100 && client.entityContents().empty();
                 ++attempt) {
                server.poll();
                AssertThat(client.poll(), IsTrue());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            AssertThat(client.entityContents().size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(
                client.entityContents().front().definitionKey,
                Equals("basicship"));
            const auto hasEquipment = [&client](
                voxellancer::protocol::EquipmentSlotKind kind,
                const std::string& key
            ) {
                const auto& contents = client.entityContents();
                if (contents.empty()) {
                    return false;
                }
                const auto& equipment = contents.front().equipment;
                return std::any_of(
                    equipment.begin(),
                    equipment.end(),
                    [kind, &key](const auto& state) {
                        return state.kind == kind && state.equipmentKey == key;
                    });
            };
            AssertThat(
                hasEquipment(voxellancer::protocol::EquipmentSlotKind::Engine, "piratethruster"),
                IsTrue()
            );
            AssertThat(
                hasEquipment(voxellancer::protocol::EquipmentSlotKind::Weapon, "gun"),
                IsTrue()
            );
            AssertThat(
                hasEquipment(voxellancer::protocol::EquipmentSlotKind::Shield, "thinshield"),
                IsTrue()
            );

            const auto originalEngine =
                ship->components().engineSlots().front()->engine();
            ship->components().engineSlots().front()->setEngine(nullptr);
            for (int attempt = 0; attempt < 5; ++attempt) {
                session.advance(SimulationTickRunner::TickDurationSeconds);
                server.poll();
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(client.requestSnapshotResync(), IsTrue());
            AssertThat(client.poll(), IsTrue());
            server.poll();
            AssertThat(client.poll(), IsTrue());
            AssertThat(
                client.entityContents().front().definitionKey,
                Equals("basicship"));
            AssertThat(
                hasEquipment(voxellancer::protocol::EquipmentSlotKind::Engine, ""),
                IsTrue());

            ship->components().engineSlots().front()->setEngine(originalEngine);
            bool engineEquipped = false;
            for (int attempt = 0; attempt < 12 && !engineEquipped; ++attempt) {
                session.advance(SimulationTickRunner::TickDurationSeconds);
                server.poll();
                AssertThat(client.poll(), IsTrue());
                engineEquipped = hasEquipment(
                    voxellancer::protocol::EquipmentSlotKind::Engine, "piratethruster");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(engineEquipped, IsTrue());

            ship->components().engineSlots().front()->setEngine(nullptr);
            bool engineUnequipped = false;
            for (int attempt = 0; attempt < 12 && !engineUnequipped; ++attempt) {
                session.advance(SimulationTickRunner::TickDurationSeconds);
                server.poll();
                AssertThat(client.poll(), IsTrue());
                engineUnequipped = hasEquipment(
                    voxellancer::protocol::EquipmentSlotKind::Engine, "");
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(engineUnequipped, IsTrue());
            client.disconnect();
        });

        it("routes weapon spawns and events through the owning standalone world", []() {
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            World world(World::Mode::Server);

            Ship* ship = WorldObjectBuilder("basicship", world).buildShip();
            world.god().scheduleSpawn(ship);
            world.god().spawn();
            AssertThat(world.worldObjects().size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(ship->components().hardpoints().empty(), IsFalse());

            const auto& hardpoint = ship->components().hardpoints().front();
            const auto gun = std::dynamic_pointer_cast<Gun>(hardpoint->weapon());
            AssertThat(gun != nullptr, IsTrue());

            SimulationSession session(world);
            TcpServer server(world, session, 0);
            TcpClient client;
            AssertThat(client.connectLocal(server.port()), IsTrue());
            for (int attempt = 0;
                 attempt < 100 && client.entityContents().empty();
                 ++attempt) {
                server.poll();
                AssertThat(client.poll(), IsTrue());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(client.entityContents().size(), Equals(static_cast<std::size_t>(1)));

            voxellancer::protocol::InputCommand command;
            command.sequence = 1;
            command.tick = client.snapshotTick() + 4;
            command.buttons = voxellancer::protocol::FirePrimary;
            AssertThat(client.sendInput(command), IsTrue());

            bool inputAccepted = false;
            std::vector<voxellancer::protocol::WeaponFiredEvent> events;
            for (int attempt = 0;
                 attempt < 100 && (!inputAccepted || events.empty());
                 ++attempt) {
                AssertThat(client.poll(), IsTrue());
                server.poll();
                session.advance(SimulationTickRunner::TickDurationSeconds);
                server.poll();
                AssertThat(client.poll(), IsTrue());
                for (const auto& acknowledgement : client.drainAcknowledgements()) {
                    if (acknowledgement.sequence == command.sequence) {
                        inputAccepted = acknowledgement.result ==
                            voxellancer::protocol::InputCommandResult::Accepted;
                    }
                }
                const auto receivedEvents = client.drainWeaponFiredEvents();
                events.insert(
                    events.end(), receivedEvents.begin(), receivedEvents.end());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            AssertThat(inputAccepted, IsTrue());
            AssertThat(world.worldObjects().size() > 1, IsTrue());
            AssertThat(events.empty(), IsFalse());
            AssertThat(events.front().entity, Equals(*world.entityId(ship)));
            AssertThat(events.front().group, Equals(
                static_cast<std::uint16_t>(hardpoint->group())));
            AssertThat(events.front().cell, Equals(hardpoint->voxel()->cell()));
            client.disconnect();
        });

        it("replicates nonlethal voxel damage over TCP", []() {
            PropertyManager::instance()->load("data/config.ini");
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);

            Ship* ship = new Ship(*world);
            ship->addVoxel(new Voxel(glm::ivec3(0), 0xffffff, 1.0f, 10.0f));
            world->god().scheduleSpawn(ship);
            world->god().spawn();
            Voxel* voxel = ship->voxel(glm::ivec3(0));
            const auto entity = *world->entityId(ship);

            SimulationSession session(*world);
            TcpServer server(*world, session, 0);
            TcpClient client;
            AssertThat(client.connectLocal(server.port()), IsTrue());
            server.poll();
            AssertThat(client.poll(), IsTrue());
            AssertThat(client.entityContents().front().voxels.front().hp, Equals(10.0f));
            AssertThat(
                client.entityContents().front().voxels.front().maxHP,
                Equals(10.0f)
            );

            world->worldLogic().processDamageImpacts({
                DamageImpact(ship, voxel, glm::vec3(2.0f, 0.0f, 0.0f), 1.0f)
            });
            for (int tick = 0; tick < 3; ++tick) {
                session.advance(SimulationTickRunner::TickDurationSeconds);
            }
            server.poll();
            for (int attempt = 0;
                 attempt < 100 &&
                    client.entityContents().front().voxels.front().hp == 10.0f;
                 ++attempt) {
                AssertThat(client.poll(), IsTrue());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            const auto updatedContent = std::find_if(
                client.entityContents().begin(),
                client.entityContents().end(),
                [entity](const auto& content) {
                    return content.id == entity;
                });
            AssertThat(updatedContent != client.entityContents().end(), IsTrue());
            AssertThat(updatedContent->voxels.front().hp, Equals(8.0f));
            AssertThat(updatedContent->voxels.front().maxHP, Equals(10.0f));
            const auto damageEvents = client.drainVoxelDamageEvents();
            AssertThat(damageEvents.size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(damageEvents.front().entity, Equals(entity));
            AssertThat(damageEvents.front().cell, Equals(glm::ivec3(0)));
            AssertThat(damageEvents.front().hp, Equals(8.0f));
            client.disconnect();
        });

        it("replicates shield-absorbed damage over TCP", []() {
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);

            Ship* ship = new Ship(*world);
            ship->setShieldType(ShieldType::Hull);
            ship->addVoxel(new Voxel(glm::ivec3(0), 0xffffff, 1.0f, 10.0f));
            std::shared_ptr<ShieldSlot> shieldSlot =
                std::make_shared<ShieldSlot>(&ship->components(), 0);
            std::shared_ptr<Shield> shield =
                std::make_shared<Shield>("thinshield");
            shield->setMaxHP(10.0f);
            shield->setHP(10.0f);
            shieldSlot->setMountable("thinshield", true);
            shieldSlot->setShield(shield);
            ship->components().addShieldSlot(shieldSlot);
            world->god().scheduleSpawn(ship);
            world->god().spawn();
            Voxel* voxel = ship->voxel(glm::ivec3(0));
            const auto entity = *world->entityId(ship);

            SimulationSession session(*world);
            TcpServer server(*world, session, 0);
            TcpClient client;
            AssertThat(client.connectLocal(server.port()), IsTrue());
            server.poll();
            AssertThat(client.poll(), IsTrue());

            world->worldLogic().processDamageImpacts({
                DamageImpact(ship, voxel, glm::vec3(2.0f, 0.0f, 0.0f), 1.0f)
            });
            for (int tick = 0; tick < 3; ++tick) {
                session.advance(SimulationTickRunner::TickDurationSeconds);
            }
            server.poll();
            std::vector<voxellancer::protocol::ShieldHitEvent> events;
            for (int attempt = 0; attempt < 100 && events.empty(); ++attempt) {
                AssertThat(client.poll(), IsTrue());
                const auto receivedEvents = client.drainShieldHitEvents();
                events.insert(
                    events.end(), receivedEvents.begin(), receivedEvents.end());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            AssertThat(events.size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(events.front().entity, Equals(entity));
            AssertThat(events.front().cell, Equals(glm::ivec3(0)));
            AssertThat(events.front().localImpactPosition.x, Equals(0.0f));
            AssertThat(events.front().localImpactPosition.y, Equals(0.8f));
            AssertThat(events.front().localImpactPosition.z, Equals(0.0f));
            AssertThat(events.front().impactColor, Equals(0xffffffu));
            AssertThat(events.front().absorbedDamage, Equals(2.0f));
            const auto content = std::find_if(
                client.entityContents().begin(),
                client.entityContents().end(),
                [entity](const auto& candidate) {
                    return candidate.id == entity;
                });
            AssertThat(content != client.entityContents().end(), IsTrue());
            AssertThat(content->voxels.front().hp, Equals(10.0f));
            client.disconnect();
        });

        it("replicates authoritative mission-state events over TCP", []() {
            loadSimulationData();
            World::reset(false);
            WorldResetGuard resetGuard;
            World world(World::Mode::Server);

            Ship* ship = new Ship(world);
            ship->addVoxel(new Voxel(glm::ivec3(0), 0xffffff));
            world.god().scheduleSpawn(ship);
            world.god().spawn();

            SimulationSession session(world);
            TcpServer server(world, session, 0);
            TcpClient client;
            AssertThat(client.connectLocal(server.port()), IsTrue());
            for (int attempt = 0;
                 attempt < 100 && !client.actor();
                 ++attempt) {
                server.poll();
                AssertThat(client.poll(), IsTrue());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(client.actor().has_value(), IsTrue());

            session.advance(SimulationTickRunner::TickDurationSeconds);
            std::shared_ptr<Mission> mission = std::make_shared<Mission>(
                world, "test/data/mission-state-replication.lua");
            world.missionSystem().addMission(mission);
            server.poll();
            std::vector<voxellancer::protocol::MissionStateEvent> events;
            for (int attempt = 0; attempt < 100 && events.empty(); ++attempt) {
                AssertThat(client.poll(), IsTrue());
                const auto receivedEvents = client.drainMissionStateEvents();
                events.insert(
                    events.end(), receivedEvents.begin(), receivedEvents.end());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            AssertThat(events.size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(
                events.front().state,
                Equals(voxellancer::protocol::MissionEventState::Started));
            AssertThat(
                events.front().missionKey,
                Equals("test/data/mission-state-replication.lua"));

            mission->succeed();
            server.poll();
            events.clear();
            for (int attempt = 0; attempt < 100 && events.empty(); ++attempt) {
                AssertThat(client.poll(), IsTrue());
                const auto receivedEvents = client.drainMissionStateEvents();
                events.insert(
                    events.end(), receivedEvents.begin(), receivedEvents.end());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(events.size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(
                events.front().state,
                Equals(voxellancer::protocol::MissionEventState::Succeeded));
            client.disconnect();
        });

        it("forwards authoritative voxel removals over TCP", []() {
            PropertyManager::instance()->load("data/config.ini");
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);

            auto spawnShip = [world](float x, float voxelHp) {
                Ship* ship = new Ship(*world);
                ship->addVoxel(new Voxel(
                    glm::ivec3(0, 0, 0), 0xffffff, 1.0f, voxelHp));
                ship->transform().setPosition(glm::vec3(x, 0.0f, 0.0f));
                world->god().scheduleSpawn(ship);
                world->god().spawn();
                return ship;
            };
            spawnShip(0.0f, 100.0f);
            Ship* target = spawnShip(10.0f, 1.0f);
            target->addVoxel(new Voxel(
                glm::ivec3(1, 0, 0), 0xffffff, 1.0f, 100.0f));
            const auto targetId = *world->entityId(target);
            Voxel* voxel = target->voxel(glm::ivec3(0, 0, 0));

            SimulationSession session(*world);
            TcpServer server(*world, session, 0, 250.0f);
            TcpClient client;
            AssertThat(client.connectLocal(server.port()), IsTrue());
            server.poll();
            AssertThat(client.poll(), IsTrue());
            AssertThat(client.actor().has_value(), IsTrue());
            const auto initialContent = std::find_if(
                client.entityContents().begin(),
                client.entityContents().end(),
                [targetId](const auto& content) {
                    return content.id == targetId;
                });
            AssertThat(initialContent != client.entityContents().end(), IsTrue());
            AssertThat(initialContent->voxels.size(), Equals(static_cast<std::size_t>(2)));

            world->worldLogic().processDamageImpacts({
                DamageImpact(target, voxel, glm::vec3(2.0f, 0.0f, 0.0f), 1.0f)
            });
            session.advance(SimulationTickRunner::TickDurationSeconds);
            server.poll();
            AssertThat(client.poll(), IsTrue());

            const auto events = client.drainVoxelRemovalEvents();
            AssertThat(events.size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(events.front().entity, Equals(targetId));
            AssertThat(events.front().cell, Equals(glm::ivec3(0, 0, 0)));
            const auto updatedContent = std::find_if(
                client.entityContents().begin(),
                client.entityContents().end(),
                [targetId](const auto& content) {
                    return content.id == targetId;
                });
            AssertThat(updatedContent != client.entityContents().end(), IsTrue());
            AssertThat(updatedContent->voxels.size(), Equals(static_cast<std::size_t>(1)));
            client.disconnect();
        });

        it("replicates voxel damage and newly split ship content", []() {
            PropertyManager::instance()->load("data/config.ini");
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);

            Ship* ship = new Ship(*world);
            ship->info().setName("Scenario display name");
            ship->info().setDefinitionKey("test-ship-definition");
            ship->addVoxel(new Voxel(glm::ivec3(2, 0, 0), 0xffffff));
            ship->addVoxel(new Voxel(glm::ivec3(2, 1, 0), 0xffffff));
            ship->addVoxel(new Voxel(glm::ivec3(2, 2, 0), 0xffffff, 1.0f, 1.0f));
            ship->addVoxel(new Voxel(glm::ivec3(2, 3, 0), 0xffffff));
            ship->addVoxel(new Voxel(glm::ivec3(1, 3, 0), 0xffffff));
            ship->addVoxel(new Voxel(glm::ivec3(0, 3, 0), 0xffffff));
            ship->addVoxel(new Voxel(glm::ivec3(3, 3, 0), 0xffffff));
            ship->addVoxel(new Voxel(glm::ivec3(4, 3, 0), 0xffffff));
            ship->addVoxel(new Voxel(glm::ivec3(5, 3, 0), 0xffffff));
            ship->setCrucialVoxel(glm::ivec3(2, 1, 0));
            world->god().scheduleSpawn(ship);
            world->god().spawn();
            Voxel* destroyedVoxel = ship->voxel(glm::ivec3(2, 2, 0));
            const auto originalId = *world->entityId(ship);

            SimulationSession session(*world);
            TcpServer server(*world, session, 0);
            TcpClient client;
            AssertThat(client.connectLocal(server.port()), IsTrue());
            server.poll();
            AssertThat(client.poll(), IsTrue());
            AssertThat(client.actor().has_value(), IsTrue());
            AssertThat(client.entityContents().size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(
                client.entityContents().front().definitionKey,
                Equals("test-ship-definition"));
            AssertThat(client.entityContents().front().voxels.size(),
                Equals(static_cast<std::size_t>(9)));

            world->worldLogic().processDamageImpacts({
                DamageImpact(ship, destroyedVoxel, glm::vec3(2.0f, 0.0f, 0.0f), 1.0f)
            });
            for (int tick = 0; tick < 3; ++tick) {
                session.advance(SimulationTickRunner::TickDurationSeconds);
            }
            server.poll();
            for (int attempt = 0;
                 attempt < 100 && client.snapshotTick() < session.tickIndex();
                 ++attempt) {
                AssertThat(client.poll(), IsTrue());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }

            AssertThat(client.snapshotTick(), Equals(session.tickIndex()));
            const auto originalContent = std::find_if(
                client.entityContents().begin(),
                client.entityContents().end(),
                [originalId](const auto& content) {
                    return content.id == originalId;
                });
            AssertThat(originalContent != client.entityContents().end(), IsTrue());
            AssertThat(
                originalContent->definitionKey,
                Equals("test-ship-definition"));
            AssertThat(originalContent->voxels.size(), Equals(static_cast<std::size_t>(2)));
            const auto splitContent = std::find_if(
                client.entityContents().begin(),
                client.entityContents().end(),
                [originalId](const auto& content) {
                    return content.id != originalId && content.voxels.size() == 6;
                });
            AssertThat(splitContent != client.entityContents().end(), IsTrue());
            AssertThat(
                splitContent->definitionKey,
                Equals("test-ship-definition"));
            const auto removalEvents = client.drainVoxelRemovalEvents();
            AssertThat(removalEvents.size(), Equals(static_cast<std::size_t>(7)));
            AssertThat(std::find(
                removalEvents.begin(),
                removalEvents.end(),
                voxellancer::protocol::VoxelRemovalEvent{
                    originalId, glm::ivec3(2, 2, 0)
                }) != removalEvents.end(), IsTrue());
            client.disconnect();
        });

        it("reassigns a live actor after disconnect and reconnect", []() {
            PropertyManager::instance()->load("data/config.ini");
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);

            const auto spawnShip = [world](float x) {
                Ship* ship = new Ship(*world);
                ship->addVoxel(new Voxel(glm::ivec3(0), 0xffffff));
                ship->transform().setPosition(glm::vec3(x, 0.0f, 0.0f));
                world->god().scheduleSpawn(ship);
                world->god().spawn();
            };
            spawnShip(0.0f);
            spawnShip(10.0f);

            SimulationSession session(*world);
            TcpServer server(*world, session, 0);
            TcpClient firstClient;
            AssertThat(firstClient.connectLocal(server.port()), IsTrue());
            for (int attempt = 0;
                 attempt < 100 &&
                    (!firstClient.actor() || firstClient.snapshotSequence() == 0);
                 ++attempt) {
                server.poll();
                AssertThat(firstClient.poll(), IsTrue());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(firstClient.actor().has_value(), IsTrue());
            const auto firstActor = *firstClient.actor();

            firstClient.disconnect();
            server.poll();
            session.advance(SimulationTickRunner::TickDurationSeconds);

            TcpClient reconnectingClient;
            AssertThat(reconnectingClient.connectLocal(server.port()), IsTrue());
            for (int attempt = 0;
                 attempt < 100 &&
                    (!reconnectingClient.actor() ||
                     reconnectingClient.snapshotSequence() == 0);
                 ++attempt) {
                server.poll();
                AssertThat(reconnectingClient.poll(), IsTrue());
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(reconnectingClient.actor().has_value(), IsTrue());
            AssertThat(*reconnectingClient.actor() != firstActor, IsTrue());

            const voxellancer::protocol::InputCommand command{
                1, reconnectingClient.snapshotTick() + 4, 4096
            };
            AssertThat(reconnectingClient.sendInput(command), IsTrue());
            bool accepted = false;
            for (int attempt = 0; attempt < 100 && !accepted; ++attempt) {
                AssertThat(reconnectingClient.poll(), IsTrue());
                server.poll();
                session.advance(SimulationTickRunner::TickDurationSeconds);
                server.poll();
                AssertThat(reconnectingClient.poll(), IsTrue());
                for (const auto& acknowledgement :
                    reconnectingClient.drainAcknowledgements()) {
                    if (acknowledgement.sequence == command.sequence) {
                        accepted = acknowledgement.result ==
                            voxellancer::protocol::InputCommandResult::Accepted;
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
            AssertThat(accepted, IsTrue());
            reconnectingClient.disconnect();
        });

        it("replicates entities inside the actor interest radius and removes those leaving it", []() {
            PropertyManager::instance()->load("data/config.ini");
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);

            auto spawnShip = [world](const glm::vec3& position) {
                Ship* ship = new Ship(*world);
                ship->addVoxel(new Voxel(glm::ivec3(0), 0xffffff));
                ship->transform().setPosition(position);
                world->god().scheduleSpawn(ship);
                world->god().spawn();
                return ship;
            };
            Ship* actorShip = spawnShip(glm::vec3(0.0f));

            SimulationSession session(*world);
            TcpServer server(*world, session, 0, 250.0f);
            TcpClient client;
            AssertThat(client.connectLocal(server.port()), IsTrue());
            server.poll();
            AssertThat(client.poll(), IsTrue());
            AssertThat(client.actor().has_value(), IsTrue());
            AssertThat(*client.actor(), Equals(*world->entityId(actorShip)));
            const auto initialContentChanges =
                client.drainEntityContentChanges();
            AssertThat(
                std::find(
                    initialContentChanges.begin(),
                    initialContentChanges.end(),
                    *world->entityId(actorShip)) != initialContentChanges.end(),
                IsTrue());

            Ship* nearbyShip = spawnShip(glm::vec3(100.0f, 0.0f, 0.0f));
            Ship* distantShip = spawnShip(glm::vec3(1000.0f, 0.0f, 0.0f));
            const auto nearbyId = *world->entityId(nearbyShip);
            const auto distantId = *world->entityId(distantShip);
            const auto contains = [&client](voxellancer::protocol::EntityId id) {
                return std::any_of(
                    client.replicaState().begin(),
                    client.replicaState().end(),
                    [id](const auto& snapshot) { return snapshot.id == id; }
                );
            };

            for (int tick = 0; tick < 3; ++tick) {
                session.advance(SimulationTickRunner::TickDurationSeconds);
                server.poll();
                AssertThat(client.poll(), IsTrue());
            }
            const auto nearbyContentChanges =
                client.drainEntityContentChanges();
            AssertThat(contains(nearbyId), IsTrue());
            AssertThat(contains(distantId), IsFalse());
            AssertThat(
                std::find(
                    nearbyContentChanges.begin(),
                    nearbyContentChanges.end(),
                    nearbyId) != nearbyContentChanges.end(),
                IsTrue());
            AssertThat(
                std::find(
                    nearbyContentChanges.begin(),
                    nearbyContentChanges.end(),
                    distantId) == nearbyContentChanges.end(),
                IsTrue());
            const auto hasContent = [&client](voxellancer::protocol::EntityId id) {
                return std::any_of(
                    client.entityContents().begin(),
                    client.entityContents().end(),
                    [id](const auto& content) { return content.id == id; }
                );
            };
            AssertThat(hasContent(nearbyId), IsTrue());
            AssertThat(hasContent(distantId), IsFalse());

            distantShip->transform().setPosition(glm::vec3(120.0f, 0.0f, 0.0f));
            for (int tick = 0; tick < 3; ++tick) {
                session.advance(SimulationTickRunner::TickDurationSeconds);
                server.poll();
                AssertThat(client.poll(), IsTrue());
            }
            AssertThat(contains(distantId), IsTrue());
            AssertThat(hasContent(distantId), IsTrue());
            const auto enteredRangeChanges =
                client.drainEntityContentChanges();
            AssertThat(
                std::find(
                    enteredRangeChanges.begin(),
                    enteredRangeChanges.end(),
                    distantId) != enteredRangeChanges.end(),
                IsTrue());

            distantShip->transform().setPosition(glm::vec3(1000.0f, 0.0f, 0.0f));
            for (int tick = 0; tick < 3; ++tick) {
                session.advance(SimulationTickRunner::TickDurationSeconds);
                server.poll();
                AssertThat(client.poll(), IsTrue());
            }
            AssertThat(contains(distantId), IsFalse());
            AssertThat(hasContent(distantId), IsFalse());
            const auto leftRangeChanges =
                client.drainEntityContentChanges();
            AssertThat(
                std::find(
                    leftRangeChanges.begin(),
                    leftRangeChanges.end(),
                    distantId) != leftRangeChanges.end(),
                IsTrue());
            client.disconnect();
        });
    });
});
