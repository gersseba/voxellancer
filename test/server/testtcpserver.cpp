#include <bandit/bandit.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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
#include "voxel/voxel.h"
#include "voxel/specialvoxels/hardpointvoxel.h"
#include "world/god.h"
#include "world/helper/damageimpact.h"
#include "world/world.h"
#include "worldobject/ship.h"
#include "world/worldlogic.h"
#include "worldobject/ship.h"
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
}

go_bandit([]() {
    describe("TCP simulation client and server", []() {
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
                session.advance(1.0 / 60.0);
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
                     client.snapshotTick() < commandTick);
                 ++tick) {
                session.advance(1.0 / 60.0);
                server.poll();
                AssertThat(client.poll(), IsTrue());
            }
            AssertThat(client.snapshotSequence() > initialSnapshotSequence, IsTrue());
            AssertThat(client.snapshotTick() >= commandTick, IsTrue());
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
            session.advance(1.0 / 60.0);
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
                session.advance(1.0 / 60.0);
            }

            server.poll();
            session.advance(1.0 / 60.0);
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
                session.advance(1.0 / 60.0);
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
                session.advance(1.0 / 60.0);
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
                session.advance(1.0 / 60.0);
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
                session.advance(1.0 / 60.0);
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
                session.advance(1.0 / 60.0);
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
                 attempt < 30 && firstClient.snapshotTick() < command.tick;
                 ++attempt) {
                session.advance(1.0 / 60.0);
                server.poll();
                AssertThat(firstClient.poll(), IsTrue());
                AssertThat(secondClient.poll(), IsTrue());
            }
            AssertThat(firstClient.snapshotTick() >= command.tick, IsTrue());

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
            session.advance(1.0 / 60.0);
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
                session.advance(1.0 / 60.0);
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
                session.advance(1.0 / 60.0);
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
                session.advance(1.0 / 60.0);
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
                session.advance(1.0 / 60.0);
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
                session.advance(1.0 / 60.0);
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
                session.advance(1.0 / 60.0);
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

            session.advance(1.0 / 60.0);
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
            session.advance(1.0 / 60.0);
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
                session.advance(1.0 / 60.0);
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
            session.advance(1.0 / 60.0);

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
                session.advance(1.0 / 60.0);
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
                session.advance(1.0 / 60.0);
                server.poll();
                AssertThat(client.poll(), IsTrue());
            }
            AssertThat(contains(nearbyId), IsTrue());
            AssertThat(contains(distantId), IsFalse());
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
                session.advance(1.0 / 60.0);
                server.poll();
                AssertThat(client.poll(), IsTrue());
            }
            AssertThat(contains(distantId), IsTrue());
            AssertThat(hasContent(distantId), IsTrue());

            distantShip->transform().setPosition(glm::vec3(1000.0f, 0.0f, 0.0f));
            for (int tick = 0; tick < 3; ++tick) {
                session.advance(1.0 / 60.0);
                server.poll();
                AssertThat(client.poll(), IsTrue());
            }
            AssertThat(contains(distantId), IsFalse());
            AssertThat(hasContent(distantId), IsFalse());
            client.disconnect();
        });
    });
});
