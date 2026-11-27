#include "serverprotocol.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace voxellancer {
namespace protocol {
namespace {

constexpr std::uint8_t EntityIdMessageType = 1;
constexpr std::uint8_t InputCommandMessageType = 2;
constexpr std::uint8_t SnapshotFrameMessageType = 3;
constexpr std::uint8_t InputCommandAcknowledgementMessageType = 4;
constexpr std::uint8_t SnapshotResyncRequestMessageType = 5;
constexpr std::uint8_t VoxelRemovalMessageType = 6;
constexpr std::uint8_t EntityContentMessageType = 7;
constexpr std::uint8_t VoxelDamageMessageType = 8;
constexpr std::uint8_t EntityEquipmentSnapshotMessageType = 9;
constexpr std::uint8_t WeaponFiredEventMessageType = 10;
constexpr std::uint8_t ShieldHitEventMessageType = 11;
constexpr std::uint8_t MissionStateEventMessageType = 12;
constexpr std::uint8_t LobbyStatusMessageType = 13;
constexpr std::uint8_t ProjectileImpactEventMessageType = 16;

void writeU16(std::uint8_t* destination, std::uint16_t value) {
    destination[0] = static_cast<std::uint8_t>(value);
    destination[1] = static_cast<std::uint8_t>(value >> 8);
}

void writeU32(std::uint8_t* destination, std::uint32_t value) {
    for (unsigned int byte = 0; byte < 4; ++byte) {
        destination[byte] = static_cast<std::uint8_t>(value >> (byte * 8));
    }
}

void writeU64(std::uint8_t* destination, std::uint64_t value) {
    for (unsigned int byte = 0; byte < 8; ++byte) {
        destination[byte] = static_cast<std::uint8_t>(value >> (byte * 8));
    }
}

std::uint16_t readU16(const std::uint8_t* source) {
    return static_cast<std::uint16_t>(source[0]) |
        static_cast<std::uint16_t>(static_cast<std::uint16_t>(source[1]) << 8);
}

std::uint32_t readU32(const std::uint8_t* source) {
    std::uint32_t value = 0;
    for (unsigned int byte = 0; byte < 4; ++byte) {
        value |= static_cast<std::uint32_t>(source[byte]) << (byte * 8);
    }
    return value;
}

std::uint64_t readU64(const std::uint8_t* source) {
    std::uint64_t value = 0;
    for (unsigned int byte = 0; byte < 8; ++byte) {
        value |= static_cast<std::uint64_t>(source[byte]) << (byte * 8);
    }
    return value;
}

void writeFloat(std::uint8_t* destination, float value) {
    std::uint32_t encoded = 0;
    static_assert(sizeof(encoded) == sizeof(value), "Snapshot floats must be 32-bit.");
    std::memcpy(&encoded, &value, sizeof(encoded));
    writeU32(destination, encoded);
}

float readFloat(const std::uint8_t* source) {
    const std::uint32_t encoded = readU32(source);
    float value = 0.0f;
    std::memcpy(&value, &encoded, sizeof(value));
    return value;
}

std::uint64_t entityIdKey(EntityId id) {
    return (static_cast<std::uint64_t>(id.generation) << 32) | id.index;
}

bool validSnapshot(const EntitySnapshot& snapshot) {
    const auto finiteVector = [](const glm::vec3& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) &&
            std::isfinite(value.z);
    };
    const glm::quat& orientation = snapshot.orientation;
    const float orientationLengthSquared = glm::dot(orientation, orientation);
    const auto kind = static_cast<std::uint8_t>(snapshot.kind);
    return kind <= static_cast<std::uint8_t>(EntitySnapshot::Kind::Other) &&
        snapshot.id.valid() &&
        finiteVector(snapshot.position) &&
        finiteVector(snapshot.directionalVelocity) &&
        finiteVector(snapshot.angularVelocity) &&
        std::isfinite(orientation.w) &&
        std::isfinite(orientation.x) &&
        std::isfinite(orientation.y) &&
        std::isfinite(orientation.z) &&
        std::isfinite(orientationLengthSquared) &&
        orientationLengthSquared > 1e-8f &&
        std::isfinite(snapshot.shieldFraction) &&
        snapshot.shieldFraction >= 0.0f &&
        snapshot.shieldFraction <= 1.0f &&
        std::isfinite(snapshot.hullFraction) &&
        snapshot.hullFraction >= 0.0f &&
        snapshot.hullFraction <= 1.0f &&
        std::isfinite(snapshot.scale) &&
        snapshot.scale > 0.0f &&
        std::isfinite(snapshot.maxMass) &&
        snapshot.maxMass >= 0.0f &&
        finiteVector(snapshot.engineState) &&
        glm::all(glm::lessThanEqual(glm::abs(snapshot.engineState), glm::vec3(1.0f)));
}

std::int16_t readI16(const std::uint8_t* source) {
    const std::uint16_t encoded = readU16(source);
    const std::int32_t value = encoded <=
            static_cast<std::uint16_t>(std::numeric_limits<std::int16_t>::max())
        ? encoded
        : static_cast<std::int32_t>(encoded) - 0x10000;
    return static_cast<std::int16_t>(value);
}

std::int32_t readI32(const std::uint8_t* source) {
    const std::uint32_t encoded = readU32(source);
    const std::int64_t value = encoded <=
            static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max())
        ? encoded
        : static_cast<std::int64_t>(encoded) - 0x100000000LL;
    return static_cast<std::int32_t>(value);
}

void writeI16(std::uint8_t* destination, std::int16_t value) {
    writeU16(destination, static_cast<std::uint16_t>(value));
}

void writeCompressedQuaternion(std::uint8_t* destination, const glm::quat& value) {
    const glm::quat normalized = glm::normalize(value);
    float components[] = {
        normalized.w, normalized.x, normalized.y, normalized.z
    };
    std::uint8_t omitted = 0;
    for (std::uint8_t index = 1; index < 4; ++index) {
        if (std::abs(components[index]) > std::abs(components[omitted])) {
            omitted = index;
        }
    }
    if (components[omitted] < 0.0f) {
        for (float& component : components) {
            component = -component;
        }
    }
    destination[0] = omitted;
    constexpr float ComponentLimit = 0.7071067811865475f;
    std::size_t offset = 1;
    for (std::uint8_t index = 0; index < 4; ++index) {
        if (index == omitted) {
            continue;
        }
        const float normalizedComponent = glm::clamp(
            components[index] / ComponentLimit, -1.0f, 1.0f);
        const auto quantized = static_cast<std::int16_t>(
            std::lround(normalizedComponent * 32767.0f));
        writeI16(destination + offset, quantized);
        offset += sizeof(std::int16_t);
    }
}

bool readCompressedQuaternion(const std::uint8_t* source, glm::quat& value) {
    if ((source[0] & 0xfc) != 0) {
        return false;
    }
    const std::uint8_t omitted = source[0];
    constexpr float ComponentLimit = 0.7071067811865475f;
    float components[4]{};
    float sumSquared = 0.0f;
    std::size_t offset = 1;
    for (std::uint8_t index = 0; index < 4; ++index) {
        if (index == omitted) {
            continue;
        }
        const float component =
            static_cast<float>(readI16(source + offset)) / 32767.0f *
            ComponentLimit;
        offset += sizeof(std::int16_t);
        components[index] = component;
        sumSquared += component * component;
    }
    if (!std::isfinite(sumSquared) || sumSquared > 1.0001f) {
        return false;
    }
    components[omitted] = std::sqrt(std::max(0.0f, 1.0f - sumSquared));
    value = glm::quat(
        components[0], components[1], components[2], components[3]);
    return true;
}

bool validInputCommand(const InputCommand& command) {
    const bool targetGenerationValid =
        command.target.generation != 0 || command.target.index == 0;
    const bool targetRequired = (command.buttons & (FireSecondary | FireWithLead)) != 0;
    const bool leadModeValid = (command.buttons & FireWithLead) == 0 ||
        (command.buttons & FirePrimary) != 0;
    return command.sequence != 0 &&
        std::isfinite(command.firePoint.x) &&
        std::isfinite(command.firePoint.y) &&
        std::isfinite(command.firePoint.z) &&
        glm::dot(command.firePoint, command.firePoint) > 1e-8f &&
        glm::dot(command.firePoint, command.firePoint) <=
            MaxFirePointDistance * MaxFirePointDistance &&
        (command.buttons & ~ValidInputButtons) == 0 &&
        targetGenerationValid &&
        (!targetRequired || command.target.valid()) && leadModeValid;
}

DecodeError validateHeader(
    const std::uint8_t* data,
    std::size_t size,
    std::size_t expectedSize,
    std::uint8_t expectedType
) {
    if (data == nullptr || size != expectedSize) {
        return DecodeError::InvalidLength;
    }
    if (data[0] != ProtocolVersion) {
        return DecodeError::InvalidVersion;
    }
    if (data[1] != expectedType) {
        return DecodeError::InvalidMessageType;
    }
    return DecodeError::None;
}

bool validateEquipmentStates(
    const std::vector<EntityEquipmentState>& equipment,
    std::size_t& encodedSize
) {
    if (equipment.size() > MaxEntityEquipmentStates) {
        return false;
    }

    std::set<std::tuple<std::uint8_t, std::uint16_t,
        std::int32_t, std::int32_t, std::int32_t>> equipmentSlots;
    for (const EntityEquipmentState& state : equipment) {
        const auto kind = static_cast<std::uint8_t>(state.kind);
        if (kind < static_cast<std::uint8_t>(EquipmentSlotKind::Engine) ||
            kind > static_cast<std::uint8_t>(EquipmentSlotKind::Shield) ||
            state.equipmentKey.size() > MaxEquipmentKeyBytes ||
            !equipmentSlots.emplace(
                kind,
                state.group,
                state.cell.x,
                state.cell.y,
                state.cell.z
            ).second) {
            return false;
        }
        encodedSize += EntityEquipmentStateHeaderSize + state.equipmentKey.size();
    }
    return true;
}

void writeEquipmentStates(
    std::vector<std::uint8_t>& encoded,
    std::size_t& offset,
    const std::vector<EntityEquipmentState>& equipment
) {
    for (const EntityEquipmentState& state : equipment) {
        encoded[offset] = static_cast<std::uint8_t>(state.kind);
        writeU16(encoded.data() + offset + 1, state.group);
        writeU32(encoded.data() + offset + 3,
            static_cast<std::uint32_t>(state.cell.x));
        writeU32(encoded.data() + offset + 7,
            static_cast<std::uint32_t>(state.cell.y));
        writeU32(encoded.data() + offset + 11,
            static_cast<std::uint32_t>(state.cell.z));
        encoded[offset + 15] =
            static_cast<std::uint8_t>(state.equipmentKey.size());
        offset += EntityEquipmentStateHeaderSize;
        std::copy(
            state.equipmentKey.begin(),
            state.equipmentKey.end(),
            encoded.begin() + static_cast<std::ptrdiff_t>(offset));
        offset += state.equipmentKey.size();
    }
}

DecodeError readEquipmentStates(
    const std::uint8_t* data,
    std::size_t size,
    std::uint16_t count,
    std::size_t& offset,
    std::vector<EntityEquipmentState>& equipment
) {
    std::set<std::tuple<std::uint8_t, std::uint16_t,
        std::int32_t, std::int32_t, std::int32_t>> equipmentSlots;
    equipment.reserve(count);
    for (std::uint16_t index = 0; index < count; ++index) {
        if (size - offset < EntityEquipmentStateHeaderSize) {
            return DecodeError::InvalidLength;
        }
        EntityEquipmentState state;
        const std::uint8_t kind = data[offset];
        state.kind = static_cast<EquipmentSlotKind>(kind);
        state.group = readU16(data + offset + 1);
        state.cell = glm::ivec3(
            readI32(data + offset + 3),
            readI32(data + offset + 7),
            readI32(data + offset + 11));
        const std::size_t keyLength = data[offset + 15];
        offset += EntityEquipmentStateHeaderSize;
        if (kind < static_cast<std::uint8_t>(EquipmentSlotKind::Engine) ||
            kind > static_cast<std::uint8_t>(EquipmentSlotKind::Shield) ||
            keyLength > MaxEquipmentKeyBytes) {
            return DecodeError::InvalidValue;
        }
        if (size - offset < keyLength) {
            return DecodeError::InvalidLength;
        }
        if (keyLength != 0) {
            state.equipmentKey.assign(
                reinterpret_cast<const char*>(data + offset), keyLength);
        }
        offset += keyLength;
        if (!equipmentSlots.emplace(
                kind,
                state.group,
                state.cell.x,
                state.cell.y,
                state.cell.z
            ).second) {
            return DecodeError::InvalidValue;
        }
        equipment.push_back(std::move(state));
    }
    return DecodeError::None;
}

} // namespace

bool EntityId::valid() const {
    return generation != 0;
}

bool EntityId::operator==(const EntityId& other) const {
    return index == other.index && generation == other.generation;
}

bool EntityId::operator!=(const EntityId& other) const {
    return !(*this == other);
}

bool LobbyStatus::operator==(const LobbyStatus& other) const {
    return connectedPlayers == other.connectedPlayers &&
        targetPlayers == other.targetPlayers &&
        worldStarted == other.worldStarted;
}

bool EntitySnapshot::operator==(const EntitySnapshot& other) const {
    return id == other.id &&
        position == other.position &&
        orientation == other.orientation &&
        directionalVelocity == other.directionalVelocity &&
        angularVelocity == other.angularVelocity &&
        kind == other.kind &&
        shieldFraction == other.shieldFraction &&
        hullFraction == other.hullFraction &&
        scale == other.scale &&
        maxMass == other.maxMass &&
        engineState == other.engineState &&
        hostile == other.hostile;
}

bool VoxelRemovalEvent::operator==(const VoxelRemovalEvent& other) const {
    return entity == other.entity && cell == other.cell;
}

bool VoxelDamageEvent::operator==(const VoxelDamageEvent& other) const {
    return entity == other.entity && cell == other.cell && hp == other.hp;
}

bool ShieldHitEvent::operator==(const ShieldHitEvent& other) const {
    return entity == other.entity &&
        tick == other.tick &&
        cell == other.cell &&
        localImpactPosition.x == other.localImpactPosition.x &&
        localImpactPosition.y == other.localImpactPosition.y &&
        localImpactPosition.z == other.localImpactPosition.z &&
        impactColor == other.impactColor &&
        absorbedDamage == other.absorbedDamage;
}

bool MissionStateEvent::operator==(const MissionStateEvent& other) const {
    return tick == other.tick &&
        state == other.state &&
        missionKey == other.missionKey;
}

bool EntityVoxelState::operator==(const EntityVoxelState& other) const {
    return cell == other.cell &&
        color == other.color &&
        density == other.density &&
        hp == other.hp &&
        maxHP == other.maxHP;
}

bool EntityEquipmentState::operator==(
    const EntityEquipmentState& other
) const {
    return kind == other.kind &&
        group == other.group &&
        cell == other.cell &&
        equipmentKey == other.equipmentKey;
}

bool WeaponFiredEvent::operator==(const WeaponFiredEvent& other) const {
    return entity == other.entity &&
        tick == other.tick &&
        group == other.group &&
        cell == other.cell;
}

std::optional<EntityId> EntityIdAllocator::create() {
    if (!m_freeIndices.empty()) {
        const std::uint32_t index = m_freeIndices.back();
        m_freeIndices.pop_back();
        Slot& slot = m_slots[index];
        slot.alive = true;
        return EntityId{index, slot.generation};
    }

    if (m_slots.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }

    const auto index = static_cast<std::uint32_t>(m_slots.size());
    m_slots.push_back(Slot{});
    m_slots.back().alive = true;
    return EntityId{index, m_slots.back().generation};
}

bool EntityIdAllocator::release(EntityId id) {
    if (!isAlive(id)) {
        return false;
    }

    Slot& slot = m_slots[id.index];
    slot.alive = false;
    // At the maximum generation, leave the slot off the free list forever.
    if (slot.generation != std::numeric_limits<std::uint32_t>::max()) {
        ++slot.generation;
        m_freeIndices.push_back(id.index);
    }
    return true;
}

bool EntityIdAllocator::isAlive(EntityId id) const {
    return id.valid() && id.index < m_slots.size() &&
        m_slots[id.index].alive && m_slots[id.index].generation == id.generation;
}

bool InputCommand::operator==(const InputCommand& other) const {
    return sequence == other.sequence &&
        tick == other.tick &&
        moveX == other.moveX &&
        moveY == other.moveY &&
        moveZ == other.moveZ &&
        aimX == other.aimX &&
        aimY == other.aimY &&
        roll == other.roll &&
        buttons == other.buttons &&
        target == other.target &&
        firePoint == other.firePoint;
}

bool InputCommandAcknowledgement::operator==(
    const InputCommandAcknowledgement& other
) const {
    return sequence == other.sequence && result == other.result;
}

InputCommandResult InputCommandSequencer::accept(const InputCommand& command) {
    if (!validInputCommand(command)) {
        return InputCommandResult::Invalid;
    }
    if (command.sequence == m_lastAcceptedSequence) {
        return InputCommandResult::Duplicate;
    }
    if (command.sequence < m_lastAcceptedSequence) {
        return InputCommandResult::OutOfOrder;
    }

    m_lastAcceptedSequence = command.sequence;
    return InputCommandResult::Accepted;
}

std::uint64_t InputCommandSequencer::lastAcceptedSequence() const {
    return m_lastAcceptedSequence;
}

InputCommandBuffer::InputCommandBuffer(
    std::size_t capacity,
    std::uint64_t maxFutureTicks
) :
    m_capacity(capacity),
    m_maxFutureTicks(maxFutureTicks)
{
    if (m_capacity == 0) {
        throw std::invalid_argument("Input command buffer capacity must be positive.");
    }
}

InputCommandResult InputCommandBuffer::enqueue(
    const InputCommand& command,
    std::uint64_t currentTick
) {
    if (!validInputCommand(command)) {
        return InputCommandResult::Invalid;
    }
    if (command.tick < currentTick) {
        return InputCommandResult::TooLate;
    }
    if (command.tick - currentTick > m_maxFutureTicks) {
        return InputCommandResult::TooFarAhead;
    }
    if (m_hasAcceptedTick && command.tick < m_lastAcceptedTick) {
        return InputCommandResult::OutOfOrder;
    }
    if (m_pending.size() >= m_capacity && m_pending.find(command.tick) == m_pending.end()) {
        return InputCommandResult::QueueFull;
    }

    const InputCommandResult result = m_sequencer.accept(command);
    if (result != InputCommandResult::Accepted) {
        return result;
    }
    m_pending[command.tick] = command;
    m_lastAcceptedTick = command.tick;
    m_hasAcceptedTick = true;
    return InputCommandResult::Accepted;
}

std::optional<InputCommand> InputCommandBuffer::advanceToTick(std::uint64_t tick) {
    while (!m_pending.empty() && m_pending.begin()->first <= tick) {
        m_currentCommand = m_pending.begin()->second;
        m_pending.erase(m_pending.begin());
    }
    return m_currentCommand;
}

std::size_t InputCommandBuffer::pendingCount() const {
    return m_pending.size();
}

bool encodeEntityId(const EntityId& id, EntityIdPacket& packet) {
    if (!id.valid()) {
        return false;
    }

    EntityIdPacket encoded{};
    encoded[0] = ProtocolVersion;
    encoded[1] = EntityIdMessageType;
    writeU32(encoded.data() + 2, id.index);
    writeU32(encoded.data() + 6, id.generation);
    packet = encoded;
    return true;
}

DecodeError decodeEntityId(
    const std::uint8_t* data,
    std::size_t size,
    EntityId& id
) {
    const DecodeError headerError =
        validateHeader(data, size, EntityIdPacketSize, EntityIdMessageType);
    if (headerError != DecodeError::None) {
        return headerError;
    }

    const EntityId decoded{readU32(data + 2), readU32(data + 6)};
    if (!decoded.valid()) {
        return DecodeError::InvalidValue;
    }
    id = decoded;
    return DecodeError::None;
}

bool encodeContractAction(const ContractAction& action, ContractActionPacket& packet) {
    const auto kind = static_cast<std::uint8_t>(action.kind);
    const auto choice = static_cast<std::uint8_t>(action.choice);
    if (action.sequence == 0 || kind < 1 || kind > 11 ||
        action.weapon > ContractWeaponKinds ||
        ((kind == 6 || kind == 11) ? action.weapon == 0 : action.weapon != 0) ||
        glm::any(glm::lessThan(action.cell, glm::ivec3(0))) ||
        glm::any(glm::greaterThan(action.cell, glm::ivec3(255))) ||
        (kind != 6 && action.cell != glm::ivec3(0)) ||
        (kind == 7 ? action.node >= MaxRouteNodes : action.node != NoRouteNode) ||
        (kind == 8 ? choice < 1 || choice > 3 : choice != 0) ||
        (kind == 3 ? action.shield > ContractShieldKinds : action.shield != 0) ||
        (action.shieldShape != ShieldType::None && action.shieldShape != ShieldType::Hull &&
         action.shieldShape != ShieldType::Sphere) ||
        (kind != 1 && kind != 9 && action.shieldShape != ShieldType::None)) {
        return false;
    }
    packet[0] = ProtocolVersion;
    packet[1] = 14;
    writeU64(packet.data() + 2, action.sequence);
    writeU32(packet.data() + 10, action.mission);
    packet[14] = kind;
    packet[15] = action.weapon;
    packet[16] = static_cast<std::uint8_t>(action.cell.x);
    packet[17] = static_cast<std::uint8_t>(action.cell.y);
    packet[18] = static_cast<std::uint8_t>(action.cell.z);
    packet[19] = action.node;
    packet[20] = choice;
    packet[21] = action.shield;
    packet[22] = static_cast<std::uint8_t>(action.shieldShape);
    return true;
}

DecodeError decodeContractAction(
    const std::uint8_t* data, std::size_t size, ContractAction& action
) {
    const auto error = validateHeader(data, size, ContractActionPacketSize, 14);
    if (error != DecodeError::None) {
        return error;
    }
    ContractAction decoded{readU64(data + 2), readU32(data + 10),
        static_cast<ContractActionKind>(data[14])};
    decoded.weapon = data[15];
    decoded.cell = glm::ivec3(data[16], data[17], data[18]);
    decoded.node = data[19];
    decoded.choice = static_cast<RewardChoice>(data[20]);
    decoded.shield = data[21];
    decoded.shieldShape = static_cast<ShieldType>(data[22]);
    ContractActionPacket check{};
    if (!encodeContractAction(decoded, check)) {
        return DecodeError::InvalidValue;
    }
    action = decoded;
    return DecodeError::None;
}

bool encodeContractStatus(const ContractStatus& status, ContractStatusPacket& packet) {
    const auto phase = static_cast<std::uint8_t>(status.phase);
    for (float value : {status.shieldHP,status.shieldCapacity,status.shieldRegeneration,
            status.shieldRechargeRemaining,status.weaponEnergy,status.weaponEnergyCapacity,
            status.weaponEnergyRegeneration,status.weaponEnergyRechargeRemaining})
        if (!std::isfinite(value) || value < 0) return false;
    if (status.shieldBuild > ContractShieldKinds || status.shieldHP > status.shieldCapacity ||
        status.weaponEnergy > status.weaponEnergyCapacity) return false;
    if (phase < 1 || phase > 6 || status.players == 0 ||
        status.players > 16 || status.readyPlayers > status.players ||
        static_cast<std::uint8_t>(status.result) >
            static_cast<std::uint8_t>(ContractResult::CheckpointFailed) ||
        status.offer == 0 || status.offer > ContractWeaponKinds ||
        status.mounts.size() > MaxContractMounts ||
        status.gunPrice == 0 || status.shieldPrice == 0 || status.run == 0 ||
        status.livingPlayers > status.players || status.pendingRewards > status.livingPlayers ||
        status.damageUpgrades > MaxRunUpgradeStacks || status.shieldUpgrades > MaxRunUpgradeStacks ||
        status.rewardWeapon > ContractWeaponKinds || status.rewardUpgrade > 2 ||
        (!status.viewActor.valid() && status.viewActor.index != 0) ||
        (status.phase == ContractPhase::Reward &&
            (status.rewardWeapon == 0 || status.rewardUpgrade == 0 || status.reward == 0))) {
        return false;
    }
    unsigned inventoryCount = 0;
    for (auto count : status.inventory) inventoryCount += count;
    if (inventoryCount > ContractInventoryCapacity) return false;
    if (status.route.size() > MaxRouteNodes) return false;
    const auto nodeMask = (1u << status.route.size()) - 1;
    if (status.visitedNodes & ~nodeMask) return false;
    if (status.route.empty()) {
        if (status.currentNode != NoRouteNode || status.selectedNode != NoRouteNode ||
            phase > 2) return false;
    } else {
        if (status.currentNode >= status.route.size() ||
            !(status.visitedNodes & (1u << status.currentNode))) return false;
        if (status.selectedNode != NoRouteNode &&
            (status.selectedNode >= status.route.size() ||
                !(status.route[status.currentNode].links & (1u << status.selectedNode)))) return false;
        for (std::size_t i = 0; i < status.route.size(); ++i) {
            const auto& node = status.route[i];
            const auto kind = static_cast<std::uint8_t>(node.kind);
            if (kind < 1 || kind > 3 || node.row > 6 || node.lane > 2 ||
                (node.kind == RouteNodeKind::Base ? node.level != 0 :
                    node.level < 1 || node.level > (node.kind == RouteNodeKind::Attack ? FlagshipLevel : 3)) ||
                (node.links & ~nodeMask)) return false;
            for (std::size_t j = 0; j < status.route.size(); ++j)
                if ((node.links & (1u << j)) && status.route[j].row != node.row + 1)
                    return false;
        }
    }
    for (std::size_t i = 0; i < status.mounts.size(); ++i) {
        const auto& mount = status.mounts[i];
        if (mount.weapon > ContractWeaponKinds ||
            glm::any(glm::lessThan(mount.cell, glm::ivec3(0))) ||
            glm::any(glm::greaterThan(mount.cell, glm::ivec3(255)))) return false;
        for (std::size_t j = 0; j < i; ++j)
            if (status.mounts[j].cell == mount.cell) return false;
    }
    packet.fill(0);
    packet[0] = ProtocolVersion;
    packet[1] = 15;
    packet[2] = phase;
    writeU32(packet.data() + 3, status.mission);
    writeU32(packet.data() + 7, status.credits);
    writeU16(packet.data() + 11, status.remaining);
    packet[13] = status.gun;
    packet[14] = status.shield;
    packet[15] = status.ready;
    packet[16] = status.readyPlayers;
    packet[17] = status.players;
    packet[18] = static_cast<std::uint8_t>(status.result);
    writeU64(packet.data() + 19, status.acknowledgedSequence);
    writeU32(packet.data() + 27, status.gunPrice);
    writeU32(packet.data() + 31, status.shieldPrice);
    writeU32(packet.data() + 35, status.reward);
    packet[39] = status.invulnerable;
    packet[40] = status.offer;
    for (std::size_t i = 0; i < status.inventory.size(); ++i)
        writeU16(packet.data() + 41 + i * 2, status.inventory[i]);
    packet[53] = static_cast<std::uint8_t>(status.mounts.size());
    for (std::size_t i = 0; i < status.mounts.size(); ++i) {
        const auto& mount = status.mounts[i];
        const auto offset = 55 + i * 4;
        packet[offset] = static_cast<std::uint8_t>(mount.cell.x);
        packet[offset + 1] = static_cast<std::uint8_t>(mount.cell.y);
        packet[offset + 2] = static_cast<std::uint8_t>(mount.cell.z);
        packet[offset + 3] = mount.weapon;
    }
    packet[ContractRouteOffset] = status.currentNode;
    packet[ContractRouteOffset + 1] = status.selectedNode;
    writeU32(packet.data() + ContractRouteOffset + 2, status.visitedNodes);
    packet[ContractRouteOffset + 6] = static_cast<std::uint8_t>(status.route.size());
    for (std::size_t i = 0; i < status.route.size(); ++i) {
        const auto offset = ContractRouteOffset + 7 + i * 8;
        const auto& node = status.route[i];
        packet[offset] = static_cast<std::uint8_t>(node.kind);
        packet[offset + 1] = node.level;
        packet[offset + 2] = node.row;
        packet[offset + 3] = node.lane;
        writeU32(packet.data() + offset + 4, node.links);
    }
    auto* run = packet.data() + ContractRunOffset;
    writeU32(run, status.run);
    writeU32(run + 4, status.viewActor.index);
    writeU32(run + 8, status.viewActor.generation);
    writeU64(run + 12, status.elapsedTicks);
    writeU32(run + 20, status.creditsEarned);
    writeU16(run + 24, status.attacksCleared);
    writeU16(run + 26, status.enemiesDefeated);
    writeU16(run + 28, status.basesVisited);
    writeU16(run + 30, status.weaponsFound);
    run[32] = status.damageUpgrades;
    run[33] = status.shieldUpgrades;
    run[34] = status.rewardWeapon;
    run[35] = status.rewardUpgrade;
    run[36] = status.alive;
    run[37] = status.rewardClaimed;
    run[38] = status.livingPlayers;
    run[39] = status.pendingRewards;
    auto* resources = packet.data() + ContractResourceOffset;
    resources[0] = status.shieldBuild;
    writeFloat(resources+1,status.shieldHP);
    writeFloat(resources+5,status.shieldCapacity);
    writeFloat(resources+9,status.shieldRegeneration);
    writeFloat(resources+13,status.shieldRechargeRemaining);
    writeFloat(resources+17,status.weaponEnergy);
    writeFloat(resources+21,status.weaponEnergyCapacity);
    writeFloat(resources+25,status.weaponEnergyRegeneration);
    writeFloat(resources+29,status.weaponEnergyRechargeRemaining);
    writeU32(resources+33,status.repairPrice);
    writeU32(resources+37,status.missingHullBlocks);
    return true;
}

DecodeError decodeContractStatus(
    const std::uint8_t* data, std::size_t size, ContractStatus& status
) {
    const auto error = validateHeader(data, size, ContractStatusPacketSize, 15);
    if (error != DecodeError::None) {
        return error;
    }
    ContractStatus decoded;
    decoded.phase = static_cast<ContractPhase>(data[2]);
    decoded.mission = readU32(data + 3);
    decoded.credits = readU32(data + 7);
    decoded.remaining = readU16(data + 11);
    decoded.gun = data[13] != 0;
    decoded.shield = data[14] != 0;
    decoded.ready = data[15] != 0;
    decoded.readyPlayers = data[16];
    decoded.players = data[17];
    decoded.result = static_cast<ContractResult>(data[18]);
    decoded.acknowledgedSequence = readU64(data + 19);
    decoded.gunPrice = readU32(data + 27);
    decoded.shieldPrice = readU32(data + 31);
    decoded.reward = readU32(data + 35);
    decoded.invulnerable = data[39] != 0;
    decoded.offer = data[40];
    for (std::size_t i = 0; i < decoded.inventory.size(); ++i)
        decoded.inventory[i] = readU16(data + 41 + i * 2);
    if (data[53] > MaxContractMounts || data[54] != 0) return DecodeError::InvalidValue;
    for (std::size_t i = 0; i < data[53]; ++i) {
        const auto offset = 55 + i * 4;
        decoded.mounts.push_back({glm::ivec3(data[offset], data[offset + 1],
            data[offset + 2]), data[offset + 3]});
    }
    for (std::size_t i = 55 + data[53] * 4; i < ContractRouteOffset; ++i)
        if (data[i] != 0) return DecodeError::InvalidValue;
    decoded.currentNode = data[ContractRouteOffset];
    decoded.selectedNode = data[ContractRouteOffset + 1];
    decoded.visitedNodes = readU32(data + ContractRouteOffset + 2);
    const auto count = data[ContractRouteOffset + 6];
    if (count > MaxRouteNodes) return DecodeError::InvalidValue;
    for (std::size_t i = 0; i < count; ++i) {
        const auto offset = ContractRouteOffset + 7 + i * 8;
        decoded.route.push_back({static_cast<RouteNodeKind>(data[offset]), data[offset + 1],
            data[offset + 2], data[offset + 3], readU32(data + offset + 4)});
    }
    for (std::size_t i = ContractRouteOffset + 7 + count * 8; i < ContractRunOffset; ++i)
        if (data[i] != 0) return DecodeError::InvalidValue;
    const auto* run = data + ContractRunOffset;
    decoded.run = readU32(run);
    decoded.viewActor = {readU32(run + 4), readU32(run + 8)};
    decoded.elapsedTicks = readU64(run + 12);
    decoded.creditsEarned = readU32(run + 20);
    decoded.attacksCleared = readU16(run + 24);
    decoded.enemiesDefeated = readU16(run + 26);
    decoded.basesVisited = readU16(run + 28);
    decoded.weaponsFound = readU16(run + 30);
    decoded.damageUpgrades = run[32];
    decoded.shieldUpgrades = run[33];
    decoded.rewardWeapon = run[34];
    decoded.rewardUpgrade = run[35];
    decoded.alive = run[36] != 0;
    decoded.rewardClaimed = run[37] != 0;
    decoded.livingPlayers = run[38];
    decoded.pendingRewards = run[39];
    const auto* resources = data + ContractResourceOffset;
    decoded.shieldBuild = resources[0];
    decoded.shieldHP = readFloat(resources+1);
    decoded.shieldCapacity = readFloat(resources+5);
    decoded.shieldRegeneration = readFloat(resources+9);
    decoded.shieldRechargeRemaining = readFloat(resources+13);
    decoded.weaponEnergy = readFloat(resources+17);
    decoded.weaponEnergyCapacity = readFloat(resources+21);
    decoded.weaponEnergyRegeneration = readFloat(resources+25);
    decoded.weaponEnergyRechargeRemaining = readFloat(resources+29);
    decoded.repairPrice = readU32(resources+33);
    decoded.missingHullBlocks = readU32(resources+37);
    ContractStatusPacket check{};
    if (data[13] > 1 || data[14] > 1 || data[15] > 1 || data[39] > 1 ||
        run[36] > 1 || run[37] > 1 ||
        !encodeContractStatus(decoded, check)) {
        return DecodeError::InvalidValue;
    }
    status = decoded;
    return DecodeError::None;
}

bool encodeLobbyStatus(const LobbyStatus& status, LobbyStatusPacket& packet) {
    if (status.targetPlayers == 0 ||
        status.connectedPlayers > status.targetPlayers) {
        return false;
    }
    packet[0] = ProtocolVersion;
    packet[1] = LobbyStatusMessageType;
    writeU16(packet.data() + 2, status.connectedPlayers);
    writeU16(packet.data() + 4, status.targetPlayers);
    packet[6] = status.worldStarted ? 1 : 0;
    return true;
}

DecodeError decodeLobbyStatus(
    const std::uint8_t* data,
    std::size_t size,
    LobbyStatus& status
) {
    const DecodeError headerError = validateHeader(
        data, size, LobbyStatusPacketSize, LobbyStatusMessageType);
    if (headerError != DecodeError::None) {
        return headerError;
    }
    const LobbyStatus decoded{
        readU16(data + 2),
        readU16(data + 4),
        data[6] != 0
    };
    if (decoded.targetPlayers == 0 ||
        decoded.connectedPlayers > decoded.targetPlayers ||
        data[6] > 1) {
        return DecodeError::InvalidValue;
    }
    status = decoded;
    return DecodeError::None;
}

bool encodeInputCommand(const InputCommand& command, InputCommandPacket& packet) {
    if (!validInputCommand(command)) {
        return false;
    }

    InputCommandPacket encoded{};
    encoded[0] = ProtocolVersion;
    encoded[1] = InputCommandMessageType;
    writeU64(encoded.data() + 2, command.sequence);
    writeU64(encoded.data() + 10, command.tick);
    writeI16(encoded.data() + 18, command.moveX);
    writeI16(encoded.data() + 20, command.moveY);
    writeI16(encoded.data() + 22, command.moveZ);
    writeI16(encoded.data() + 24, command.aimX);
    writeI16(encoded.data() + 26, command.aimY);
    writeI16(encoded.data() + 28, command.roll);
    writeU16(encoded.data() + 30, command.buttons);
    writeU32(encoded.data() + 32, command.target.index);
    writeU32(encoded.data() + 36, command.target.generation);
    writeFloat(encoded.data() + 40, command.firePoint.x);
    writeFloat(encoded.data() + 44, command.firePoint.y);
    writeFloat(encoded.data() + 48, command.firePoint.z);
    packet = encoded;
    return true;
}

DecodeError decodeInputCommand(
    const std::uint8_t* data,
    std::size_t size,
    InputCommand& command
) {
    const DecodeError headerError =
        validateHeader(data, size, InputCommandPacketSize, InputCommandMessageType);
    if (headerError != DecodeError::None) {
        return headerError;
    }

    InputCommand decoded;
    decoded.sequence = readU64(data + 2);
    decoded.tick = readU64(data + 10);
    decoded.moveX = readI16(data + 18);
    decoded.moveY = readI16(data + 20);
    decoded.moveZ = readI16(data + 22);
    decoded.aimX = readI16(data + 24);
    decoded.aimY = readI16(data + 26);
    decoded.roll = readI16(data + 28);
    decoded.buttons = readU16(data + 30);
    decoded.target = EntityId{readU32(data + 32), readU32(data + 36)};
    decoded.firePoint = glm::vec3(
        readFloat(data + 40), readFloat(data + 44), readFloat(data + 48));
    if (!validInputCommand(decoded)) {
        return DecodeError::InvalidValue;
    }

    command = decoded;
    return DecodeError::None;
}

bool encodeInputCommandAcknowledgement(
    const InputCommandAcknowledgement& acknowledgement,
    InputCommandAcknowledgementPacket& packet
) {
    const auto result = static_cast<std::uint8_t>(acknowledgement.result);
    if (acknowledgement.sequence == 0 ||
        result > static_cast<std::uint8_t>(InputCommandResult::UnknownActor)) {
        return false;
    }

    InputCommandAcknowledgementPacket encoded{};
    encoded[0] = ProtocolVersion;
    encoded[1] = InputCommandAcknowledgementMessageType;
    writeU64(encoded.data() + 2, acknowledgement.sequence);
    encoded[10] = result;
    packet = encoded;
    return true;
}

DecodeError decodeInputCommandAcknowledgement(
    const std::uint8_t* data,
    std::size_t size,
    InputCommandAcknowledgement& acknowledgement
) {
    const DecodeError headerError = validateHeader(
        data,
        size,
        InputCommandAcknowledgementPacketSize,
        InputCommandAcknowledgementMessageType
    );
    if (headerError != DecodeError::None) {
        return headerError;
    }

    const std::uint8_t result = data[10];
    if (readU64(data + 2) == 0 ||
        result > static_cast<std::uint8_t>(InputCommandResult::UnknownActor)) {
        return DecodeError::InvalidValue;
    }
    acknowledgement = InputCommandAcknowledgement{
        readU64(data + 2),
        static_cast<InputCommandResult>(result)
    };
    return DecodeError::None;
}

bool encodeSnapshotResyncRequest(SnapshotResyncRequestPacket& packet) {
    packet[0] = ProtocolVersion;
    packet[1] = SnapshotResyncRequestMessageType;
    return true;
}

DecodeError decodeSnapshotResyncRequest(const std::uint8_t* data, std::size_t size) {
    return validateHeader(
        data,
        size,
        SnapshotResyncRequestPacketSize,
        SnapshotResyncRequestMessageType
    );
}

bool encodeVoxelRemovalEvent(
    const VoxelRemovalEvent& event,
    VoxelRemovalPacket& packet
) {
    if (!event.entity.valid()) {
        return false;
    }

    packet[0] = ProtocolVersion;
    packet[1] = VoxelRemovalMessageType;
    writeU32(packet.data() + 2, event.entity.index);
    writeU32(packet.data() + 6, event.entity.generation);
    writeU32(packet.data() + 10, static_cast<std::uint32_t>(event.cell.x));
    writeU32(packet.data() + 14, static_cast<std::uint32_t>(event.cell.y));
    writeU32(packet.data() + 18, static_cast<std::uint32_t>(event.cell.z));
    return true;
}

DecodeError decodeVoxelRemovalEvent(
    const std::uint8_t* data,
    std::size_t size,
    VoxelRemovalEvent& event
) {
    const DecodeError headerError = validateHeader(
        data, size, VoxelRemovalPacketSize, VoxelRemovalMessageType);
    if (headerError != DecodeError::None) {
        return headerError;
    }

    VoxelRemovalEvent decoded;
    decoded.entity = EntityId{readU32(data + 2), readU32(data + 6)};
    decoded.cell = glm::ivec3(
        readI32(data + 10), readI32(data + 14), readI32(data + 18));
    if (!decoded.entity.valid()) {
        return DecodeError::InvalidValue;
    }
    event = decoded;
    return DecodeError::None;
}

bool encodeVoxelDamageEvent(
    const VoxelDamageEvent& event,
    VoxelDamagePacket& packet
) {
    if (!event.entity.valid() || !std::isfinite(event.hp) || event.hp < 0.0f) {
        return false;
    }

    packet[0] = ProtocolVersion;
    packet[1] = VoxelDamageMessageType;
    writeU32(packet.data() + 2, event.entity.index);
    writeU32(packet.data() + 6, event.entity.generation);
    writeU32(packet.data() + 10, static_cast<std::uint32_t>(event.cell.x));
    writeU32(packet.data() + 14, static_cast<std::uint32_t>(event.cell.y));
    writeU32(packet.data() + 18, static_cast<std::uint32_t>(event.cell.z));
    writeFloat(packet.data() + 22, event.hp);
    return true;
}

DecodeError decodeVoxelDamageEvent(
    const std::uint8_t* data,
    std::size_t size,
    VoxelDamageEvent& event
) {
    const DecodeError headerError = validateHeader(
        data, size, VoxelDamagePacketSize, VoxelDamageMessageType);
    if (headerError != DecodeError::None) {
        return headerError;
    }

    VoxelDamageEvent decoded;
    decoded.entity = EntityId{readU32(data + 2), readU32(data + 6)};
    decoded.cell = glm::ivec3(
        readI32(data + 10), readI32(data + 14), readI32(data + 18));
    decoded.hp = readFloat(data + 22);
    if (!decoded.entity.valid() || !std::isfinite(decoded.hp) || decoded.hp < 0.0f) {
        return DecodeError::InvalidValue;
    }
    event = decoded;
    return DecodeError::None;
}

bool ProjectileImpactEvent::operator==(const ProjectileImpactEvent& other) const {
    return tick == other.tick && position == other.position && normal == other.normal &&
        scale == other.scale && projectileKey == other.projectileKey;
}

bool encodeProjectileImpactEvent(const ProjectileImpactEvent& event, ProjectileImpactEventPacket& packet) {
    for (float value : {event.position.x,event.position.y,event.position.z,
        event.normal.x,event.normal.y,event.normal.z,event.scale})
        if (!std::isfinite(value)) return false;
    const float normalLength = glm::dot(event.normal,event.normal);
    if (normalLength < 0.99f || normalLength > 1.01f || event.scale <= 0 ||
        event.projectileKey.empty() || event.projectileKey.size() > MaxEquipmentKeyBytes ||
        event.projectileKey.find('\0') != std::string::npos) return false;
    packet.fill(0);
    packet[0] = ProtocolVersion; packet[1] = ProjectileImpactEventMessageType;
    writeU64(packet.data()+2,event.tick);
    for (int i = 0; i < 3; ++i) {
        writeFloat(packet.data()+10+i*4,event.position[i]);
        writeFloat(packet.data()+22+i*4,event.normal[i]);
    }
    writeFloat(packet.data()+34,event.scale);
    packet[38] = static_cast<std::uint8_t>(event.projectileKey.size());
    std::copy(event.projectileKey.begin(),event.projectileKey.end(),packet.begin()+39);
    return true;
}
DecodeError decodeProjectileImpactEvent(const std::uint8_t* data, std::size_t size,
    ProjectileImpactEvent& event) {
    const auto error = validateHeader(data,size,ProjectileImpactEventPacketSize,
        ProjectileImpactEventMessageType);
    if (error != DecodeError::None) return error;
    const auto length = data[38];
    if (length == 0 || length > MaxEquipmentKeyBytes) return DecodeError::InvalidValue;
    ProjectileImpactEvent decoded;
    decoded.tick = readU64(data+2);
    for (int i = 0; i < 3; ++i) {
        decoded.position[i] = readFloat(data+10+i*4);
        decoded.normal[i] = readFloat(data+22+i*4);
    }
    decoded.scale = readFloat(data+34);
    decoded.projectileKey.assign(reinterpret_cast<const char*>(data+39),length);
    ProjectileImpactEventPacket canonical{};
    if (!encodeProjectileImpactEvent(decoded,canonical) ||
        !std::equal(canonical.begin(),canonical.end(),data)) return DecodeError::InvalidValue;
    event = std::move(decoded);
    return DecodeError::None;
}

bool encodeShieldHitEvent(
    const ShieldHitEvent& event,
    ShieldHitEventPacket& packet
) {
    if (!event.entity.valid() ||
        !std::isfinite(event.absorbedDamage) ||
        !std::isfinite(event.localImpactPosition.x) ||
        !std::isfinite(event.localImpactPosition.y) ||
        !std::isfinite(event.localImpactPosition.z) ||
        event.absorbedDamage <= 0.0f) {
        return false;
    }

    packet[0] = ProtocolVersion;
    packet[1] = ShieldHitEventMessageType;
    writeU32(packet.data() + 2, event.entity.index);
    writeU32(packet.data() + 6, event.entity.generation);
    writeU64(packet.data() + 10, event.tick);
    writeU32(packet.data() + 18, static_cast<std::uint32_t>(event.cell.x));
    writeU32(packet.data() + 22, static_cast<std::uint32_t>(event.cell.y));
    writeU32(packet.data() + 26, static_cast<std::uint32_t>(event.cell.z));
    writeFloat(packet.data() + 30, event.localImpactPosition.x);
    writeFloat(packet.data() + 34, event.localImpactPosition.y);
    writeFloat(packet.data() + 38, event.localImpactPosition.z);
    writeU32(packet.data() + 42, event.impactColor);
    writeFloat(packet.data() + 46, event.absorbedDamage);
    return true;
}

DecodeError decodeShieldHitEvent(
    const std::uint8_t* data,
    std::size_t size,
    ShieldHitEvent& event
) {
    const DecodeError headerError = validateHeader(
        data,
        size,
        ShieldHitEventPacketSize,
        ShieldHitEventMessageType);
    if (headerError != DecodeError::None) {
        return headerError;
    }

    ShieldHitEvent decoded;
    decoded.entity = EntityId{readU32(data + 2), readU32(data + 6)};
    decoded.tick = readU64(data + 10);
    decoded.cell = glm::ivec3(
        readI32(data + 18),
        readI32(data + 22),
        readI32(data + 26));
    decoded.localImpactPosition = glm::vec3(
        readFloat(data + 30),
        readFloat(data + 34),
        readFloat(data + 38));
    decoded.impactColor = readU32(data + 42);
    decoded.absorbedDamage = readFloat(data + 46);
    if (!decoded.entity.valid() ||
        !std::isfinite(decoded.absorbedDamage) ||
        !std::isfinite(decoded.localImpactPosition.x) ||
        !std::isfinite(decoded.localImpactPosition.y) ||
        !std::isfinite(decoded.localImpactPosition.z) ||
        decoded.absorbedDamage <= 0.0f) {
        return DecodeError::InvalidValue;
    }
    event = decoded;
    return DecodeError::None;
}

bool encodeMissionStateEvent(
    const MissionStateEvent& event,
    MissionStateEventPacket& packet
) {
    const auto state = static_cast<std::uint8_t>(event.state);
    if (event.missionKey.empty() ||
        event.missionKey.size() > MaxMissionKeyBytes ||
        event.missionKey.find('\0') != std::string::npos ||
        state < static_cast<std::uint8_t>(MissionEventState::Started) ||
        state > static_cast<std::uint8_t>(MissionEventState::Succeeded)) {
        return false;
    }

    packet.fill(0);
    packet[0] = ProtocolVersion;
    packet[1] = MissionStateEventMessageType;
    writeU64(packet.data() + 2, event.tick);
    packet[10] = state;
    packet[11] = static_cast<std::uint8_t>(event.missionKey.size());
    std::copy(
        event.missionKey.begin(),
        event.missionKey.end(),
        packet.begin() + 12);
    return true;
}

DecodeError decodeMissionStateEvent(
    const std::uint8_t* data,
    std::size_t size,
    MissionStateEvent& event
) {
    const DecodeError headerError = validateHeader(
        data,
        size,
        MissionStateEventPacketSize,
        MissionStateEventMessageType);
    if (headerError != DecodeError::None) {
        return headerError;
    }

    const std::uint8_t state = data[10];
    const std::uint8_t keyLength = data[11];
    const std::uint8_t* keyBegin = data + 12;
    if (state < static_cast<std::uint8_t>(MissionEventState::Started) ||
        state > static_cast<std::uint8_t>(MissionEventState::Succeeded) ||
        keyLength == 0 ||
        keyLength > MaxMissionKeyBytes ||
        std::find(keyBegin, keyBegin + keyLength, 0) !=
            keyBegin + keyLength ||
        std::any_of(
            keyBegin + keyLength,
            data + MissionStateEventPacketSize,
            [](std::uint8_t value) { return value != 0; })) {
        return DecodeError::InvalidValue;
    }

    MissionStateEvent decoded;
    decoded.tick = readU64(data + 2);
    decoded.state = static_cast<MissionEventState>(state);
    decoded.missionKey.assign(
        reinterpret_cast<const char*>(keyBegin), keyLength);
    event = std::move(decoded);
    return DecodeError::None;
}

bool encodeWeaponFiredEvent(
    const WeaponFiredEvent& event,
    WeaponFiredEventPacket& packet
) {
    if (!event.entity.valid()) {
        return false;
    }

    packet[0] = ProtocolVersion;
    packet[1] = WeaponFiredEventMessageType;
    writeU32(packet.data() + 2, event.entity.index);
    writeU32(packet.data() + 6, event.entity.generation);
    writeU64(packet.data() + 10, event.tick);
    writeU16(packet.data() + 18, event.group);
    writeU32(packet.data() + 20, static_cast<std::uint32_t>(event.cell.x));
    writeU32(packet.data() + 24, static_cast<std::uint32_t>(event.cell.y));
    writeU32(packet.data() + 28, static_cast<std::uint32_t>(event.cell.z));
    return true;
}

DecodeError decodeWeaponFiredEvent(
    const std::uint8_t* data,
    std::size_t size,
    WeaponFiredEvent& event
) {
    const DecodeError headerError = validateHeader(
        data,
        size,
        WeaponFiredEventPacketSize,
        WeaponFiredEventMessageType);
    if (headerError != DecodeError::None) {
        return headerError;
    }

    WeaponFiredEvent decoded;
    decoded.entity = EntityId{readU32(data + 2), readU32(data + 6)};
    decoded.tick = readU64(data + 10);
    decoded.group = readU16(data + 18);
    decoded.cell = glm::ivec3(
        readI32(data + 20),
        readI32(data + 24),
        readI32(data + 28));
    if (!decoded.entity.valid()) {
        return DecodeError::InvalidValue;
    }
    event = decoded;
    return DecodeError::None;
}

bool encodeEntityContent(
    const EntityContentSnapshot& content,
    std::vector<std::uint8_t>& packet
) {
    if (!content.id.valid() ||
        content.voxels.size() > MaxEntityVoxels ||
        content.equipment.size() > MaxEntityEquipmentStates ||
        content.definitionKey.size() > MaxEntityDefinitionKeyBytes ||
        (content.shieldType != ShieldType::None && content.shieldType != ShieldType::Hull &&
         content.shieldType != ShieldType::Sphere)) {
        return false;
    }

    std::set<std::tuple<std::int32_t, std::int32_t, std::int32_t>> cells;
    for (const EntityVoxelState& voxel : content.voxels) {
        if (!std::isfinite(voxel.density) || voxel.density < 0.0f ||
            !std::isfinite(voxel.hp) || voxel.hp < 0.0f ||
            !std::isfinite(voxel.maxHP) || voxel.maxHP < 0.0f ||
            voxel.hp > voxel.maxHP ||
            !cells.emplace(voxel.cell.x, voxel.cell.y, voxel.cell.z).second) {
            return false;
        }
    }

    std::size_t packetSize =
        EntityContentHeaderSize + content.definitionKey.size() +
        content.voxels.size() * EntityVoxelStateSize;
    if (!validateEquipmentStates(content.equipment, packetSize)) {
        return false;
    }

    std::vector<std::uint8_t> encoded(packetSize, 0);
    encoded[0] = ProtocolVersion;
    encoded[1] = EntityContentMessageType;
    writeU32(encoded.data() + 2, content.id.index);
    writeU32(encoded.data() + 6, content.id.generation);
    writeU16(encoded.data() + 10, static_cast<std::uint16_t>(content.voxels.size()));
    writeU16(encoded.data() + 12, static_cast<std::uint16_t>(content.equipment.size()));
    encoded[14] = static_cast<std::uint8_t>(content.definitionKey.size());
    encoded[15] = static_cast<std::uint8_t>(content.shieldType);
    std::size_t offset = EntityContentHeaderSize;
    std::copy(
        content.definitionKey.begin(),
        content.definitionKey.end(),
        encoded.begin() + static_cast<std::ptrdiff_t>(offset));
    offset += content.definitionKey.size();
    for (const EntityVoxelState& voxel : content.voxels) {
        writeU32(encoded.data() + offset, static_cast<std::uint32_t>(voxel.cell.x));
        writeU32(encoded.data() + offset + 4, static_cast<std::uint32_t>(voxel.cell.y));
        writeU32(encoded.data() + offset + 8, static_cast<std::uint32_t>(voxel.cell.z));
        writeU32(encoded.data() + offset + 12, voxel.color);
        writeFloat(encoded.data() + offset + 16, voxel.density);
        writeFloat(encoded.data() + offset + 20, voxel.hp);
        writeFloat(encoded.data() + offset + 24, voxel.maxHP);
        offset += EntityVoxelStateSize;
    }
    writeEquipmentStates(encoded, offset, content.equipment);
    packet = std::move(encoded);
    return true;
}

DecodeError decodeEntityContent(
    const std::uint8_t* data,
    std::size_t size,
    EntityContentSnapshot& content
) {
    if (data == nullptr || size < EntityContentHeaderSize) {
        return DecodeError::InvalidLength;
    }
    if (data[0] != ProtocolVersion) {
        return DecodeError::InvalidVersion;
    }
    if (data[1] != EntityContentMessageType) {
        return DecodeError::InvalidMessageType;
    }

    const std::uint16_t voxelCount = readU16(data + 10);
    const std::uint16_t equipmentCount = readU16(data + 12);
    const std::size_t definitionKeyLength = data[14];
    if (voxelCount > MaxEntityVoxels ||
        equipmentCount > MaxEntityEquipmentStates) {
        return DecodeError::InvalidCount;
    }
    if (definitionKeyLength > MaxEntityDefinitionKeyBytes ||
        data[15] > static_cast<std::uint8_t>(ShieldType::Sphere)) {
        return DecodeError::InvalidValue;
    }
    const std::size_t minimumSize =
        EntityContentHeaderSize + definitionKeyLength +
        static_cast<std::size_t>(voxelCount) * EntityVoxelStateSize;
    if (size < minimumSize) {
        return DecodeError::InvalidLength;
    }

    EntityContentSnapshot decoded;
    decoded.shieldType = static_cast<ShieldType>(data[15]);
    decoded.id = EntityId{readU32(data + 2), readU32(data + 6)};
    if (!decoded.id.valid()) {
        return DecodeError::InvalidValue;
    }
    std::set<std::tuple<std::int32_t, std::int32_t, std::int32_t>> cells;
    std::size_t offset = EntityContentHeaderSize;
    if (definitionKeyLength != 0) {
        decoded.definitionKey.assign(
            reinterpret_cast<const char*>(data + offset),
            definitionKeyLength);
    }
    offset += definitionKeyLength;
    decoded.voxels.reserve(voxelCount);
    for (std::uint16_t index = 0; index < voxelCount; ++index) {
        EntityVoxelState voxel;
        voxel.cell = glm::ivec3(
            readI32(data + offset),
            readI32(data + offset + 4),
            readI32(data + offset + 8));
        voxel.color = readU32(data + offset + 12);
        voxel.density = readFloat(data + offset + 16);
        voxel.hp = readFloat(data + offset + 20);
        voxel.maxHP = readFloat(data + offset + 24);
        offset += EntityVoxelStateSize;
        if (!std::isfinite(voxel.density) || voxel.density < 0.0f ||
            !std::isfinite(voxel.hp) || voxel.hp < 0.0f ||
            !std::isfinite(voxel.maxHP) || voxel.maxHP < 0.0f ||
            voxel.hp > voxel.maxHP ||
            !cells.emplace(voxel.cell.x, voxel.cell.y, voxel.cell.z).second) {
            return DecodeError::InvalidValue;
        }
        decoded.voxels.push_back(voxel);
    }
    const DecodeError equipmentError = readEquipmentStates(
        data, size, equipmentCount, offset, decoded.equipment);
    if (equipmentError != DecodeError::None) {
        return equipmentError;
    }
    if (offset != size) {
        return DecodeError::InvalidLength;
    }
    content = std::move(decoded);
    return DecodeError::None;
}

bool encodeEntityEquipmentSnapshot(
    const EntityEquipmentSnapshot& equipment,
    std::vector<std::uint8_t>& packet
) {
    if (!equipment.id.valid()) {
        return false;
    }

    std::size_t packetSize = EntityEquipmentSnapshotHeaderSize;
    if (!validateEquipmentStates(equipment.equipment, packetSize)) {
        return false;
    }

    std::vector<std::uint8_t> encoded(packetSize, 0);
    encoded[0] = ProtocolVersion;
    encoded[1] = EntityEquipmentSnapshotMessageType;
    writeU32(encoded.data() + 2, equipment.id.index);
    writeU32(encoded.data() + 6, equipment.id.generation);
    writeU16(
        encoded.data() + 10,
        static_cast<std::uint16_t>(equipment.equipment.size()));
    std::size_t offset = EntityEquipmentSnapshotHeaderSize;
    writeEquipmentStates(encoded, offset, equipment.equipment);
    packet = std::move(encoded);
    return true;
}

DecodeError decodeEntityEquipmentSnapshot(
    const std::uint8_t* data,
    std::size_t size,
    EntityEquipmentSnapshot& equipment
) {
    if (data == nullptr || size < EntityEquipmentSnapshotHeaderSize) {
        return DecodeError::InvalidLength;
    }
    if (data[0] != ProtocolVersion) {
        return DecodeError::InvalidVersion;
    }
    if (data[1] != EntityEquipmentSnapshotMessageType) {
        return DecodeError::InvalidMessageType;
    }

    const std::uint16_t count = readU16(data + 10);
    if (count > MaxEntityEquipmentStates) {
        return DecodeError::InvalidCount;
    }
    EntityEquipmentSnapshot decoded;
    decoded.id = EntityId{readU32(data + 2), readU32(data + 6)};
    if (!decoded.id.valid()) {
        return DecodeError::InvalidValue;
    }
    std::size_t offset = EntityEquipmentSnapshotHeaderSize;
    const DecodeError equipmentError = readEquipmentStates(
        data, size, count, offset, decoded.equipment);
    if (equipmentError != DecodeError::None) {
        return equipmentError;
    }
    if (offset != size) {
        return DecodeError::InvalidLength;
    }
    equipment = std::move(decoded);
    return DecodeError::None;
}

bool buildSnapshotDelta(
    std::uint64_t sequence,
    std::uint64_t tick,
    std::uint64_t baselineSequence,
    const std::vector<EntitySnapshot>& current,
    const std::vector<EntitySnapshot>& baseline,
    SnapshotFrame& frame
) {
    if (sequence == 0 || baselineSequence >= sequence ||
        (baselineSequence == 0 && !baseline.empty()) ||
        current.size() > MaxSnapshotEntries ||
        baseline.size() > MaxSnapshotEntries) {
        return false;
    }

    std::unordered_map<std::uint64_t, const EntitySnapshot*> baselineById;
    for (const EntitySnapshot& snapshot : baseline) {
        if (!validSnapshot(snapshot) ||
            !baselineById.emplace(entityIdKey(snapshot.id), &snapshot).second) {
            return false;
        }
    }

    std::unordered_set<std::uint64_t> currentIds;
    SnapshotFrame delta;
    delta.sequence = sequence;
    delta.tick = tick;
    delta.baselineSequence = baselineSequence;
    for (const EntitySnapshot& snapshot : current) {
        const std::uint64_t key = entityIdKey(snapshot.id);
        if (!validSnapshot(snapshot) || !currentIds.insert(key).second) {
            return false;
        }
        const auto previous = baselineById.find(key);
        if (previous == baselineById.end() || !(snapshot == *previous->second)) {
            delta.updates.push_back(snapshot);
        }
    }
    for (const EntitySnapshot& snapshot : baseline) {
        if (currentIds.find(entityIdKey(snapshot.id)) == currentIds.end()) {
            delta.removals.push_back(snapshot.id);
        }
    }

    const auto byId = [](EntityId left, EntityId right) {
        return left.index < right.index ||
            (left.index == right.index && left.generation < right.generation);
    };
    std::sort(delta.updates.begin(), delta.updates.end(),
        [&byId](const EntitySnapshot& left, const EntitySnapshot& right) {
            return byId(left.id, right.id);
        });
    std::sort(delta.removals.begin(), delta.removals.end(), byId);
    frame = std::move(delta);
    return true;
}

bool applySnapshotFrame(
    const SnapshotFrame& frame,
    std::uint64_t expectedBaselineSequence,
    std::vector<EntitySnapshot>& replicaState
) {
    if (frame.sequence == 0 ||
        frame.baselineSequence != expectedBaselineSequence ||
        expectedBaselineSequence >= frame.sequence ||
        (expectedBaselineSequence == 0 && !replicaState.empty()) ||
        replicaState.size() > MaxSnapshotEntries ||
        frame.updates.size() > MaxSnapshotEntries ||
        frame.removals.size() > MaxSnapshotEntries) {
        return false;
    }

    std::unordered_map<std::uint64_t, EntitySnapshot> updatedState;
    for (const EntitySnapshot& snapshot : replicaState) {
        if (!validSnapshot(snapshot) ||
            !updatedState.emplace(entityIdKey(snapshot.id), snapshot).second) {
            return false;
        }
    }

    std::unordered_set<std::uint64_t> frameIds;
    for (const EntitySnapshot& snapshot : frame.updates) {
        const std::uint64_t key = entityIdKey(snapshot.id);
        if (!validSnapshot(snapshot) || !frameIds.insert(key).second) {
            return false;
        }
        updatedState[key] = snapshot;
    }
    for (EntityId id : frame.removals) {
        const std::uint64_t key = entityIdKey(id);
        if (!id.valid() || !frameIds.insert(key).second ||
            updatedState.erase(key) == 0) {
            return false;
        }
    }
    if (updatedState.size() > MaxSnapshotEntries) {
        return false;
    }

    std::vector<EntitySnapshot> nextState;
    nextState.reserve(updatedState.size());
    for (auto& entry : updatedState) {
        nextState.push_back(std::move(entry.second));
    }
    std::sort(nextState.begin(), nextState.end(),
        [](const EntitySnapshot& left, const EntitySnapshot& right) {
            return left.id.index < right.id.index ||
                (left.id.index == right.id.index &&
                    left.id.generation < right.id.generation);
        });
    replicaState = std::move(nextState);
    return true;
}

bool encodeSnapshotFrame(const SnapshotFrame& frame, std::vector<std::uint8_t>& packet) {
    if (frame.sequence == 0 || frame.baselineSequence >= frame.sequence ||
        frame.updates.size() > MaxSnapshotEntries ||
        frame.removals.size() > MaxSnapshotEntries) {
        return false;
    }

    std::unordered_set<std::uint64_t> entityIds;
    for (const EntitySnapshot& snapshot : frame.updates) {
        if (!validSnapshot(snapshot) ||
            !entityIds.insert(entityIdKey(snapshot.id)).second) {
            return false;
        }
    }
    for (EntityId id : frame.removals) {
        if (!id.valid() || !entityIds.insert(entityIdKey(id)).second) {
            return false;
        }
    }

    const std::size_t packetSize =
        SnapshotHeaderSize +
        frame.updates.size() * SnapshotEntitySize +
        frame.removals.size() * SnapshotEntityIdSize;
    std::vector<std::uint8_t> encoded(packetSize, 0);
    encoded[0] = ProtocolVersion;
    encoded[1] = SnapshotFrameMessageType;
    writeU64(encoded.data() + 2, frame.sequence);
    writeU64(encoded.data() + 10, frame.tick);
    writeU64(encoded.data() + 18, frame.baselineSequence);
    writeU16(encoded.data() + 26, static_cast<std::uint16_t>(frame.updates.size()));
    writeU16(encoded.data() + 28, static_cast<std::uint16_t>(frame.removals.size()));

    std::size_t offset = SnapshotHeaderSize;
    for (const EntitySnapshot& snapshot : frame.updates) {
        writeU32(encoded.data() + offset, snapshot.id.index);
        writeU32(encoded.data() + offset + 4, snapshot.id.generation);
        offset += 8;
        encoded[offset++] = static_cast<std::uint8_t>(snapshot.kind);
        encoded[offset++] = static_cast<std::uint8_t>(
            std::lround(snapshot.shieldFraction * 255.0f));
        encoded[offset++] = static_cast<std::uint8_t>(
            std::lround(snapshot.hullFraction * 255.0f));
        const float position[] = {
            snapshot.position.x, snapshot.position.y, snapshot.position.z
        };
        for (float value : position) {
            writeFloat(encoded.data() + offset, value);
            offset += sizeof(float);
        }
        writeCompressedQuaternion(encoded.data() + offset, snapshot.orientation);
        offset += 7;
        const float velocities[] = {
            snapshot.directionalVelocity.x, snapshot.directionalVelocity.y,
            snapshot.directionalVelocity.z,
            snapshot.angularVelocity.x, snapshot.angularVelocity.y,
            snapshot.angularVelocity.z
        };
        for (float value : velocities) {
            writeFloat(encoded.data() + offset, value);
            offset += sizeof(float);
        }
        writeFloat(encoded.data() + offset, snapshot.scale);
        offset += sizeof(float);
        writeFloat(encoded.data() + offset, snapshot.maxMass);
        offset += sizeof(float);
        for (float value : {
                 snapshot.engineState.x,
                 snapshot.engineState.y,
                 snapshot.engineState.z}) {
            const auto quantized = static_cast<std::int8_t>(
                std::lround(glm::clamp(value, -1.0f, 1.0f) * 127.0f));
            encoded[offset++] = static_cast<std::uint8_t>(quantized);
        }
        encoded[offset++] = snapshot.hostile;
    }
    for (EntityId id : frame.removals) {
        writeU32(encoded.data() + offset, id.index);
        writeU32(encoded.data() + offset + 4, id.generation);
        offset += SnapshotEntityIdSize;
    }
    packet = std::move(encoded);
    return true;
}

DecodeError decodeSnapshotFrame(
    const std::uint8_t* data,
    std::size_t size,
    SnapshotFrame& frame
) {
    if (data == nullptr || size < SnapshotHeaderSize) {
        return DecodeError::InvalidLength;
    }
    if (data[0] != ProtocolVersion) {
        return DecodeError::InvalidVersion;
    }
    if (data[1] != SnapshotFrameMessageType) {
        return DecodeError::InvalidMessageType;
    }

    const std::uint16_t updateCount = readU16(data + 26);
    const std::uint16_t removalCount = readU16(data + 28);
    if (updateCount > MaxSnapshotEntries || removalCount > MaxSnapshotEntries) {
        return DecodeError::InvalidCount;
    }
    const std::size_t expectedSize =
        SnapshotHeaderSize +
        static_cast<std::size_t>(updateCount) * SnapshotEntitySize +
        static_cast<std::size_t>(removalCount) * SnapshotEntityIdSize;
    if (size != expectedSize) {
        return DecodeError::InvalidLength;
    }

    SnapshotFrame decoded;
    decoded.sequence = readU64(data + 2);
    decoded.tick = readU64(data + 10);
    decoded.baselineSequence = readU64(data + 18);
    if (decoded.sequence == 0 || decoded.baselineSequence >= decoded.sequence) {
        return DecodeError::InvalidValue;
    }

    std::unordered_set<std::uint64_t> entityIds;
    std::size_t offset = SnapshotHeaderSize;
    decoded.updates.reserve(updateCount);
    for (std::uint16_t index = 0; index < updateCount; ++index) {
        EntitySnapshot snapshot;
        snapshot.id = EntityId{readU32(data + offset), readU32(data + offset + 4)};
        offset += 8;
        snapshot.kind = static_cast<EntitySnapshot::Kind>(data[offset++]);
        snapshot.shieldFraction =
            static_cast<float>(data[offset++]) / 255.0f;
        snapshot.hullFraction =
            static_cast<float>(data[offset++]) / 255.0f;
        float position[3];
        for (float& value : position) {
            value = readFloat(data + offset);
            offset += sizeof(float);
        }
        snapshot.position = glm::vec3(position[0], position[1], position[2]);
        if (!readCompressedQuaternion(data + offset, snapshot.orientation)) {
            return DecodeError::InvalidValue;
        }
        offset += 7;
        float velocities[6];
        for (float& value : velocities) {
            value = readFloat(data + offset);
            offset += sizeof(float);
        }
        snapshot.directionalVelocity =
            glm::vec3(velocities[0], velocities[1], velocities[2]);
        snapshot.angularVelocity =
            glm::vec3(velocities[3], velocities[4], velocities[5]);
        snapshot.scale = readFloat(data + offset);
        offset += sizeof(float);
        snapshot.maxMass = readFloat(data + offset);
        offset += sizeof(float);
        float engineState[3];
        for (float& value : engineState) {
            int quantized = data[offset++];
            if (quantized > 127) {
                quantized -= 256;
            }
            if (quantized == -128) {
                return DecodeError::InvalidValue;
            }
            value = static_cast<float>(quantized) / 127.0f;
        }
        snapshot.engineState =
            glm::vec3(engineState[0], engineState[1], engineState[2]);
        const auto hostile = data[offset++];
        if (hostile > 1) {
            return DecodeError::InvalidValue;
        }
        snapshot.hostile = hostile != 0;
        if (!validSnapshot(snapshot) ||
            !entityIds.insert(entityIdKey(snapshot.id)).second) {
            return DecodeError::InvalidValue;
        }
        decoded.updates.push_back(snapshot);
    }
    decoded.removals.reserve(removalCount);
    for (std::uint16_t index = 0; index < removalCount; ++index) {
        const EntityId id{readU32(data + offset), readU32(data + offset + 4)};
        offset += SnapshotEntityIdSize;
        if (!id.valid() || !entityIds.insert(entityIdKey(id)).second) {
            return DecodeError::InvalidValue;
        }
        decoded.removals.push_back(id);
    }
    frame = std::move(decoded);
    return DecodeError::None;
}

} // namespace protocol
} // namespace voxellancer
