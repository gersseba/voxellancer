#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

namespace voxellancer {
namespace protocol {

constexpr std::uint8_t ProtocolVersion = 16;
constexpr std::size_t EntityIdPacketSize = 10;
constexpr std::size_t InputCommandPacketSize = 40;
constexpr std::size_t InputCommandAcknowledgementPacketSize = 11;
constexpr std::size_t SnapshotResyncRequestPacketSize = 2;
constexpr std::size_t VoxelRemovalPacketSize = 22;
constexpr std::size_t VoxelDamagePacketSize = 26;
constexpr std::size_t EntityContentHeaderSize = 15;
constexpr std::size_t EntityVoxelStateSize = 28;
constexpr std::size_t EntityEquipmentStateHeaderSize = 16;
constexpr std::size_t EntityEquipmentSnapshotHeaderSize = 12;
constexpr std::size_t WeaponFiredEventPacketSize = 32;
constexpr std::size_t ShieldHitEventPacketSize = 34;
constexpr std::size_t MissionStateEventPacketSize = 75;
constexpr std::size_t MaxEquipmentKeyBytes = 63;
constexpr std::size_t MaxEntityDefinitionKeyBytes = 63;
constexpr std::size_t MaxMissionKeyBytes = 63;
constexpr std::size_t SnapshotHeaderSize = 30;
constexpr std::size_t SnapshotEntitySize = 65;
constexpr std::size_t SnapshotEntityIdSize = 8;
constexpr std::size_t MaxSnapshotEntries = 4096;
constexpr std::size_t MaxEntityVoxels = 16000;
constexpr std::size_t MaxEntityEquipmentStates = 256;

struct EntityId {
    std::uint32_t index = 0;
    std::uint32_t generation = 0;

    bool valid() const;
    bool operator==(const EntityId& other) const;
    bool operator!=(const EntityId& other) const;
};

struct EntitySnapshot {
    EntityId id;
    glm::vec3 position{0.0f};
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 directionalVelocity{0.0f};
    glm::vec3 angularVelocity{0.0f};
    enum class Kind : std::uint8_t {
        Unknown,
        Ship,
        Bullet,
        Rocket,
        Other
    } kind = Kind::Unknown;
    float shieldFraction = 0.0f;
    float hullFraction = 0.0f;
    float scale = 1.0f;
    float maxMass = 0.0f;
    glm::vec3 engineState{0.0f};

    bool operator==(const EntitySnapshot& other) const;
};

struct VoxelRemovalEvent {
    EntityId entity;
    glm::ivec3 cell{0};

    bool operator==(const VoxelRemovalEvent& other) const;
};

struct VoxelDamageEvent {
    EntityId entity;
    glm::ivec3 cell{0};
    float hp = 0.0f;

    bool operator==(const VoxelDamageEvent& other) const;
};

struct ShieldHitEvent {
    EntityId entity;
    std::uint64_t tick = 0;
    glm::ivec3 cell{0};
    float absorbedDamage = 0.0f;

    bool operator==(const ShieldHitEvent& other) const;
};

enum class MissionEventState : std::uint8_t {
    Started = 1,
    Failed = 2,
    Succeeded = 3
};

struct MissionStateEvent {
    std::uint64_t tick = 0;
    MissionEventState state = MissionEventState::Started;
    std::string missionKey;

    bool operator==(const MissionStateEvent& other) const;
};

struct EntityVoxelState {
    glm::ivec3 cell{0};
    std::uint32_t color = 0xffffff;
    float density = 1.0f;
    float hp = 1.0f;
    float maxHP = 1.0f;

    bool operator==(const EntityVoxelState& other) const;
};

enum class EquipmentSlotKind : std::uint8_t {
    Engine = 1,
    Weapon = 2,
    Shield = 3
};

struct EntityEquipmentState {
    EquipmentSlotKind kind = EquipmentSlotKind::Engine;
    std::uint16_t group = 0;
    glm::ivec3 cell{0};
    std::string equipmentKey;

    bool operator==(const EntityEquipmentState& other) const;
};

struct EntityContentSnapshot {
    EntityId id;
    std::vector<EntityVoxelState> voxels;
    std::vector<EntityEquipmentState> equipment;
    std::string definitionKey;
};

struct EntityEquipmentSnapshot {
    EntityId id;
    std::vector<EntityEquipmentState> equipment;
};

struct WeaponFiredEvent {
    EntityId entity;
    std::uint64_t tick = 0;
    std::uint16_t group = 0;
    glm::ivec3 cell{0};

    bool operator==(const WeaponFiredEvent& other) const;
};

struct SnapshotFrame {
    std::uint64_t sequence = 0;
    std::uint64_t tick = 0;
    std::uint64_t baselineSequence = 0;
    std::vector<EntitySnapshot> updates;
    std::vector<EntityId> removals;
};

// IDs are process-independent values; do not serialize runtime pointers/handles.
class EntityIdAllocator {
public:
    std::optional<EntityId> create();
    bool release(EntityId id);
    bool isAlive(EntityId id) const;

private:
    struct Slot {
        std::uint32_t generation = 1;
        bool alive = false;
    };

    std::vector<Slot> m_slots;
    std::vector<std::uint32_t> m_freeIndices;
};

enum InputButton : std::uint16_t {
    FirePrimary = 1u << 0,
    FireSecondary = 1u << 1
};

constexpr std::uint16_t ValidInputButtons = FirePrimary | FireSecondary;

struct InputCommand {
    std::uint64_t sequence = 0;
    std::uint64_t tick = 0;
    std::int16_t moveX = 0;
    std::int16_t moveY = 0;
    std::int16_t moveZ = 0;
    std::int16_t aimX = 0;
    std::int16_t aimY = 0;
    std::int16_t roll = 0;
    std::uint16_t buttons = 0;
    EntityId target;

    bool operator==(const InputCommand& other) const;
};

enum class DecodeError {
    None,
    InvalidLength,
    InvalidVersion,
    InvalidMessageType,
    InvalidValue,
    InvalidCount
};

enum class InputCommandResult {
    Queued,
    Accepted,
    Duplicate,
    OutOfOrder,
    Invalid,
    TooLate,
    TooFarAhead,
    QueueFull,
    UnknownActor
};

struct InputCommandAcknowledgement {
    std::uint64_t sequence = 0;
    InputCommandResult result = InputCommandResult::Invalid;

    bool operator==(const InputCommandAcknowledgement& other) const;
};

class InputCommandSequencer {
public:
    InputCommandResult accept(const InputCommand& command);
    std::uint64_t lastAcceptedSequence() const;

private:
    std::uint64_t m_lastAcceptedSequence = 0;
};

class InputCommandBuffer {
public:
    explicit InputCommandBuffer(
        std::size_t capacity = 64,
        std::uint64_t maxFutureTicks = 120
    );

    InputCommandResult enqueue(const InputCommand& command, std::uint64_t currentTick);
    std::optional<InputCommand> advanceToTick(std::uint64_t tick);
    std::size_t pendingCount() const;

private:
    const std::size_t m_capacity;
    const std::uint64_t m_maxFutureTicks;
    InputCommandSequencer m_sequencer;
    std::map<std::uint64_t, InputCommand> m_pending;
    std::optional<InputCommand> m_currentCommand;
    std::uint64_t m_lastAcceptedTick = 0;
    bool m_hasAcceptedTick = false;
};

using EntityIdPacket = std::array<std::uint8_t, EntityIdPacketSize>;
using InputCommandPacket = std::array<std::uint8_t, InputCommandPacketSize>;
using InputCommandAcknowledgementPacket =
    std::array<std::uint8_t, InputCommandAcknowledgementPacketSize>;
using SnapshotResyncRequestPacket =
    std::array<std::uint8_t, SnapshotResyncRequestPacketSize>;
using VoxelRemovalPacket = std::array<std::uint8_t, VoxelRemovalPacketSize>;
using VoxelDamagePacket = std::array<std::uint8_t, VoxelDamagePacketSize>;
using WeaponFiredEventPacket =
    std::array<std::uint8_t, WeaponFiredEventPacketSize>;
using ShieldHitEventPacket =
    std::array<std::uint8_t, ShieldHitEventPacketSize>;
using MissionStateEventPacket =
    std::array<std::uint8_t, MissionStateEventPacketSize>;

bool encodeEntityId(const EntityId& id, EntityIdPacket& packet);
DecodeError decodeEntityId(const std::uint8_t* data, std::size_t size, EntityId& id);

bool encodeInputCommand(const InputCommand& command, InputCommandPacket& packet);
DecodeError decodeInputCommand(
    const std::uint8_t* data,
    std::size_t size,
    InputCommand& command
);
bool encodeInputCommandAcknowledgement(
    const InputCommandAcknowledgement& acknowledgement,
    InputCommandAcknowledgementPacket& packet
);
DecodeError decodeInputCommandAcknowledgement(
    const std::uint8_t* data,
    std::size_t size,
    InputCommandAcknowledgement& acknowledgement
);
bool encodeSnapshotResyncRequest(SnapshotResyncRequestPacket& packet);
DecodeError decodeSnapshotResyncRequest(const std::uint8_t* data, std::size_t size);
bool encodeVoxelRemovalEvent(
    const VoxelRemovalEvent& event,
    VoxelRemovalPacket& packet
);
DecodeError decodeVoxelRemovalEvent(
    const std::uint8_t* data,
    std::size_t size,
    VoxelRemovalEvent& event
);
bool encodeVoxelDamageEvent(const VoxelDamageEvent& event, VoxelDamagePacket& packet);
DecodeError decodeVoxelDamageEvent(
    const std::uint8_t* data,
    std::size_t size,
    VoxelDamageEvent& event
);
bool encodeShieldHitEvent(
    const ShieldHitEvent& event,
    ShieldHitEventPacket& packet
);
DecodeError decodeShieldHitEvent(
    const std::uint8_t* data,
    std::size_t size,
    ShieldHitEvent& event
);
bool encodeMissionStateEvent(
    const MissionStateEvent& event,
    MissionStateEventPacket& packet
);
DecodeError decodeMissionStateEvent(
    const std::uint8_t* data,
    std::size_t size,
    MissionStateEvent& event
);
bool encodeEntityContent(
    const EntityContentSnapshot& content,
    std::vector<std::uint8_t>& packet
);
DecodeError decodeEntityContent(
    const std::uint8_t* data,
    std::size_t size,
    EntityContentSnapshot& content
);
bool encodeEntityEquipmentSnapshot(
    const EntityEquipmentSnapshot& equipment,
    std::vector<std::uint8_t>& packet
);
DecodeError decodeEntityEquipmentSnapshot(
    const std::uint8_t* data,
    std::size_t size,
    EntityEquipmentSnapshot& equipment
);
bool encodeWeaponFiredEvent(
    const WeaponFiredEvent& event,
    WeaponFiredEventPacket& packet
);
DecodeError decodeWeaponFiredEvent(
    const std::uint8_t* data,
    std::size_t size,
    WeaponFiredEvent& event
);

bool buildSnapshotDelta(
    std::uint64_t sequence,
    std::uint64_t tick,
    std::uint64_t baselineSequence,
    const std::vector<EntitySnapshot>& current,
    const std::vector<EntitySnapshot>& baseline,
    SnapshotFrame& frame
);
bool applySnapshotFrame(
    const SnapshotFrame& frame,
    std::uint64_t expectedBaselineSequence,
    std::vector<EntitySnapshot>& replicaState
);
bool encodeSnapshotFrame(const SnapshotFrame& frame, std::vector<std::uint8_t>& packet);
DecodeError decodeSnapshotFrame(
    const std::uint8_t* data,
    std::size_t size,
    SnapshotFrame& frame
);

} // namespace protocol
} // namespace voxellancer
