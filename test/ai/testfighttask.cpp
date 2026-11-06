#include <bandit/bandit.h>

#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "ai/basictasks/fighttask.h"
#include "ai/squad.h"
#include "ai/squadlogic.h"
#include "property/propertydirectory.h"
#include "property/propertymanager.h"
#include "resource/worldobjectbuilder.h"
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
    });
});
