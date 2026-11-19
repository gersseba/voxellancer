#include <bandit/bandit.h>

#include <stdexcept>
#include <vector>

#include "equipment/enginestate.h"
#include "property/propertymanager.h"
#include "simulation/simulationsession.h"
#include "utils/simulationtickrunner.h"
#include "voxel/voxel.h"
#include "world/helper/damageimpact.h"
#include "world/god.h"
#include "world/world.h"
#include "world/worldlogic.h"
#include "worldobject/worldobject.h"
#include "worldobject/worldobjectcomponents.h"
#include "worldobject/ship.h"

using namespace bandit;

namespace {
    struct WorldResetGuard {
        ~WorldResetGuard() {
            World::reset(false);
        }
    };
}

go_bandit([]() {
    describe("simulation session", []() {
        it("advances its world in fixed steps", []() {
            PropertyManager::instance()->load("data/config.ini");
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);
            SimulationSession session(*world);

            bool presentationAccessRejected = false;
            try {
                world->player();
            } catch (const std::logic_error&) {
                presentationAccessRejected = true;
            }

            for (std::uint64_t sequence = 1; sequence <= 1024; ++sequence) {
                AssertThat(
                    session.submitInputCommand(
                        voxellancer::protocol::EntityId{1, 1},
                        voxellancer::protocol::InputCommand{sequence}),
                    Equals(voxellancer::protocol::InputCommandResult::Queued)
                );
            }
            AssertThat(
                session.submitInputCommand(
                    voxellancer::protocol::EntityId{1, 1},
                    voxellancer::protocol::InputCommand{1025}),
                Equals(voxellancer::protocol::InputCommandResult::QueueFull)
            );
            const std::uint32_t ticksRun = session.advance(
                2.0 * SimulationTickRunner::TickDurationSeconds);

            AssertThat(world->hasPresentation(), IsFalse());
            AssertThat(presentationAccessRejected, IsTrue());
            AssertThat(ticksRun, Equals(static_cast<std::uint32_t>(2)));
            AssertThat(session.tickIndex(), Equals(static_cast<std::uint64_t>(2)));
            AssertThat(
                world->deltaSec(),
                EqualsWithDelta(
                    static_cast<float>(SimulationTickRunner::TickDurationSeconds),
                    1e-7f));
            const auto acknowledgements = session.drainInputCommandAcknowledgements();
            AssertThat(acknowledgements.size(), Equals(static_cast<std::size_t>(1024)));
            AssertThat(
                acknowledgements[0].result,
                Equals(voxellancer::protocol::InputCommandResult::UnknownActor)
            );
        });

        it("assigns generation-protected IDs through world lifecycle", []() {
            PropertyManager::instance()->load("data/config.ini");
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);

            WorldObject* first = new WorldObject();
            first->addVoxel(new Voxel(glm::ivec3(0), 0xffffff));
            world->god().scheduleSpawn(first);
            world->god().spawn();
            const auto firstId = world->entityId(first);
            AssertThat(firstId.has_value(), IsTrue());
            AssertThat(world->worldObject(*firstId), Equals(first));

            world->god().scheduleRemoval(first);
            world->god().remove();
            AssertThat(world->entityId(first).has_value(), IsFalse());
            AssertThat(world->worldObject(*firstId) == nullptr, IsTrue());

            WorldObject* second = new WorldObject();
            second->addVoxel(new Voxel(glm::ivec3(0), 0xffffff));
            world->god().scheduleSpawn(second);
            world->god().spawn();
            const auto secondId = world->entityId(second);
            AssertThat(secondId.has_value(), IsTrue());
            AssertThat(secondId->index, Equals(firstId->index));
            AssertThat(secondId->generation, Equals(firstId->generation + 1));

            world->god().scheduleRemoval(second);
            world->god().remove();
        });

        it("applies sequenced ship input at the assigned simulation tick", []() {
            PropertyManager::instance()->load("data/config.ini");
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);

            Ship* ship = new Ship(*world);
            ship->addVoxel(new Voxel(glm::ivec3(0), 0xffffff));
            ship->transform().setScale(2.0f);
            world->god().scheduleSpawn(ship);
            world->god().spawn();
            const auto actorId = world->entityId(ship);
            AssertThat(actorId.has_value(), IsTrue());

            SimulationSession session(*world);
            voxellancer::protocol::SnapshotFrame fullSnapshot;
            AssertThat(
                session.createSnapshotFrame(1, 0, {}, fullSnapshot),
                Equals(true)
            );
            AssertThat(fullSnapshot.updates.size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(fullSnapshot.updates[0].id, Equals(*actorId));
            AssertThat(
                fullSnapshot.updates[0].kind,
                Equals(voxellancer::protocol::EntitySnapshot::Kind::Ship)
            );
            AssertThat(fullSnapshot.updates[0].hullFraction, Equals(1.0f));
            AssertThat(fullSnapshot.updates[0].scale, Equals(2.0f));

            const voxellancer::protocol::InputCommand command{
                1, 0, 32767, 0, 0, 0, 0, 0, 0
            };
            AssertThat(
                session.submitInputCommand(*actorId, command),
                Equals(voxellancer::protocol::InputCommandResult::Queued)
            );
            AssertThat(
                session.advance(SimulationTickRunner::TickDurationSeconds),
                Equals(static_cast<std::uint32_t>(1)));
            const auto acknowledgements = session.drainInputCommandAcknowledgements();
            AssertThat(acknowledgements.size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(
                acknowledgements[0].result,
                Equals(voxellancer::protocol::InputCommandResult::Accepted)
            );
            AssertThat(
                ship->components().engineState().directional().x,
                EqualsWithDelta(1.0f, 1e-6f)
            );
            voxellancer::protocol::SnapshotFrame inputStateDelta;
            AssertThat(
                session.createSnapshotFrame(
                    2, 1, fullSnapshot.updates, inputStateDelta),
                Equals(true)
            );
            AssertThat(
                inputStateDelta.updates.size(),
                Equals(static_cast<std::size_t>(1))
            );
            AssertThat(
                inputStateDelta.updates.front().engineState.x,
                Equals(1.0f));
            world->god().scheduleRemoval(ship);
            world->god().remove();
        });

        it("coalesces repeated voxel health updates before replication", []() {
            PropertyManager::instance()->load("data/config.ini");
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);

            WorldObject* object = new WorldObject();
            object->addVoxel(new Voxel(glm::ivec3(0), 0xffffff, 1.0f, 10.0f));
            world->god().scheduleSpawn(object);
            world->god().spawn();
            Voxel* voxel = object->voxel(glm::ivec3(0));
            world->worldLogic().setVoxelReplicationEnabled(true);
            world->worldLogic().processDamageImpacts({
                DamageImpact(object, voxel, glm::vec3(2.0f, 0.0f, 0.0f), 1.0f)
            });
            world->worldLogic().processDamageImpacts({
                DamageImpact(object, voxel, glm::vec3(2.0f, 0.0f, 0.0f), 1.0f)
            });

            SimulationSession session(*world);
            AssertThat(
                session.advance(SimulationTickRunner::TickDurationSeconds),
                Equals(static_cast<std::uint32_t>(1)));
            const auto damageEvents = session.drainVoxelDamageEvents();
            AssertThat(damageEvents.size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(damageEvents.front().hp, Equals(6.0f));
            std::vector<voxellancer::protocol::EntitySnapshot> snapshot;
            AssertThat(session.createSnapshotState(snapshot), IsTrue());
            AssertThat(snapshot.size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(snapshot.front().hullFraction, Equals(0.6f));

            world->god().scheduleRemoval(object);
            world->god().remove();
        });

        it("clamps replicated engine state to the protocol range", []() {
            PropertyManager::instance()->load("data/config.ini");
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);
            Ship* ship = new Ship(*world);
            ship->components().setEngineState(EngineState(
                glm::vec3(0.0f, 0.0f, -1.000001f),
                glm::vec3(0.0f)));
            world->god().scheduleSpawn(ship);
            world->god().spawn();

            SimulationSession session(*world);
            std::vector<voxellancer::protocol::EntitySnapshot> snapshot;
            AssertThat(session.createSnapshotState(snapshot), IsTrue());
            AssertThat(snapshot.size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(snapshot.front().engineState.z, Equals(-1.0f));

            world->god().scheduleRemoval(ship);
            world->god().remove();
        });
    });
});
