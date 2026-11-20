#include <bandit/bandit.h>

#include <memory>
#include <limits>
#include <stdexcept>
#include <iostream>
#include <cmath>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ai/basictasks/fighttask.h"
#include "ai/boardcomputer.h"
#include "ai/character.h"
#include "ai/squad.h"
#include "ai/squadlogic.h"
#include "property/propertydirectory.h"
#include "property/propertymanager.h"
#include "resource/worldobjectbuilder.h"
#include "equipment/hardpoint.h"
#include "equipment/weapons/gun.h"
#include "voxel/specialvoxels/hardpointvoxel.h"
#include "factions/factionmatrix.h"
#include "factions/factionrelation.h"
#include "resource/weaponbuilder.h"
#include "physics/physics.h"
#include "scenarios/aicomparisonscenario.h"
#include "utils/simulationtickrunner.h"
#include "worldobject/helper/hardpointaimhelper.h"
#include "worldobject/worldobjectcomponents.h"
#include "worldobject/worldobjectinfo.h"
#include "utils/handle/handle.h"
#include "world/god.h"
#include "world/world.h"
#include "worldobject/ship.h"

using namespace bandit;

namespace {
    Ship* buildTarget(
        World& world,
        const std::string& definition,
        const glm::vec3& position)
    {
        Ship* ship = WorldObjectBuilder(definition, world).buildShip();
        ship->transform().setPosition(position);
        world.god().scheduleSpawn(ship);
        return ship;
    }

    void advanceTasks(
        World& world,
        const std::vector<FightTask*>& tasks,
        int ticks)
    {
        for (int tick = 0; tick < ticks; ++tick) {
            world.update(1.0f / 30.0f);
            for (FightTask* task : tasks) {
                task->update(1.0f / 30.0f);
            }
        }
    }
}

go_bandit([]() {
    describe("FightTask", []() {
        before_each([]() {
            PropertyManager::instance()->reset();
            PropertyManager::instance()->load("data/config.ini");
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            PropertyDirectory("data/worldobjects").read();
            PropertyDirectory("data/equipment/engines").read();
            PropertyDirectory("data/equipment/shields").read();
            PropertyDirectory("data/equipment/weapons").read();
            PropertyDirectory("data/equipment/projectiles").read();
            World::reset(false);
        });

        it("loads ship types and maps them to default fight archetypes", []() {
            World world(World::Mode::Server);
            Ship* fighter = WorldObjectBuilder("basicship", world).buildShip();
            Ship* cruiser = WorldObjectBuilder("bc304", world).buildShip();

            AssertThat(fighter->shipType(), Equals(ShipType::Fighter));
            AssertThat(cruiser->shipType(), Equals(ShipType::Cruiser));
            AssertThat(
                FightTask(fighter->boardComputer(), {}).archetype(),
                Equals(AiArchetype::Fighter));
            AssertThat(
                FightTask(cruiser->boardComputer(), {}).archetype(),
                Equals(AiArchetype::Cruiser));

            delete fighter;
            delete cruiser;
        });

        it("prefers the nearest fighter and falls back when none exist", []() {
            World world(World::Mode::Server);
            std::unique_ptr<Ship> owner(
                WorldObjectBuilder("basicship", world).buildShip());
            std::vector<Handle<WorldObject>> targets;
            Ship* cruiser = buildTarget(
                world, "bc304", glm::vec3(1200.0f, 0.0f, 0.0f));
            Ship* fartherFighter = buildTarget(
                world, "f302", glm::vec3(2100.0f, 0.0f, 0.0f));
            Ship* nearerFighter = buildTarget(
                world, "f301", glm::vec3(1700.0f, 0.0f, 0.0f));
            targets.push_back(cruiser->handle<WorldObject>());
            targets.push_back(fartherFighter->handle<WorldObject>());
            targets.push_back(nearerFighter->handle<WorldObject>());
            world.god().spawn();

            FightTask fighterTask(
                owner->boardComputer(), targets, AiArchetype::Fighter);
            advanceTasks(world, {&fighterTask}, 40);
            AssertThat(fighterTask.primaryTarget(), Equals(nearerFighter));

            FightTask cruiserTask(
                owner->boardComputer(), targets, AiArchetype::Cruiser);
            advanceTasks(world, {&cruiserTask}, 120);
            AssertThat(cruiserTask.primaryTarget(), Equals(cruiser));

            FightTask fallbackTask(
                owner->boardComputer(),
                {cruiser->handle<WorldObject>()},
                AiArchetype::Fighter);
            advanceTasks(world, {&fallbackTask}, 40);
            AssertThat(fallbackTask.primaryTarget(), Equals(cruiser));
        });

        it("shares the leader's primary target across squad members", []() {
            World world(World::Mode::Server);
            std::unique_ptr<Ship> leader(
                WorldObjectBuilder("basicship", world).buildShip());
            std::unique_ptr<Ship> follower(
                WorldObjectBuilder("basicship", world).buildShip());
            follower->transform().setPosition(glm::vec3(10.0f, 0.0f, 0.0f));
            const std::shared_ptr<Squad> squad = std::make_shared<Squad>();
            leader->squadLogic()->joinSquad(squad);
            follower->squadLogic()->joinSquad(squad);

            Ship* target = buildTarget(
                world, "basicship", glm::vec3(1200.0f, 0.0f, 0.0f));
            Ship* otherTarget = buildTarget(
                world, "basicship", glm::vec3(1700.0f, 0.0f, 0.0f));
            const std::vector<Handle<WorldObject>> targets{
                otherTarget->handle<WorldObject>(),
                target->handle<WorldObject>()};
            world.god().spawn();

            FightTask leaderTask(leader->boardComputer(), targets);
            FightTask followerTask(follower->boardComputer(), targets);
            advanceTasks(world, {&leaderTask, &followerTask}, 40);
            AssertThat(leaderTask.primaryTarget(), Equals(target));
            AssertThat(followerTask.primaryTarget(), Equals(target));
        });

        it("leads moving targets using inherited projectile velocity and rejects unreachable shots", []() {
            World world(World::Mode::Server);
            std::unique_ptr<Ship> owner(WorldObjectBuilder("contractraider",world).buildShip());
            std::unique_ptr<Ship> target(WorldObjectBuilder("contractraider",world).buildShip());
            target->transform().setPosition(glm::vec3(0,0,-300));
            const auto mount = owner->components().hardpoints().front();
            owner->physics().setSpeed(Speed(glm::vec3(100,0,0),glm::vec3(0)));
            target->physics().setSpeed(Speed(glm::vec3(100,0,0),glm::vec3(0)));
            HardpointAimHelper matched(mount.get(),target.get());
            AssertThat(matched.isHittable(),IsTrue());
            AssertThat(glm::distance(matched.point(),target->position()) < 0.001f,IsTrue());
            owner->physics().setSpeed(Speed());
            HardpointAimHelper crossing(mount.get(),target.get());
            AssertThat(crossing.isHittable(),IsTrue());
            AssertThat(crossing.point().x > 50,IsTrue());
            target->physics().setSpeed(Speed(glm::vec3(0,0,-1000),glm::vec3(0)));
            HardpointAimHelper escaping(mount.get(),target.get());
            AssertThat(escaping.isHittable(),IsFalse());
            target->physics().setSpeed(Speed());
            target->transform().setPosition(glm::vec3(0,0,-700));
            static_cast<Gun*>(mount->weapon().get())->setBulletSpeed(1000);
            HardpointAimHelper pastRange(mount.get(),target.get());
            AssertThat(pastRange.isHittable(),IsFalse());
        });

        it("turns the interceptor toward a directly rearward target and clears thrust after target loss", []() {
            World world(World::Mode::Server);
            auto* owner = buildTarget(world,"contractraider",glm::vec3(0));
            auto* target = buildTarget(world,"contractraider",glm::vec3(0,0,300));
            world.god().spawn();
            FightTask task(owner->boardComputer(),{target->handle<WorldObject>()},AiArchetype::Interceptor);
            task.update(1.0f/30);
            const auto angular = owner->boardComputer()->engineState().angular();
            AssertThat(std::isfinite(angular.x) && std::isfinite(angular.y) &&
                std::isfinite(angular.z),IsTrue());
            AssertThat(glm::length(angular) > 0.1f,IsTrue());
            world.god().scheduleRemoval(target);
            world.god().remove();
            task.update(1.0f/30);
            AssertThat(task.isFinished(),IsTrue());
            AssertThat(glm::length(owner->boardComputer()->engineState().directional()),Equals(0.0f));
            AssertThat(glm::length(owner->boardComputer()->engineState().angular()),Equals(0.0f));
        });

        it("brakes an interceptor on a high-closing-speed collision course instead of chasing through", []() {
            World world(World::Mode::Server);
            auto* owner = buildTarget(world,"contractraider",glm::vec3(0));
            auto* target = buildTarget(world,"contractraider",glm::vec3(0,0,-100));
            world.god().spawn();
            owner->physics().setSpeed(Speed(glm::vec3(0,0,-120),glm::vec3(0)));
            FightTask task(owner->boardComputer(),{target->handle<WorldObject>()},AiArchetype::Interceptor);
            task.update(1.0f/30);
            AssertThat(owner->boardComputer()->engineState().directional().z > 0,IsTrue());
            world.god().spawn();
        });

        it("recovers its reactor in bursts without letting an unaffordable mount starve usable guns", []() {
            World world(World::Mode::Server);
            auto* owner = buildTarget(world,"contractship",glm::vec3(0));
            auto* target = buildTarget(world,"contractraider",glm::vec3(0,0,-250));
            world.god().spawn();
            auto& factions = world.factionMatrix();
            owner->character()->setFaction(factions.policeFaction());
            target->character()->setFaction(factions.pirateFaction());
            factions.getRelation(factions.policeFaction(),factions.pirateFaction()).setFriendliness(-100);
            auto& components = owner->components();
            components.configureWeaponEnergy(15,10,1);
            for (const auto& mount : components.hardpoints())
                mount->setWeapon(std::shared_ptr<Weapon>(WeaponBuilder("kineticrailgun").build()));
            components.hardpoints().front()->setWeapon(
                std::shared_ptr<Weapon>(WeaponBuilder("pulselaser").build()));
            FightTask task(owner->boardComputer(),{target->handle<WorldObject>()},AiArchetype::Interceptor);
            task.shooting().setSkill({1,1,1});
            task.update(1.0f/30);
            AssertThat(components.weaponEnergy(),Equals(11.0f));
            AssertThat(components.consumeWeaponEnergy(11),IsTrue());
            task.update(1.0f/30);
            components.update(1.5f);
            task.update(1.0f/30);
            AssertThat(components.weaponEnergy(),Equals(5.0f));
            components.update(0.4f);
            task.update(1.0f/30);
            AssertThat(components.weaponEnergy(),EqualsWithDelta(5.0f,0.0001f));
            world.god().spawn();
        });

        it("isolates one experiment variable per duel, mirrors the inputs, and records mutual losses", []() {
            World world(World::Mode::Server);
            AiComparisonScenario comparison(world);
            comparison.load();
            AssertThat(world.ships().size(),Equals(std::size_t(32)));
            for (std::size_t index = 0; index < comparison.duels().size(); ++index) {
                auto duel = comparison.duels()[index];
                AssertThat(duel.initialHull[0],Equals(duel.initialHull[1]));
                for (int side = 0; side < 2; ++side) {
                    auto* ship = static_cast<Ship*>(duel.ships[side].get());
                    auto task = std::dynamic_pointer_cast<FightTask>(ship->character()->task());
                    AssertThat(bool(task),IsTrue());
                    AssertThat(task->archetype(),Equals(duel.behaviours[side]));
                    const auto& skill = task->shooting().skill();
                    const auto& other = duel.skills[1-side];
                    const int differences = int(skill.lead != other.lead)+
                        int(skill.tracking != other.tracking)+int(skill.accuracy != other.accuracy);
                    AssertThat(differences,Equals(index < 4 ? 0 : 1));
                    if (index >= 4) {
                        AssertThat(duel.behaviours[0],Equals(duel.behaviours[1]));
                        AssertThat(skill.lead,Equals(duel.skills[side].lead));
                        AssertThat(skill.tracking,Equals(duel.skills[side].tracking));
                        AssertThat(skill.accuracy,Equals(duel.skills[side].accuracy));
                        const float value = side == 1 ? 1.0f : ((index-4)%4 < 2 ? 0.0f : 0.5f);
                        switch (duel.experiment) {
                        case AiComparisonScenario::Experiment::Lead:
                            AssertThat(skill.lead,Equals(value)); break;
                        case AiComparisonScenario::Experiment::Tracking:
                            AssertThat(skill.tracking,Equals(value)); break;
                        case AiComparisonScenario::Experiment::Accuracy:
                            AssertThat(skill.accuracy,Equals(value)); break;
                        default: AssertThat(false,IsTrue());
                        }
                    }
                    AssertThat(task->targets().size(),Equals(std::size_t(1)));
                    AssertThat(task->targets()[0].get(),Equals(duel.ships[1-side].get()));
                    AssertThat(ship->info().invulnerable(),IsFalse());
                }
                const auto x = duel.ships[0].get()->position().x-duel.center.x;
                AssertThat(duel.mirrored ? x > 0 : x < 0,IsTrue());
                if (index%2) {
                    auto previous = comparison.duels()[index-1];
                    for (int side = 0; side < 2; ++side) {
                        const auto* mirrored = duel.ships[side].get();
                        const auto* original = previous.ships[1-side].get();
                        AssertThat(glm::distance(mirrored->position()-duel.center,
                            original->position()-previous.center) < 0.001f,IsTrue());
                        AssertThat(glm::distance(mirrored->physics().speed().directional(),
                            original->physics().speed().directional()) < 0.001f,IsTrue());
                        AssertThat(glm::length(mirrored->orientation()-original->orientation()) < 0.001f,IsTrue());
                    }
                }
            }
            for (auto* ship : world.ships()) world.god().scheduleRemoval(ship);
            world.god().remove();
            world.update(1.0f/30);
            AssertThat(comparison.finished(),IsTrue());
            for (const auto& duel : comparison.duels())
                AssertThat(duel.result,Equals(AiComparisonScenario::Result::MutualLoss));
        });

        it("keeps unfinished duels active until exactly the comparison limit and freezes timeout results", []() {
            World world(World::Mode::Server);
            AiComparisonScenario comparison(world);
            comparison.load();
            for (auto* ship : world.ships()) {
                ship->character()->setTask(nullptr);
                ship->physics().setSpeed(Speed());
            }
            const auto limit = AiComparisonScenario::DurationSeconds*SimulationTickRunner::TickRate;
            for (int tick = 0; tick < limit-1; ++tick) world.update(1.0f/30);
            AssertThat(comparison.finished(),IsFalse());
            world.update(1.0f/30);
            AssertThat(comparison.finished(),IsTrue());
            for (const auto& duel : comparison.duels()) {
                AssertThat(duel.result,Equals(AiComparisonScenario::Result::Timeout));
                AssertThat(duel.hull[0],Equals(1.0f));
                AssertThat(duel.hull[1],Equals(1.0f));
            }
            world.update(1.0f/30);
            AssertThat(comparison.elapsedTicks(),Equals(static_cast<std::uint64_t>(limit)));
        });

        it("allows in-flight return fire to resolve before awarding a win and keeps the result frozen", []() {
            World world(World::Mode::Server);
            AiComparisonScenario comparison(world);
            comparison.load();
            for (auto* ship : world.ships()) {
                ship->character()->setTask(nullptr);
                ship->physics().setSpeed(Speed());
            }
            auto duel = comparison.duels()[0];
            world.god().scheduleRemoval(duel.ships[0].get());
            world.god().remove();
            for (int tick = 0; tick < 2*SimulationTickRunner::TickRate; ++tick)
                world.update(1.0f/30);
            AssertThat(comparison.duels()[0].result,Equals(AiComparisonScenario::Result::Fighting));
            world.update(1.0f/30);
            AssertThat(comparison.duels()[0].result,Equals(AiComparisonScenario::Result::SideBWin));
            world.god().scheduleRemoval(duel.ships[1].get());
            world.god().remove();
            world.update(1.0f/30);
            AssertThat(comparison.duels()[0].result,Equals(AiComparisonScenario::Result::SideBWin));
        });

        it("blends lead independently and rejects invalid skill changes without changing the configuration", []() {
            World world(World::Mode::Server);
            auto* owner = buildTarget(world,"contractraider",glm::vec3(0));
            auto* target = buildTarget(world,"contractraider",glm::vec3(0,0,-300));
            world.god().spawn();
            target->physics().setSpeed(Speed(glm::vec3(100,0,0),glm::vec3(0)));
            std::vector<Handle<WorldObject>> targets{target->handle<WorldObject>()};
            const auto mount = owner->components().hardpoints().front();
            HardpointAimHelper helper(mount.get(),target);
            AssertThat(helper.isHittable(),IsTrue());
            ShootingController shooting(owner->boardComputer(),{0,1,1});
            for (float lead : {0.0f,0.5f,1.0f}) {
                shooting.setSkill({lead,1,1});
                shooting.update(0.2f,targets,target,false);
                const auto point = shooting.aimPoint(mount.get());
                AssertThat(point.has_value(),IsTrue());
                AssertThat(glm::distance(*point,glm::mix(target->position(),helper.point(),lead)) < 0.001f,IsTrue());
            }
            for (const auto& invalid : {ShootingSkill{-0.01f,1,1},ShootingSkill{1,1.01f,1},
                    ShootingSkill{1,1,std::numeric_limits<float>::quiet_NaN()}}) {
                bool rejected = false;
                try { shooting.setSkill(invalid); }
                catch (const std::invalid_argument&) { rejected = true; }
                AssertThat(rejected,IsTrue());
                AssertThat(shooting.skill().lead,Equals(1.0f));
                AssertThat(shooting.skill().tracking,Equals(1.0f));
                AssertThat(shooting.skill().accuracy,Equals(1.0f));
            }
        });

        it("slews the crosshair slowly at zero tracking, retains it on target switches, and tracks instantly at one", []() {
            World world(World::Mode::Server);
            auto* owner = buildTarget(world,"contractraider",glm::vec3(0));
            auto* target = buildTarget(world,"contractraider",glm::vec3(40,0,-250));
            auto* other = buildTarget(world,"contractraider",glm::vec3(-100,0,-250));
            world.god().spawn();
            const auto mount = owner->components().hardpoints().front();
            const auto origin = mount->voxel()->position();
            std::vector<Handle<WorldObject>> targets{target->handle<WorldObject>()};
            ShootingController shooting(owner->boardComputer(),{1,0,1});
            shooting.update(0.2f,targets,target,false);
            AssertThat(shooting.aimPoint(mount.get()).has_value(),IsTrue());
            auto direction = glm::normalize(*shooting.aimPoint(mount.get())-origin);
            const float firstAngle = glm::degrees(std::acos(glm::clamp(
                glm::dot(direction,owner->orientation()*mount->direction()),-1.0f,1.0f)));
            AssertThat(firstAngle,EqualsWithDelta(1.0f,0.005f));
            targets = {other->handle<WorldObject>()};
            shooting.update(0.2f,targets,other,false);
            AssertThat(shooting.aimPoint(mount.get()).has_value(),IsTrue());
            const auto next = glm::normalize(*shooting.aimPoint(mount.get())-origin);
            const float step = glm::degrees(std::acos(glm::clamp(glm::dot(direction,next),-1.0f,1.0f)));
            AssertThat(step,EqualsWithDelta(1.0f,0.005f));
            shooting.setSkill({1,1,1});
            other->physics().setSpeed(Speed(glm::vec3(-40,30,0),glm::vec3(0)));
            shooting.update(0.2f,targets,other,false);
            AssertThat(shooting.aimPoint(mount.get()).has_value(),IsTrue());
            HardpointAimHelper helper(mount.get(),other);
            AssertThat(helper.isHittable(),IsTrue());
            AssertThat(glm::distance(*shooting.aimPoint(mount.get()),helper.point()) < 0.001f,IsTrue());
            targets.clear();
            shooting.update(0.2f,targets,nullptr,false);
            AssertThat(shooting.aimPoint(mount.get()).has_value(),IsFalse());
        });

        it("keeps shooting skill and shooting randomness out of both flight controllers", []() {
            for (auto behaviour : {AiArchetype::Fighter,AiArchetype::Interceptor}) {
                World world(World::Mode::Server);
                auto* owner = buildTarget(world,"contractraider",glm::vec3(0));
                auto* target = buildTarget(world,"contractraider",glm::vec3(30,0,-250));
                target->info().setInvulnerable(true);
                world.god().spawn();
                FightTask task(owner->boardComputer(),{target->handle<WorldObject>()},behaviour,{1,1,1});
                advanceTasks(world,{&task},40);
                const auto before = owner->boardComputer()->engineState();
                AssertThat(glm::length(before.directional())+glm::length(before.angular()) > 0,IsTrue());
                owner->boardComputer()->seedRandom(7373);
                target->boardComputer()->seedRandom(7373);
                owner->components().update(0.5f);
                task.shooting().setSkill({0,0,0});
                task.update(1.0f/30);
                AssertThat(glm::distance(before.directional(),
                    owner->boardComputer()->engineState().directional()) < 0.001f,IsTrue());
                AssertThat(glm::distance(before.angular(),
                    owner->boardComputer()->engineState().angular()) < 0.001f,IsTrue());
                AssertThat(owner->boardComputer()->randomFloat(0,1),
                    Equals(target->boardComputer()->randomFloat(0,1)));
                world.god().spawn();
            }
        });

        it("calibrates accuracy with real pulse projectiles against stationary voxel and spherical targets", []() {
            for (const char* definition : {"contractraider","contractship"}) {
                for (float accuracy : {0.0f,0.5f,1.0f}) {
                    World world(World::Mode::Server);
                    auto* owner = buildTarget(world,"contractraider",glm::vec3(0));
                    auto* target = buildTarget(world,definition,glm::vec3(0,0,-300));
                    target->info().setInvulnerable(true);
                    for (const auto& mount : owner->components().hardpoints())
                        if (mount != owner->components().hardpoints().front()) mount->setWeapon(nullptr);
                    auto& factions = world.factionMatrix();
                    owner->character()->setFaction(factions.policeFaction());
                    target->character()->setFaction(factions.pirateFaction());
                    factions.getRelation(factions.policeFaction(),factions.pirateFaction()).setFriendliness(-100);
                    world.god().spawn();
                    std::vector<Handle<WorldObject>> targets{target->handle<WorldObject>()};
                    ShootingController shooting(owner->boardComputer(),{1,1,accuracy});
                    shooting.seed(8101);
                    const auto statistics = shooting.statistics();
                    const std::uint64_t samples = accuracy == 1 ? 200 : accuracy == 0 ? 2000 : 1000;
                    for (int tick = 0; tick < int(samples)*30 && statistics->shots < samples; ++tick) {
                        shooting.update(1.0f/30,targets,target);
                        world.update(1.0f/30);
                    }
                    AssertThat(statistics->shots,Equals(samples));
                    for (int tick = 0; tick < 60; ++tick) world.update(1.0f/30);
                    const double rate = double(statistics->hits)/statistics->shots;
                    std::cout << "AI_ACCURACY_REFERENCE target=" << definition <<
                        " accuracy=" << accuracy << " shots=" <<
                        statistics->shots << " hits=" << statistics->hits << " hit_rate=" << rate << std::endl;
                    if (accuracy == 1) {
                        AssertThat(statistics->hits,Equals(statistics->shots));
                    } else if (accuracy == 0) {
                        AssertThat(std::abs(rate-0.1) < 0.02,IsTrue());
                    } else {
                        AssertThat(rate > 0.25 && rate < 0.8,IsTrue());
                    }
                }
            }
        });

        it("counts a scatter volley once even when several pellets hit its designated opponent", []() {
            World world(World::Mode::Server);
            auto* owner = buildTarget(world,"contractraider",glm::vec3(0));
            auto* target = buildTarget(world,"contractship",glm::vec3(0,0,-100));
            target->info().setInvulnerable(true);
            for (const auto& mount : owner->components().hardpoints())
                mount->setWeapon(mount == owner->components().hardpoints().front() ?
                    std::shared_ptr<Weapon>(WeaponBuilder("scattercannon").build()) : nullptr);
            auto& factions = world.factionMatrix();
            owner->character()->setFaction(factions.policeFaction());
            target->character()->setFaction(factions.pirateFaction());
            factions.getRelation(factions.policeFaction(),factions.pirateFaction()).setFriendliness(-100);
            world.god().spawn();
            std::vector<Handle<WorldObject>> targets{target->handle<WorldObject>()};
            ShootingController shooting(owner->boardComputer(),{1,1,1});
            shooting.update(1.0f/30,targets,target);
            AssertThat(shooting.statistics()->shots,Equals(std::uint64_t(1)));
            for (int tick = 0; tick < 60; ++tick) world.update(1.0f/30);
            AssertThat(shooting.statistics()->hits,Equals(std::uint64_t(1)));
        });
    });
});
