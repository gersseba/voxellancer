#include <bandit/bandit.h>
#include <limits>

#include "protocol/serverprotocol.h"

using namespace bandit;
using namespace voxellancer::protocol;

go_bandit([]() {
    describe("server protocol values", []() {
        it("round trips entity IDs and input commands", []() {
            const EntityId originalId{0x12345678, 0x90abcdef};
            EntityIdPacket idPacket{};
            AssertThat(encodeEntityId(originalId, idPacket), Equals(true));
            AssertThat(idPacket[0], Equals(ProtocolVersion));
            AssertThat(idPacket[1], Equals(static_cast<std::uint8_t>(1)));
            AssertThat(idPacket[2], Equals(static_cast<std::uint8_t>(0x78)));
            AssertThat(idPacket[5], Equals(static_cast<std::uint8_t>(0x12)));
            AssertThat(idPacket[6], Equals(static_cast<std::uint8_t>(0xef)));
            AssertThat(idPacket[9], Equals(static_cast<std::uint8_t>(0x90)));

            EntityId decodedId;
            AssertThat(
                decodeEntityId(idPacket.data(), idPacket.size(), decodedId),
                Equals(DecodeError::None)
            );
            AssertThat(decodedId, Equals(originalId));

            const InputCommand originalCommand{
                0x0102030405060708,
                0x1112131415161718,
                -32768,
                -1,
                0,
                1,
                32767,
                -1234,
                FirePrimary | FireSecondary,
                EntityId{0x10203040, 0x50607080}
            };
            InputCommandPacket commandPacket{};
            AssertThat(encodeInputCommand(originalCommand, commandPacket), Equals(true));
            AssertThat(commandPacket[0], Equals(ProtocolVersion));
            AssertThat(commandPacket[1], Equals(static_cast<std::uint8_t>(2)));
            AssertThat(commandPacket[2], Equals(static_cast<std::uint8_t>(0x08)));
            AssertThat(commandPacket[9], Equals(static_cast<std::uint8_t>(0x01)));
            AssertThat(commandPacket[18], Equals(static_cast<std::uint8_t>(0x00)));
            AssertThat(commandPacket[19], Equals(static_cast<std::uint8_t>(0x80)));
            AssertThat(commandPacket[32], Equals(static_cast<std::uint8_t>(0x40)));
            AssertThat(commandPacket[39], Equals(static_cast<std::uint8_t>(0x50)));

            InputCommand decodedCommand;
            AssertThat(
                decodeInputCommand(
                    commandPacket.data(),
                    commandPacket.size(),
                    decodedCommand
                ),
                Equals(DecodeError::None)
            );
            AssertThat(decodedCommand, Equals(originalCommand));
        });

        it("round trips and validates multiplayer lobby status", []() {
            const LobbyStatus original{2, 4, false};
            LobbyStatusPacket packet{};
            AssertThat(encodeLobbyStatus(original, packet), Equals(true));
            AssertThat(packet[0], Equals(ProtocolVersion));
            AssertThat(packet[1], Equals(static_cast<std::uint8_t>(13)));

            LobbyStatus decoded;
            AssertThat(
                decodeLobbyStatus(packet.data(), packet.size(), decoded),
                Equals(DecodeError::None)
            );
            AssertThat(decoded, Equals(original));
            packet[6] = 2;
            AssertThat(
                decodeLobbyStatus(packet.data(), packet.size(), decoded),
                Equals(DecodeError::InvalidValue)
            );
            AssertThat(
                encodeLobbyStatus({3, 2, false}, packet),
                Equals(false)
            );
            AssertThat(
                encodeLobbyStatus({1, 2, true}, packet),
                Equals(true)
            );
            AssertThat(
                decodeLobbyStatus(packet.data(), packet.size(), decoded),
                Equals(DecodeError::None)
            );
            AssertThat(decoded, Equals(LobbyStatus{1, 2, true}));
        });

        it("round trips sequenced input acknowledgements", []() {
            const InputCommandAcknowledgement original{
                42, InputCommandResult::TooLate
            };
            InputCommandAcknowledgementPacket packet{};
            AssertThat(
                encodeInputCommandAcknowledgement(original, packet),
                Equals(true)
            );
            AssertThat(packet[0], Equals(ProtocolVersion));
            AssertThat(packet[1], Equals(static_cast<std::uint8_t>(4)));

            InputCommandAcknowledgement decoded;
            AssertThat(
                decodeInputCommandAcknowledgement(
                    packet.data(), packet.size(), decoded),
                Equals(DecodeError::None)
            );
            AssertThat(decoded, Equals(original));
            packet[10] = 0xff;
            AssertThat(
                decodeInputCommandAcknowledgement(
                    packet.data(), packet.size(), decoded),
                Equals(DecodeError::InvalidValue)
            );
        });

        it("encodes and validates snapshot resynchronization requests", []() {
            SnapshotResyncRequestPacket packet{};
            AssertThat(encodeSnapshotResyncRequest(packet), Equals(true));
            AssertThat(packet[0], Equals(ProtocolVersion));
            AssertThat(packet[1], Equals(static_cast<std::uint8_t>(5)));
            AssertThat(
                decodeSnapshotResyncRequest(packet.data(), packet.size()),
                Equals(DecodeError::None)
            );
            packet[1] = 0xff;
            AssertThat(
                decodeSnapshotResyncRequest(packet.data(), packet.size()),
                Equals(DecodeError::InvalidMessageType)
            );
        });

        it("round trips authoritative voxel removal events", []() {
            const VoxelRemovalEvent original{
                EntityId{0x12345678, 0x90abcdef},
                glm::ivec3(-120, 45, -1)
            };
            VoxelRemovalPacket packet{};
            AssertThat(encodeVoxelRemovalEvent(original, packet), Equals(true));
            AssertThat(packet[0], Equals(ProtocolVersion));
            AssertThat(packet[1], Equals(static_cast<std::uint8_t>(6)));

            VoxelRemovalEvent decoded;
            AssertThat(
                decodeVoxelRemovalEvent(packet.data(), packet.size(), decoded),
                Equals(DecodeError::None)
            );
            AssertThat(decoded, Equals(original));

            packet[1] = 0xff;
            AssertThat(
                decodeVoxelRemovalEvent(packet.data(), packet.size(), decoded),
                Equals(DecodeError::InvalidMessageType)
            );
            AssertThat(encodeVoxelRemovalEvent({EntityId{1, 0}}, packet), Equals(false));
        });

        it("round trips authoritative voxel damage events", []() {
            const VoxelDamageEvent original{
                EntityId{0x12345678, 0x90abcdef},
                glm::ivec3(-120, 45, -1),
                17.25f
            };
            VoxelDamagePacket packet{};
            AssertThat(encodeVoxelDamageEvent(original, packet), Equals(true));
            AssertThat(packet[0], Equals(ProtocolVersion));
            AssertThat(packet[1], Equals(static_cast<std::uint8_t>(8)));

            VoxelDamageEvent decoded;
            AssertThat(
                decodeVoxelDamageEvent(packet.data(), packet.size(), decoded),
                Equals(DecodeError::None)
            );
            AssertThat(decoded, Equals(original));

            packet[1] = 0xff;
            AssertThat(
                decodeVoxelDamageEvent(packet.data(), packet.size(), decoded),
                Equals(DecodeError::InvalidMessageType)
            );
            AssertThat(
                encodeVoxelDamageEvent(
                    VoxelDamageEvent{EntityId{1, 1}, glm::ivec3(0), -1.0f},
                    packet),
                Equals(false)
            );
            AssertThat(
                encodeVoxelDamageEvent(
                    VoxelDamageEvent{
                        EntityId{1, 1},
                        glm::ivec3(0),
                        std::numeric_limits<float>::quiet_NaN()
                    },
                    packet),
                Equals(false)
            );
        });

        it("round trips authoritative shield-hit events", []() {
            const ShieldHitEvent original{
                EntityId{0x12345678, 0x90abcdef},
                987654321,
                glm::ivec3(-120, 45, -1),
                glm::vec3(-12.5f, 45.25f, -0.75f),
                0xff2200,
                17.25f
            };
            ShieldHitEventPacket packet{};
            AssertThat(encodeShieldHitEvent(original, packet), Equals(true));
            AssertThat(packet[0], Equals(ProtocolVersion));
            AssertThat(packet[1], Equals(static_cast<std::uint8_t>(11)));

            ShieldHitEvent decoded;
            AssertThat(
                decodeShieldHitEvent(packet.data(), packet.size(), decoded),
                Equals(DecodeError::None)
            );
            AssertThat(decoded, Equals(original));
            AssertThat(
                decodeShieldHitEvent(
                    packet.data(), packet.size() - 1, decoded),
                Equals(DecodeError::InvalidLength)
            );
            packet[46] = 0;
            packet[47] = 0;
            packet[48] = 0;
            packet[49] = 0;
            AssertThat(
                decodeShieldHitEvent(packet.data(), packet.size(), decoded),
                Equals(DecodeError::InvalidValue)
            );
            AssertThat(
                encodeShieldHitEvent(
                    ShieldHitEvent{
                        EntityId{1, 1},
                        0,
                        glm::ivec3(0),
                        glm::vec3(0.0f),
                        0xff0000,
                        -1.0f
                    },
                    packet),
                Equals(false)
            );
            AssertThat(
                encodeShieldHitEvent(
                    ShieldHitEvent{
                        EntityId{1, 1},
                        0,
                        glm::ivec3(0),
                            glm::vec3(0.0f),
                            0xff0000,
                            std::numeric_limits<float>::quiet_NaN()
                        },
                    packet),
                    Equals(false)
            );
            AssertThat(
                    encodeShieldHitEvent(
                        ShieldHitEvent{
                            EntityId{1, 1},
                            0,
                            glm::ivec3(0),
                            glm::vec3(
                                std::numeric_limits<float>::quiet_NaN(),
                                0.0f,
                                0.0f),
                            0xff0000,
                            1.0f
                        },
                        packet),
                    Equals(false)
            );
        });

        it("round trips bounded mission-state events", []() {
            const MissionStateEvent original{
                987654321,
                MissionEventState::Succeeded,
                "data/scripts/missions/intro.lua"
            };
            MissionStateEventPacket packet{};
            AssertThat(encodeMissionStateEvent(original, packet), Equals(true));
            AssertThat(packet[0], Equals(ProtocolVersion));
            AssertThat(packet[1], Equals(static_cast<std::uint8_t>(12)));

            MissionStateEvent decoded;
            AssertThat(
                decodeMissionStateEvent(packet.data(), packet.size(), decoded),
                Equals(DecodeError::None)
            );
            AssertThat(decoded, Equals(original));
            AssertThat(
                decodeMissionStateEvent(
                    packet.data(), packet.size() - 1, decoded),
                Equals(DecodeError::InvalidLength)
            );

            packet[10] = 0xff;
            AssertThat(
                decodeMissionStateEvent(packet.data(), packet.size(), decoded),
                Equals(DecodeError::InvalidValue)
            );
            AssertThat(
                encodeMissionStateEvent(
                    MissionStateEvent{0, MissionEventState::Started, ""},
                    packet),
                Equals(false)
            );
            AssertThat(
                encodeMissionStateEvent(
                    MissionStateEvent{
                        0,
                        MissionEventState::Started,
                        std::string(MaxMissionKeyBytes + 1, 'a')
                    },
                    packet),
                Equals(false)
            );
        });

        it("round trips authoritative weapon-fired events", []() {
            const WeaponFiredEvent original{
                EntityId{0x12345678, 0x90abcdef},
                987654321,
                17,
                glm::ivec3(-120, 45, -1)
            };
            WeaponFiredEventPacket packet{};
            AssertThat(encodeWeaponFiredEvent(original, packet), Equals(true));
            AssertThat(packet[0], Equals(ProtocolVersion));
            AssertThat(packet[1], Equals(static_cast<std::uint8_t>(10)));

            WeaponFiredEvent decoded;
            AssertThat(
                decodeWeaponFiredEvent(packet.data(), packet.size(), decoded),
                Equals(DecodeError::None)
            );
            AssertThat(decoded, Equals(original));
            AssertThat(
                decodeWeaponFiredEvent(
                    packet.data(), packet.size() - 1, decoded),
                Equals(DecodeError::InvalidLength)
            );
            AssertThat(
                encodeWeaponFiredEvent(
                    WeaponFiredEvent{EntityId{1, 0}, 0, 0, glm::ivec3(0)},
                    packet),
                Equals(false)
            );
        });

        it("round trips voxel and equipment content", []() {
            const EntityContentSnapshot original{
                EntityId{0x12345678, 0x90abcdef},
                {
                    EntityVoxelState{
                        glm::ivec3(-12, 4, 7), 0xabcdef, 1.5f, 20.0f, 20.0f
                    },
                    EntityVoxelState{
                        glm::ivec3(2, 0, -3), 0x123456, 0.5f, 8.0f, 12.0f
                    }
                },
                {
                    EntityEquipmentState{
                        EquipmentSlotKind::Engine, 2, glm::ivec3(-1, 0, 3), "ion"
                    },
                    EntityEquipmentState{
                        EquipmentSlotKind::Weapon, 1, glm::ivec3(4, 1, -2), "pulsegun"
                    },
                    EntityEquipmentState{
                        EquipmentSlotKind::Shield, 0, glm::ivec3(0), ""
                    }
                },
                "basicship"
            };
            std::vector<std::uint8_t> packet;
            AssertThat(encodeEntityContent(original, packet), Equals(true));
            AssertThat(
                packet.size(),
                Equals(
                    EntityContentHeaderSize +
                    original.definitionKey.size() +
                    2 * EntityVoxelStateSize +
                    3 * EntityEquipmentStateHeaderSize +
                    3 + 8
                )
            );
            AssertThat(packet[0], Equals(ProtocolVersion));
            AssertThat(packet[1], Equals(static_cast<std::uint8_t>(7)));
            AssertThat(packet[14], Equals(
                static_cast<std::uint8_t>(original.definitionKey.size())));

            EntityContentSnapshot decoded;
            AssertThat(
                decodeEntityContent(packet.data(), packet.size(), decoded),
                Equals(DecodeError::None)
            );
            AssertThat(decoded.id, Equals(original.id));
            AssertThat(decoded.definitionKey, Equals(original.definitionKey));
            AssertThat(decoded.voxels, Equals(original.voxels));
            AssertThat(decoded.equipment, Equals(original.equipment));

            std::vector<std::uint8_t> invalidKindPacket = packet;
            invalidKindPacket[
                EntityContentHeaderSize + original.definitionKey.size() +
                2 * EntityVoxelStateSize
            ] = 0;
            AssertThat(
                decodeEntityContent(
                    invalidKindPacket.data(), invalidKindPacket.size(), decoded),
                Equals(DecodeError::InvalidValue)
            );
            std::vector<std::uint8_t> invalidDefinitionPacket = packet;
            invalidDefinitionPacket[14] =
                static_cast<std::uint8_t>(MaxEntityDefinitionKeyBytes + 1);
            AssertThat(
                decodeEntityContent(
                    invalidDefinitionPacket.data(),
                    invalidDefinitionPacket.size(),
                    decoded),
                Equals(DecodeError::InvalidValue)
            );
            EntityContentSnapshot oversizedKey = original;
            oversizedKey.equipment.front().equipmentKey =
                std::string(MaxEquipmentKeyBytes + 1, 'x');
            AssertThat(encodeEntityContent(oversizedKey, packet), Equals(false));
            EntityContentSnapshot oversizedDefinition = original;
            oversizedDefinition.definitionKey =
                std::string(MaxEntityDefinitionKeyBytes + 1, 'x');
            AssertThat(
                encodeEntityContent(oversizedDefinition, packet),
                Equals(false));

            packet.pop_back();
            AssertThat(
                decodeEntityContent(packet.data(), packet.size(), decoded),
                Equals(DecodeError::InvalidLength)
            );
            AssertThat(
                encodeEntityContent(
                    EntityContentSnapshot{
                        EntityId{1, 1},
                        {
                            EntityVoxelState{glm::ivec3(0), 0xffffff, 1.0f, 1.0f, 1.0f},
                            EntityVoxelState{glm::ivec3(0), 0xffffff, 1.0f, 1.0f, 1.0f}
                        }
                    },
                    packet),
                Equals(false)
            );
            AssertThat(
                encodeEntityContent(
                    EntityContentSnapshot{
                        EntityId{1, 1},
                        {
                            EntityVoxelState{
                                glm::ivec3(0), 0xffffff, 1.0f, 2.0f, 1.0f
                            }
                        }
                    },
                    packet),
                Equals(false)
            );
            AssertThat(
                encodeEntityContent(
                    EntityContentSnapshot{
                        EntityId{1, 1},
                        {},
                        {
                            EntityEquipmentState{
                                EquipmentSlotKind::Engine, 0, glm::ivec3(1), "engine"
                            },
                            EntityEquipmentState{
                                EquipmentSlotKind::Engine, 0, glm::ivec3(1), "other"
                            }
                        }
                    },
                    packet),
                Equals(false)
            );
        });

        it("round trips equipment-only updates", []() {
            const EntityEquipmentSnapshot original{
                EntityId{7, 3},
                {
                    EntityEquipmentState{
                        EquipmentSlotKind::Engine, 1, glm::ivec3(2, 0, -1), ""
                    },
                    EntityEquipmentState{
                        EquipmentSlotKind::Weapon, 0, glm::ivec3(0, 1, 4), "gun"
                    }
                }
            };
            std::vector<std::uint8_t> packet;
            AssertThat(
                encodeEntityEquipmentSnapshot(original, packet),
                Equals(true)
            );
            AssertThat(
                packet.size(),
                Equals(
                    EntityEquipmentSnapshotHeaderSize +
                    2 * EntityEquipmentStateHeaderSize +
                    3
                )
            );

            EntityEquipmentSnapshot decoded;
            AssertThat(
                decodeEntityEquipmentSnapshot(
                    packet.data(), packet.size(), decoded),
                Equals(DecodeError::None)
            );
            AssertThat(decoded.id, Equals(original.id));
            AssertThat(decoded.equipment, Equals(original.equipment));

            std::vector<std::uint8_t> invalidKindPacket = packet;
            invalidKindPacket[EntityEquipmentSnapshotHeaderSize] = 0;
            AssertThat(
                decodeEntityEquipmentSnapshot(
                    invalidKindPacket.data(),
                    invalidKindPacket.size(),
                    decoded),
                Equals(DecodeError::InvalidValue)
            );

            packet.pop_back();
            AssertThat(
                decodeEntityEquipmentSnapshot(
                    packet.data(), packet.size(), decoded),
                Equals(DecodeError::InvalidLength)
            );

            const EntityEquipmentSnapshot empty{EntityId{7, 3}, {}};
            AssertThat(encodeEntityEquipmentSnapshot(empty, packet), Equals(true));
            AssertThat(
                decodeEntityEquipmentSnapshot(
                    packet.data(), packet.size(), decoded),
                Equals(DecodeError::None)
            );
            AssertThat(decoded.equipment.empty(), IsTrue());
        });

        it("rejects unsupported protocol versions", []() {
            InputCommandPacket packet{};
            AssertThat(
                encodeInputCommand(InputCommand{1}, packet),
                Equals(true)
            );
            packet[0] = static_cast<std::uint8_t>(ProtocolVersion + 1);

            InputCommand decoded;
            AssertThat(
                decodeInputCommand(packet.data(), packet.size(), decoded),
                Equals(DecodeError::InvalidVersion)
            );
        });

        it("rejects malformed, truncated, and invalid-value packets", []() {
            EntityIdPacket idPacket{};
            AssertThat(encodeEntityId(EntityId{4, 2}, idPacket), Equals(true));
            EntityId decodedId{9, 9};
            AssertThat(
                decodeEntityId(idPacket.data(), idPacket.size() - 1, decodedId),
                Equals(DecodeError::InvalidLength)
            );
            AssertThat(decodedId, Equals(EntityId{9, 9}));

            idPacket[1] = 0xff;
            AssertThat(
                decodeEntityId(idPacket.data(), idPacket.size(), decodedId),
                Equals(DecodeError::InvalidMessageType)
            );

            AssertThat(encodeEntityId(EntityId{4, 0}, idPacket), Equals(false));
            idPacket[1] = 1;
            idPacket[6] = 0;
            idPacket[7] = 0;
            idPacket[8] = 0;
            idPacket[9] = 0;
            AssertThat(
                decodeEntityId(idPacket.data(), idPacket.size(), decodedId),
                Equals(DecodeError::InvalidValue)
            );

            InputCommandPacket commandPacket{};
            AssertThat(encodeInputCommand(InputCommand{1}, commandPacket), Equals(true));
            commandPacket[30] = 0x80;
            commandPacket[31] = 0;
            InputCommand decodedCommand;
            AssertThat(
                decodeInputCommand(
                    commandPacket.data(),
                    commandPacket.size(),
                    decodedCommand
                ),
                Equals(DecodeError::InvalidValue)
            );
            AssertThat(
                decodeInputCommand(nullptr, InputCommandPacketSize, decodedCommand),
                Equals(DecodeError::InvalidLength)
            );
        });

        it("accepts only strictly increasing valid command sequences", []() {
            InputCommandSequencer sequencer;
            AssertThat(
                sequencer.accept(InputCommand{1}),
                Equals(InputCommandResult::Accepted)
            );
            AssertThat(
                sequencer.accept(InputCommand{1}),
                Equals(InputCommandResult::Duplicate)
            );
            AssertThat(
                sequencer.accept(InputCommand{3}),
                Equals(InputCommandResult::Accepted)
            );
            AssertThat(
                sequencer.accept(InputCommand{2}),
                Equals(InputCommandResult::OutOfOrder)
            );
            AssertThat(
                sequencer.accept(InputCommand{0}),
                Equals(InputCommandResult::Invalid)
            );
            AssertThat(sequencer.lastAcceptedSequence(), Equals(std::uint64_t{3}));
        });

        it("bounds and applies commands at their assigned ticks", []() {
            InputCommandBuffer buffer(2, 4);
            InputCommand first{1, 2, 100};
            InputCommand replacement{2, 2, 200};
            AssertThat(buffer.enqueue(first, 0), Equals(InputCommandResult::Accepted));
            AssertThat(buffer.enqueue(replacement, 0), Equals(InputCommandResult::Accepted));
            AssertThat(buffer.pendingCount(), Equals(static_cast<std::size_t>(1)));
            AssertThat(buffer.advanceToTick(1).has_value(), IsFalse());

            const auto applied = buffer.advanceToTick(2);
            AssertThat(applied.has_value(), IsTrue());
            AssertThat(applied->sequence, Equals(std::uint64_t{2}));
            AssertThat(applied->moveX, Equals(static_cast<std::int16_t>(200)));
            AssertThat(buffer.advanceToTick(3)->sequence, Equals(std::uint64_t{2}));

            AssertThat(
                buffer.enqueue(InputCommand{3, 2}, 3),
                Equals(InputCommandResult::TooLate)
            );
            AssertThat(
                buffer.enqueue(InputCommand{3, 10}, 3),
                Equals(InputCommandResult::TooFarAhead)
            );
            AssertThat(
                buffer.enqueue(InputCommand{3, 4}, 3),
                Equals(InputCommandResult::Accepted)
            );
            AssertThat(
                buffer.enqueue(InputCommand{4, 3}, 3),
                Equals(InputCommandResult::OutOfOrder)
            );
            AssertThat(
                buffer.enqueue(InputCommand{4, 5}, 3),
                Equals(InputCommandResult::Accepted)
            );
            AssertThat(
                buffer.enqueue(InputCommand{5, 6}, 3),
                Equals(InputCommandResult::QueueFull)
            );
        });

        it("encodes snapshot deltas and rejects malformed frames", []() {
            EntitySnapshot previous;
            previous.id = EntityId{1, 2};
            previous.position = glm::vec3(1.0f, 2.0f, 3.0f);
            previous.orientation = glm::quat(0.5f, -0.5f, 0.5f, -0.5f);
            previous.directionalVelocity = glm::vec3(4.0f, 5.0f, 6.0f);
            previous.angularVelocity = glm::vec3(0.1f, 0.2f, 0.3f);
            previous.kind = EntitySnapshot::Kind::Ship;
            previous.shieldFraction = 0.75f;
            previous.hullFraction = 0.5f;
            previous.scale = 2.5f;
            previous.maxMass = 42.0f;

            EntitySnapshot current = previous;
            current.position.x = 9.0f;
            current.engineState = glm::vec3(0.5f, -1.0f, 0.25f);
            const EntitySnapshot removed{
                EntityId{3, 1},
                glm::vec3(0.0f),
                glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                glm::vec3(0.0f),
                glm::vec3(0.0f)
            };

            SnapshotFrame initialFrame;
            AssertThat(
                buildSnapshotDelta(1, 59, 0, {previous, removed}, {}, initialFrame),
                Equals(true)
            );
            std::vector<EntitySnapshot> replicaState;
            AssertThat(
                applySnapshotFrame(initialFrame, 0, replicaState),
                Equals(true)
            );
            AssertThat(replicaState.size(), Equals(static_cast<std::size_t>(2)));

            SnapshotFrame frame;
            AssertThat(
                buildSnapshotDelta(
                    2, 60, 1, {current}, {previous, removed}, frame),
                Equals(true)
            );
            AssertThat(frame.updates.size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(frame.removals.size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(frame.removals[0], Equals(removed.id));
            AssertThat(applySnapshotFrame(frame, 1, replicaState), Equals(true));
            AssertThat(replicaState.size(), Equals(static_cast<std::size_t>(1)));
            AssertThat(replicaState[0], Equals(current));
            AssertThat(applySnapshotFrame(frame, 1, replicaState), Equals(false));
            AssertThat(replicaState[0], Equals(current));

            std::vector<std::uint8_t> packet;
            AssertThat(encodeSnapshotFrame(frame, packet), Equals(true));
            AssertThat(packet.size(), Equals(SnapshotHeaderSize +
                SnapshotEntitySize + SnapshotEntityIdSize));

            SnapshotFrame decoded;
            AssertThat(
                decodeSnapshotFrame(packet.data(), packet.size(), decoded),
                Equals(DecodeError::None)
            );
            AssertThat(decoded.sequence, Equals(frame.sequence));
            AssertThat(decoded.tick, Equals(frame.tick));
            AssertThat(decoded.baselineSequence, Equals(frame.baselineSequence));
            AssertThat(decoded.updates[0].id, Equals(current.id));
            AssertThat(decoded.updates[0].position.x, Equals(9.0f));
            AssertThat(decoded.updates[0].directionalVelocity.z, Equals(6.0f));
            AssertThat(decoded.updates[0].kind, Equals(EntitySnapshot::Kind::Ship));
            AssertThat(
                decoded.updates[0].shieldFraction,
                EqualsWithDelta(191.0f / 255.0f, 1e-6f)
            );
            AssertThat(
                decoded.updates[0].hullFraction,
                EqualsWithDelta(128.0f / 255.0f, 1e-6f)
            );
            AssertThat(decoded.updates[0].scale, Equals(2.5f));
            AssertThat(decoded.updates[0].maxMass, Equals(42.0f));
            AssertThat(
                decoded.updates[0].engineState.x,
                EqualsWithDelta(0.5f, 1.0f / 127.0f)
            );
            AssertThat(decoded.updates[0].engineState.y, Equals(-1.0f));
            AssertThat(
                decoded.updates[0].engineState.z,
                EqualsWithDelta(0.25f, 1.0f / 127.0f)
            );
            AssertThat(
                glm::abs(glm::dot(decoded.updates[0].orientation, current.orientation)),
                EqualsWithDelta(1.0f, 1e-4f)
            );
            AssertThat(decoded.removals[0], Equals(removed.id));
            std::vector<std::uint8_t> invalidQuaternionPacket = packet;
            invalidQuaternionPacket[53] = 0xfc;
            AssertThat(
                decodeSnapshotFrame(
                    invalidQuaternionPacket.data(),
                    invalidQuaternionPacket.size(),
                    decoded),
                Equals(DecodeError::InvalidValue)
            );
            EntitySnapshot invalidScale = current;
            invalidScale.scale = 0.0f;
            AssertThat(
                encodeSnapshotFrame(
                    SnapshotFrame{1, 60, 0, {invalidScale}, {}},
                    packet),
                Equals(false)
            );
            EntitySnapshot invalidMass = current;
            invalidMass.maxMass = -1.0f;
            AssertThat(
                encodeSnapshotFrame(
                    SnapshotFrame{1, 60, 0, {invalidMass}, {}},
                    packet),
                Equals(false)
            );
            EntitySnapshot invalidEngineState = current;
            invalidEngineState.engineState.x = 1.1f;
            AssertThat(
                encodeSnapshotFrame(
                    SnapshotFrame{1, 60, 0, {invalidEngineState}, {}},
                    packet),
                Equals(false)
            );
            AssertThat(
                decodeSnapshotFrame(packet.data(), packet.size() - 1, decoded),
                Equals(DecodeError::InvalidLength)
            );

            packet[0] = static_cast<std::uint8_t>(ProtocolVersion + 1);
            AssertThat(
                decodeSnapshotFrame(packet.data(), packet.size(), decoded),
                Equals(DecodeError::InvalidVersion)
            );
        });

        it("protects a reused entity slot with a new generation", []() {
            EntityIdAllocator allocator;
            const std::optional<EntityId> first = allocator.create();
            AssertThat(first.has_value(), Equals(true));
            AssertThat(allocator.isAlive(*first), Equals(true));
            AssertThat(allocator.release(*first), Equals(true));
            AssertThat(allocator.isAlive(*first), Equals(false));

            const std::optional<EntityId> reused = allocator.create();
            AssertThat(reused.has_value(), Equals(true));
            AssertThat(reused->index, Equals(first->index));
            AssertThat(reused->generation, Equals(first->generation + 1));
            AssertThat(*reused != *first, Equals(true));
            AssertThat(allocator.isAlive(*first), Equals(false));
            AssertThat(allocator.release(*first), Equals(false));
            AssertThat(allocator.isAlive(*reused), Equals(true));
        });
    });
});
