#include "tcpclient.h"

#include <algorithm>
#include <array>
#include <utility>
#include <limits>

#include <SFML/System/Time.hpp>

namespace {
    constexpr std::size_t FrameHeaderSize = 4;
    constexpr std::size_t MaxIncomingBytes = 1024 * 1024;

    void writeFrameLength(std::uint8_t* data, std::uint32_t length) {
        data[0] = static_cast<std::uint8_t>(length);
        data[1] = static_cast<std::uint8_t>(length >> 8);
        data[2] = static_cast<std::uint8_t>(length >> 16);
        data[3] = static_cast<std::uint8_t>(length >> 24);
    }

    std::uint32_t readFrameLength(const std::uint8_t* data) {
        return static_cast<std::uint32_t>(data[0]) |
            (static_cast<std::uint32_t>(data[1]) << 8) |
            (static_cast<std::uint32_t>(data[2]) << 16) |
            (static_cast<std::uint32_t>(data[3]) << 24);
    }

    std::uint64_t entityIdKey(voxellancer::protocol::EntityId entity) {
        return (static_cast<std::uint64_t>(entity.index) << 32) |
            static_cast<std::uint64_t>(entity.generation);
    }
}

bool TcpClient::connectLocal(std::uint16_t port) {
    if (m_connected || port == 0) {
        return fail("Cannot connect using the current TCP client state or port.");
    }
    const sf::Socket::Status status =
        m_socket.connect(sf::IpAddress::LocalHost, port, sf::seconds(2));
    if (status != sf::Socket::Status::Done) {
        return fail("Could not connect to the local simulation server.");
    }
    m_socket.setBlocking(false);
    m_connected = true;
    m_lastError.clear();
    return true;
}

void TcpClient::disconnect() {
    if (m_connected) {
        m_socket.disconnect();
        m_connected = false;
    }
}

bool TcpClient::poll() {
    if (!m_connected) {
        return fail("TCP client is not connected.");
    }
    return flushOutput() && receiveFrames() && flushOutput();
}

bool TcpClient::sendInput(const voxellancer::protocol::InputCommand& command) {
    if (!m_connected || !m_actor) {
        return fail("Cannot send input before the server assigns an actor.");
    }
    voxellancer::protocol::InputCommandPacket packet{};
    if (!voxellancer::protocol::encodeInputCommand(command, packet)) {
        return fail("Input command is invalid for this protocol version.");
    }
    return queuePacket(packet.data(), packet.size());
}

bool TcpClient::sendContractAction(voxellancer::protocol::ContractActionKind action,
    std::uint8_t weapon, glm::ivec3 cell, std::uint8_t node,
    voxellancer::protocol::RewardChoice choice, std::uint8_t shield) {
    if (!m_connected || !m_actor || !m_contractStatus ||
        m_contractActionSequence == std::numeric_limits<std::uint64_t>::max()) {
        return fail("Cannot send a station action without an active contract session.");
    }
    voxellancer::protocol::ContractActionPacket packet{};
    if (!voxellancer::protocol::encodeContractAction(
            {m_contractActionSequence + 1, m_contractStatus->mission, action, weapon, cell, node, choice, shield}, packet) ||
        !queuePacket(packet.data(), packet.size())) {
        return fail("Could not queue the station action.");
    }
    ++m_contractActionSequence;
    return true;
}

std::optional<voxellancer::protocol::ContractStatus> TcpClient::contractStatus() const {
    return m_contractStatus;
}

bool TcpClient::requestSnapshotResync() {
    if (!m_connected || !m_actor) {
        return fail("Cannot request snapshot resynchronization before actor assignment.");
    }
    if (m_resyncPending) {
        return true;
    }

    voxellancer::protocol::SnapshotResyncRequestPacket packet{};
    if (!voxellancer::protocol::encodeSnapshotResyncRequest(packet) ||
        !queuePacket(packet.data(), packet.size())) {
        return fail("Could not queue a snapshot resynchronization request.");
    }

    m_snapshotSequence = 0;
    m_snapshotTick = 0;
    m_replicaState.clear();
    for (const auto& content : m_entityContents) {
        markEntityContentChanged(content.id);
    }
    m_entityContents.clear();
    m_replicaVoxelStateCount = 0;
    m_replicaEquipmentStateCount = 0;
    m_voxelRemovalEvents.clear();
    m_voxelDamageEvents.clear();
    m_shieldHitEvents.clear();
    m_missionStateEvents.clear();
    m_weaponFiredEvents.clear();
    m_snapshotInterpolator.reset();
    m_resyncPending = true;
    return true;
}

std::optional<voxellancer::protocol::EntityId> TcpClient::actor() const {
    return m_actor;
}

std::optional<voxellancer::protocol::LobbyStatus> TcpClient::lobbyStatus() const {
    return m_lobbyStatus;
}

std::uint64_t TcpClient::snapshotSequence() const {
    return m_snapshotSequence;
}

std::uint64_t TcpClient::snapshotTick() const {
    return m_snapshotTick;
}

bool TcpClient::interpolationReady() const {
    return m_snapshotInterpolator.hasInterpolationPair();
}

const std::vector<voxellancer::protocol::EntitySnapshot>& TcpClient::replicaState() const {
    return m_replicaState;
}

const std::vector<voxellancer::protocol::EntityContentSnapshot>&
TcpClient::entityContents() const {
    return m_entityContents;
}

std::vector<voxellancer::protocol::EntityId>
TcpClient::drainEntityContentChanges() {
    std::vector<voxellancer::protocol::EntityId> changes;
    changes.swap(m_changedEntityContents);
    m_changedEntityContentKeys.clear();
    return changes;
}

void TcpClient::markEntityContentChanged(
    voxellancer::protocol::EntityId entity)
{
    if (m_changedEntityContentKeys.insert(entityIdKey(entity)).second) {
        m_changedEntityContents.push_back(entity);
    }
}

std::optional<voxellancer::protocol::EntitySnapshot> TcpClient::sample(
    voxellancer::protocol::EntityId entity,
    double renderTick
) const {
    return m_snapshotInterpolator.sample(entity, renderTick);
}

std::vector<voxellancer::protocol::InputCommandAcknowledgement>
TcpClient::drainAcknowledgements() {
    std::vector<voxellancer::protocol::InputCommandAcknowledgement> acknowledgements;
    acknowledgements.swap(m_acknowledgements);
    return acknowledgements;
}

std::vector<voxellancer::protocol::VoxelRemovalEvent>
TcpClient::drainVoxelRemovalEvents() {
    std::vector<voxellancer::protocol::VoxelRemovalEvent> events;
    events.swap(m_voxelRemovalEvents);
    return events;
}

std::vector<voxellancer::protocol::VoxelDamageEvent>
TcpClient::drainVoxelDamageEvents() {
    std::vector<voxellancer::protocol::VoxelDamageEvent> events;
    events.swap(m_voxelDamageEvents);
    return events;
}

std::vector<voxellancer::protocol::ShieldHitEvent>
TcpClient::drainShieldHitEvents() {
    std::vector<voxellancer::protocol::ShieldHitEvent> events;
    events.swap(m_shieldHitEvents);
    return events;
}

std::vector<voxellancer::protocol::MissionStateEvent>
TcpClient::drainMissionStateEvents() {
    std::vector<voxellancer::protocol::MissionStateEvent> events;
    events.swap(m_missionStateEvents);
    return events;
}

std::vector<voxellancer::protocol::WeaponFiredEvent>
TcpClient::drainWeaponFiredEvents() {
    std::vector<voxellancer::protocol::WeaponFiredEvent> events;
    events.swap(m_weaponFiredEvents);
    return events;
}

const std::string& TcpClient::lastError() const {
    return m_lastError;
}

bool TcpClient::receiveFrames() {
    std::array<std::uint8_t, 4096> buffer{};
    while (true) {
        std::size_t received = 0;
        const sf::Socket::Status status =
            m_socket.receive(buffer.data(), buffer.size(), received);
        if (status == sf::Socket::Status::NotReady) {
            return true;
        }
        if (status == sf::Socket::Status::Disconnected) {
            m_connected = false;
            return fail("Simulation server disconnected.");
        }
        if (status == sf::Socket::Status::Error) {
            m_connected = false;
            return fail("TCP receive failed.");
        }
        if (received == 0) {
            return true;
        }
        if (m_incoming.size() + received > MaxIncomingBytes) {
            m_connected = false;
            return fail("Server sent more unprocessed TCP data than the client allows.");
        }
        m_incoming.insert(
            m_incoming.end(), buffer.begin(), buffer.begin() + received);
        if (!processFrames()) {
            m_connected = false;
            return false;
        }
    }
}

bool TcpClient::processFrames() {
    while (m_incoming.size() >= FrameHeaderSize) {
        const std::uint32_t packetSize = readFrameLength(m_incoming.data());
        if (packetSize < 2 || packetSize > MaxFrameSize) {
            return fail("Server sent an invalid framed packet length.");
        }
        const std::size_t frameSize = FrameHeaderSize + packetSize;
        if (m_incoming.size() < frameSize) {
            return true;
        }

        const std::uint8_t* packet = m_incoming.data() + FrameHeaderSize;
        if (packet[0] != voxellancer::protocol::ProtocolVersion) {
            return fail("Server protocol version is unsupported.");
        }
        if (m_resyncPending && packet[1] >= 6 && packet[1] <= 12) {
            m_incoming.erase(
                m_incoming.begin(),
                m_incoming.begin() + static_cast<std::ptrdiff_t>(frameSize));
            continue;
        }
        switch (packet[1]) {
        case 15: {
            voxellancer::protocol::ContractStatus status;
            if (!m_actor || voxellancer::protocol::decodeContractStatus(
                    packet, packetSize, status) != voxellancer::protocol::DecodeError::None) {
                return fail("Server sent an invalid contract status.");
            }
            m_contractStatus = status;
            break;
        }
        case 13: {
            voxellancer::protocol::LobbyStatus status;
            if (voxellancer::protocol::decodeLobbyStatus(
                    packet, packetSize, status) !=
                voxellancer::protocol::DecodeError::None) {
                return fail("Server sent an invalid multiplayer lobby status.");
            }
            m_lobbyStatus = status;
            break;
        }
        case 1: {
            if (m_actor) {
                return fail("Server attempted to assign more than one actor.");
            }
            voxellancer::protocol::EntityId actor;
            if (voxellancer::protocol::decodeEntityId(
                    packet, packetSize, actor) !=
                voxellancer::protocol::DecodeError::None) {
                return fail("Server sent an invalid actor assignment.");
            }
            m_actor = actor;
            break;
        }
        case 3: {
            if (!m_actor) {
                return fail("Server sent a snapshot before actor assignment.");
            }
            voxellancer::protocol::SnapshotFrame frame;
            if (voxellancer::protocol::decodeSnapshotFrame(
                    packet, packetSize, frame) !=
                voxellancer::protocol::DecodeError::None) {
                return fail("Server snapshot is invalid or requires resynchronization.");
            }
            if (m_resyncPending && frame.baselineSequence != 0) {
                m_incoming.erase(
                    m_incoming.begin(),
                    m_incoming.begin() + static_cast<std::ptrdiff_t>(frameSize));
                continue;
            }
            if (!voxellancer::protocol::applySnapshotFrame(
                    frame, m_snapshotSequence, m_replicaState) ||
                !m_snapshotInterpolator.push(frame.tick, m_replicaState)) {
                if (!requestSnapshotResync()) {
                    return false;
                }
                m_incoming.erase(
                    m_incoming.begin(),
                    m_incoming.begin() + static_cast<std::ptrdiff_t>(frameSize));
                continue;
            }
            for (const auto removed : frame.removals) {
                const auto content = std::find_if(
                    m_entityContents.begin(),
                    m_entityContents.end(),
                    [removed](const auto& candidate) {
                        return candidate.id == removed;
                    });
                if (content != m_entityContents.end()) {
                    if (content->voxels.size() > m_replicaVoxelStateCount ||
                        content->equipment.size() > m_replicaEquipmentStateCount) {
                        return fail("Client replicated content counts are inconsistent.");
                    }
                    m_replicaVoxelStateCount -= content->voxels.size();
                    m_replicaEquipmentStateCount -= content->equipment.size();
                    markEntityContentChanged(removed);
                    m_entityContents.erase(content);
                }
            }
            m_snapshotSequence = frame.sequence;
            m_snapshotTick = frame.tick;
            m_resyncPending = false;
            break;
        }
        case 4: {
            if (!m_actor ||
                m_acknowledgements.size() >= MaxAcknowledgements) {
                return fail("Server sent an unexpected or excessive input acknowledgement.");
            }
            voxellancer::protocol::InputCommandAcknowledgement acknowledgement;
            if (voxellancer::protocol::decodeInputCommandAcknowledgement(
                    packet, packetSize, acknowledgement) !=
                voxellancer::protocol::DecodeError::None) {
                return fail("Server sent an invalid input acknowledgement.");
            }
            m_acknowledgements.push_back(acknowledgement);
            break;
        }
        case 6: {
            if (!m_actor || m_voxelRemovalEvents.size() >= MaxVoxelRemovalEvents) {
                return fail("Server sent an unexpected or excessive voxel removal event.");
            }
            voxellancer::protocol::VoxelRemovalEvent event;
            if (voxellancer::protocol::decodeVoxelRemovalEvent(
                    packet, packetSize, event) !=
                voxellancer::protocol::DecodeError::None) {
                return fail("Server sent an invalid voxel removal event.");
            }
            const auto content = std::find_if(
                m_entityContents.begin(),
                m_entityContents.end(),
                [&event](const auto& candidate) {
                    return candidate.id == event.entity;
                });
            if (content == m_entityContents.end()) {
                return fail("Server removed a voxel from unknown entity content.");
            }
            const auto voxel = std::find_if(
                content->voxels.begin(),
                content->voxels.end(),
                [&event](const auto& candidate) {
                    return candidate.cell == event.cell;
                });
            if (voxel != content->voxels.end()) {
                content->voxels.erase(voxel);
                --m_replicaVoxelStateCount;
                markEntityContentChanged(event.entity);
            }
            m_voxelRemovalEvents.push_back(event);
            break;
        }
        case 7: {
            if (!m_actor) {
                return fail("Server sent entity content before actor assignment.");
            }
            voxellancer::protocol::EntityContentSnapshot content;
            if (voxellancer::protocol::decodeEntityContent(
                    packet, packetSize, content) !=
                voxellancer::protocol::DecodeError::None) {
                return fail("Server sent invalid entity content.");
            }
            if (std::none_of(
                    m_replicaState.begin(),
                    m_replicaState.end(),
                    [&content](const auto& snapshot) {
                        return snapshot.id == content.id;
                    })) {
                return fail("Server sent content for an unknown entity.");
            }
            const auto existing = std::find_if(
                m_entityContents.begin(),
                m_entityContents.end(),
                [&content](const auto& candidate) {
                    return candidate.id == content.id;
                });
            const std::size_t oldVoxelCount =
                existing == m_entityContents.end() ? 0 : existing->voxels.size();
            const std::size_t oldEquipmentCount =
                existing == m_entityContents.end() ? 0 : existing->equipment.size();
            const auto contentId = content.id;
            if (oldVoxelCount > m_replicaVoxelStateCount ||
                oldEquipmentCount > m_replicaEquipmentStateCount ||
                content.voxels.size() >
                    MaxReplicaVoxelStates -
                        (m_replicaVoxelStateCount - oldVoxelCount) ||
                content.equipment.size() >
                    MaxReplicaEquipmentStates -
                        (m_replicaEquipmentStateCount - oldEquipmentCount)) {
                return fail("Server exceeded the client's replicated content-state limit.");
            }
            m_replicaVoxelStateCount =
                m_replicaVoxelStateCount - oldVoxelCount + content.voxels.size();
            m_replicaEquipmentStateCount =
                m_replicaEquipmentStateCount - oldEquipmentCount +
                content.equipment.size();
            if (existing == m_entityContents.end()) {
                m_entityContents.push_back(std::move(content));
            } else {
                *existing = std::move(content);
            }
            markEntityContentChanged(contentId);
            break;
        }
        case 8: {
            if (!m_actor) {
                return fail("Server sent voxel damage before actor assignment.");
            }
            if (m_voxelDamageEvents.size() >= MaxVoxelDamageEvents) {
                return fail("Server sent an excessive number of voxel damage events.");
            }
            voxellancer::protocol::VoxelDamageEvent event;
            if (voxellancer::protocol::decodeVoxelDamageEvent(
                    packet, packetSize, event) !=
                voxellancer::protocol::DecodeError::None) {
                return fail("Server sent an invalid voxel damage event.");
            }
            const auto content = std::find_if(
                m_entityContents.begin(),
                m_entityContents.end(),
                [&event](const auto& candidate) {
                    return candidate.id == event.entity;
                });
            if (content == m_entityContents.end()) {
                return fail("Server damaged a voxel in unknown entity content.");
            }
            const auto voxel = std::find_if(
                content->voxels.begin(),
                content->voxels.end(),
                [&event](const auto& candidate) {
                    return candidate.cell == event.cell;
                });
            if (voxel == content->voxels.end()) {
                return fail("Server damaged a voxel missing from entity content.");
            }
            if (event.hp > voxel->maxHP) {
                return fail("Server damaged a voxel beyond its replicated maximum HP.");
            }
            voxel->hp = event.hp;
            markEntityContentChanged(event.entity);
            m_voxelDamageEvents.push_back(event);
            break;
        }
        case 9: {
            if (!m_actor) {
                return fail("Server sent an equipment update before actor assignment.");
            }
            voxellancer::protocol::EntityEquipmentSnapshot update;
            if (voxellancer::protocol::decodeEntityEquipmentSnapshot(
                    packet, packetSize, update) !=
                voxellancer::protocol::DecodeError::None) {
                return fail("Server sent an invalid equipment update.");
            }
            const auto content = std::find_if(
                m_entityContents.begin(),
                m_entityContents.end(),
                [&update](const auto& candidate) {
                    return candidate.id == update.id;
                });
            if (content == m_entityContents.end()) {
                return fail("Server updated equipment for unknown entity content.");
            }
            if (content->equipment.size() > m_replicaEquipmentStateCount ||
                update.equipment.size() >
                    MaxReplicaEquipmentStates -
                        (m_replicaEquipmentStateCount - content->equipment.size())) {
                return fail("Server exceeded the client's replicated equipment-state limit.");
            }
            m_replicaEquipmentStateCount =
                m_replicaEquipmentStateCount - content->equipment.size() +
                update.equipment.size();
            content->equipment = std::move(update.equipment);
            markEntityContentChanged(update.id);
            break;
        }
        case 10: {
            if (!m_actor || m_weaponFiredEvents.size() >= MaxWeaponFiredEvents) {
                return fail("Server sent an unexpected or excessive weapon-fired event.");
            }
            voxellancer::protocol::WeaponFiredEvent event;
            if (voxellancer::protocol::decodeWeaponFiredEvent(
                    packet, packetSize, event) !=
                voxellancer::protocol::DecodeError::None) {
                return fail("Server sent an invalid weapon-fired event.");
            }
            if (std::none_of(
                    m_replicaState.begin(),
                    m_replicaState.end(),
                    [&event](const auto& entity) {
                        return entity.id == event.entity;
                    })) {
                return fail("Server sent a weapon-fired event for an unknown entity.");
            }
            m_weaponFiredEvents.push_back(event);
            break;
        }
        case 11: {
            if (!m_actor || m_shieldHitEvents.size() >= MaxShieldHitEvents) {
                return fail("Server sent an unexpected or excessive shield-hit event.");
            }
            voxellancer::protocol::ShieldHitEvent event;
            if (voxellancer::protocol::decodeShieldHitEvent(
                    packet, packetSize, event) !=
                voxellancer::protocol::DecodeError::None) {
                return fail("Server sent an invalid shield-hit event.");
            }
            if (std::none_of(
                    m_replicaState.begin(),
                    m_replicaState.end(),
                    [&event](const auto& entity) {
                        return entity.id == event.entity;
                    })) {
                return fail("Server sent a shield-hit event for an unknown entity.");
            }
            m_shieldHitEvents.push_back(event);
            break;
        }
        case 12: {
            if (!m_actor ||
                m_missionStateEvents.size() >= MaxMissionStateEvents) {
                return fail("Server sent an unexpected or excessive mission-state event.");
            }
            voxellancer::protocol::MissionStateEvent event;
            if (voxellancer::protocol::decodeMissionStateEvent(
                    packet, packetSize, event) !=
                voxellancer::protocol::DecodeError::None) {
                return fail("Server sent an invalid mission-state event.");
            }
            m_missionStateEvents.push_back(std::move(event));
            break;
        }
        default:
            return fail("Server sent an unsupported message type.");
        }
        m_incoming.erase(
            m_incoming.begin(),
            m_incoming.begin() + static_cast<std::ptrdiff_t>(frameSize));
    }
    return true;
}

bool TcpClient::flushOutput() {
    while (!m_outgoing.empty()) {
        std::vector<std::uint8_t>& frame = m_outgoing.front();
        std::size_t sent = 0;
        const sf::Socket::Status status = m_socket.send(
            frame.data() + m_outgoingOffset,
            frame.size() - m_outgoingOffset,
            sent
        );
        if (status == sf::Socket::Status::NotReady) {
            return true;
        }
        if (status == sf::Socket::Status::Disconnected ||
            status == sf::Socket::Status::Error) {
            m_connected = false;
            return fail("TCP send failed.");
        }
        if (sent == 0) {
            return true;
        }
        m_outgoingOffset += sent;
        m_outgoingBytes -= sent;
        if (m_outgoingOffset == frame.size()) {
            m_outgoing.pop_front();
            m_outgoingOffset = 0;
        }
    }
    return true;
}

bool TcpClient::queuePacket(const std::uint8_t* data, std::size_t size) {
    if (size == 0 || size > MaxFrameSize ||
        m_outgoingBytes + size + FrameHeaderSize > MaxPendingOutputBytes) {
        return fail("Client outbound TCP queue is full or packet is too large.");
    }
    std::vector<std::uint8_t> frame(size + FrameHeaderSize);
    writeFrameLength(frame.data(), static_cast<std::uint32_t>(size));
    std::copy(data, data + size, frame.begin() + FrameHeaderSize);
    m_outgoingBytes += frame.size();
    m_outgoing.push_back(std::move(frame));
    return true;
}

bool TcpClient::fail(const char* message) {
    m_lastError = message;
    return false;
}
