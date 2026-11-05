#include "tcpserver.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

#include "ai/character.h"
#include "equipment/engine.h"
#include "equipment/engineslot.h"
#include "equipment/hardpoint.h"
#include "equipment/shield.h"
#include "equipment/shieldslot.h"
#include "equipment/weapon.h"
#include "simulation/simulationsession.h"
#include "voxel/specialvoxels/engineslotvoxel.h"
#include "voxel/specialvoxels/hardpointvoxel.h"
#include "voxel/voxel.h"
#include "world/world.h"
#include "world/worldlogic.h"
#include "missions/missionsystem.h"
#include "worldobject/ship.h"
#include "worldobject/worldobjectcomponents.h"
#include "worldobject/worldobject.h"

namespace {
    using voxellancer::protocol::EntityId;

    constexpr std::size_t FrameHeaderSize = 4;
    std::uint64_t entityIdKey(EntityId id) {
        return (static_cast<std::uint64_t>(id.generation) << 32) | id.index;
    }

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

    std::uint16_t equipmentGroup(const EquipmentSlot& slot) {
        const int group = slot.group();
        if (group < 0 ||
            group > std::numeric_limits<std::uint16_t>::max()) {
            throw std::runtime_error("World object has an unsupported equipment slot group.");
        }
        return static_cast<std::uint16_t>(group);
    }

    std::vector<voxellancer::protocol::EntityEquipmentState>
    collectEquipmentState(WorldObjectComponents& components) {
        std::vector<voxellancer::protocol::EntityEquipmentState> equipment;
        equipment.reserve(
            components.engineSlots().size() +
            components.hardpoints().size() +
            components.shieldSlots().size());
        for (const auto& slot : components.engineSlots()) {
            if (!slot || slot->voxel() == nullptr) {
                throw std::runtime_error("World object has an invalid engine slot.");
            }
            const auto& engine = slot->engine();
            equipment.push_back({
                voxellancer::protocol::EquipmentSlotKind::Engine,
                equipmentGroup(*slot),
                slot->voxel()->cell(),
                engine ? engine->equipmentKey() : std::string()
            });
        }
        for (const auto& slot : components.hardpoints()) {
            if (!slot || !slot->voxel()) {
                throw std::runtime_error("World object has an invalid hardpoint.");
            }
            const auto& weapon = slot->weapon();
            equipment.push_back({
                voxellancer::protocol::EquipmentSlotKind::Weapon,
                equipmentGroup(*slot),
                slot->voxel()->cell(),
                weapon ? weapon->equipmentKey() : std::string()
            });
        }
        for (const auto& slot : components.shieldSlots()) {
            if (!slot) {
                throw std::runtime_error("World object has an invalid shield slot.");
            }
            const auto& shield = slot->shield();
            equipment.push_back({
                voxellancer::protocol::EquipmentSlotKind::Shield,
                equipmentGroup(*slot),
                glm::ivec3(0),
                shield ? shield->equipmentKey() : std::string()
            });
        }
        std::sort(
            equipment.begin(),
            equipment.end(),
            [](const auto& left, const auto& right) {
                return std::tie(
                    left.kind,
                    left.group,
                    left.cell.x,
                    left.cell.y,
                    left.cell.z
                ) < std::tie(
                    right.kind,
                    right.group,
                    right.cell.x,
                    right.cell.y,
                    right.cell.z
                );
            });
        return equipment;
    }
}

TcpServer::TcpServer(
    World& world,
    SimulationSession& session,
    std::uint16_t port,
    float interestRadius,
    std::optional<voxellancer::protocol::EntityId> preferredActor
) :
    m_world(world),
    m_session(session),
    m_port(port),
    m_interestRadiusSquared(interestRadius * interestRadius),
    m_preferredActor(preferredActor)
{
    if (!std::isfinite(interestRadius) || interestRadius <= 0.0f ||
        !std::isfinite(m_interestRadiusSquared)) {
        throw std::invalid_argument("Interest radius must be a positive finite value.");
    }
    const sf::Socket::Status listenStatus =
        m_listener.listen(port, sf::IpAddress::LocalHost);
    if (listenStatus != sf::Socket::Status::Done) {
        throw std::runtime_error("Could not listen on the requested local TCP port.");
    }
    m_listener.setBlocking(false);
    m_port = m_listener.getLocalPort();
    m_world.worldLogic().setVoxelReplicationEnabled(true);
    m_world.worldLogic().setShieldHitEventReplicationEnabled(true);
    m_world.worldLogic().setWeaponEventReplicationEnabled(true);
    m_world.missionSystem().setStateEventReplicationEnabled(true);
}

TcpServer::~TcpServer() {
    m_world.worldLogic().setVoxelReplicationEnabled(false);
    m_world.worldLogic().setShieldHitEventReplicationEnabled(false);
    m_world.worldLogic().setWeaponEventReplicationEnabled(false);
    m_world.missionSystem().setStateEventReplicationEnabled(false);
    for (Connection& connection : m_connections) {
        connection.socket->disconnect();
    }
}

std::uint16_t TcpServer::port() const {
    return m_port;
}

std::size_t TcpServer::clientCount() const {
    return m_connections.size();
}

std::vector<TcpServer::ClientStatistics> TcpServer::clientStatistics() const {
    std::vector<ClientStatistics> statistics;
    statistics.reserve(m_connections.size());
    for (const Connection& connection : m_connections) {
        statistics.push_back(ClientStatistics{
            connection.actor,
            connection.bytesSent,
            connection.snapshotBaseline.size()
        });
    }
    return statistics;
}

void TcpServer::poll() {
    if (!acceptConnections()) {
        throw std::runtime_error("TCP listener failed while accepting a client.");
    }

    for (std::size_t index = 0; index < m_connections.size();) {
        Connection& connection = m_connections[index];
        if (!m_world.worldObject(connection.actor) ||
            !receiveInput(connection) ||
            !sendPending(connection)) {
            removeConnection(index);
            continue;
        }
        ++index;
    }

    sendAcknowledgements();
    sendVoxelDamageEvents(m_session.drainVoxelDamageEvents());
    sendShieldHitEvents(m_world.worldLogic().drainShieldHitEvents());
    sendMissionStateEvents(m_world.missionSystem().drainStateEvents());
    sendVoxelRemovalEvents(m_session.drainVoxelRemovalEvents());
    sendWeaponFiredEvents(m_world.worldLogic().drainWeaponFiredEvents());
    if (m_session.tickIndex() >= m_nextSnapshotTick) {
        broadcastSnapshot();
        const std::uint64_t completedTicks =
            m_session.tickIndex() / SnapshotIntervalTicks;
        m_nextSnapshotTick = (completedTicks + 1) * SnapshotIntervalTicks;
    }

    for (std::size_t index = 0; index < m_connections.size();) {
        if (!sendPending(m_connections[index])) {
            removeConnection(index);
        } else {
            ++index;
        }
    }
}

bool TcpServer::acceptConnections() {
    while (m_connections.size() < MaxClients) {
        auto socket = std::make_unique<sf::TcpSocket>();
        const sf::Socket::Status status = m_listener.accept(*socket);
        if (status == sf::Socket::Status::NotReady) {
            return true;
        }
        if (status != sf::Socket::Status::Done) {
            return false;
        }

        const auto actor = availableActor();
        if (!actor) {
            socket->disconnect();
            continue;
        }

        Ship* ship = static_cast<Ship*>(m_world.worldObject(*actor));
        ship->character()->setTask(nullptr);
        m_controlledActors.insert(entityIdKey(*actor));

        Connection connection;
        connection.socket = std::move(socket);
        connection.actor = *actor;
        connection.socket->setBlocking(false);

        voxellancer::protocol::EntityIdPacket actorPacket{};
        if (!voxellancer::protocol::encodeEntityId(*actor, actorPacket) ||
            !queuePacket(connection, actorPacket.data(), actorPacket.size()) ||
            !sendSnapshot(connection, ++m_snapshotSequence)) {
            connection.socket->disconnect();
            m_controlledActors.erase(entityIdKey(*actor));
            continue;
        }
        m_connections.push_back(std::move(connection));
    }
    return true;
}

bool TcpServer::receiveInput(Connection& connection) {
    std::array<std::uint8_t, 4096> buffer{};
    while (true) {
        std::size_t received = 0;
        const sf::Socket::Status status =
            connection.socket->receive(buffer.data(), buffer.size(), received);
        if (status == sf::Socket::Status::NotReady) {
            return true;
        }
        if (status == sf::Socket::Status::Disconnected ||
            status == sf::Socket::Status::Error) {
            return false;
        }
        if (received == 0) {
            return true;
        }
        if (connection.incoming.size() + received > MaxIncomingBytes) {
            return false;
        }
        connection.incoming.insert(
            connection.incoming.end(), buffer.begin(), buffer.begin() + received);
        if (!processInputFrames(connection)) {
            return false;
        }
    }
}

bool TcpServer::processInputFrames(Connection& connection) {
    while (connection.incoming.size() >= FrameHeaderSize) {
        const std::uint32_t packetSize = readFrameLength(connection.incoming.data());
        if (packetSize < 2 || packetSize > MaxIncomingBytes) {
            return false;
        }
        const std::size_t frameSize = FrameHeaderSize + packetSize;
        if (connection.incoming.size() < frameSize) {
            return true;
        }

        const std::uint8_t* packet = connection.incoming.data() + FrameHeaderSize;
        if (packet[0] != voxellancer::protocol::ProtocolVersion) {
            return false;
        }

        if (packet[1] == 5) {
            if (voxellancer::protocol::decodeSnapshotResyncRequest(
                    packet, packetSize) !=
                voxellancer::protocol::DecodeError::None) {
                return false;
            }
            const std::uint64_t tick = m_session.tickIndex();
            if (connection.hasResynced &&
                tick - connection.lastResyncTick < SnapshotIntervalTicks) {
                return false;
            }
            connection.hasResynced = true;
            connection.lastResyncTick = tick;
            connection.snapshotSequence = 0;
            connection.snapshotBaseline.clear();
            connection.equipmentBaseline.clear();
            if (!sendSnapshot(connection, ++m_snapshotSequence)) {
                return false;
            }
        } else {
            if (packetSize != voxellancer::protocol::InputCommandPacketSize) {
                return false;
            }
            voxellancer::protocol::InputCommand command;
            const voxellancer::protocol::DecodeError decodeResult =
                voxellancer::protocol::decodeInputCommand(
                    packet,
                    packetSize,
                    command
                );
            if (decodeResult != voxellancer::protocol::DecodeError::None) {
                return false;
            }
            const voxellancer::protocol::InputCommandResult result =
                m_session.submitInputCommand(connection.actor, command);
            if (result != voxellancer::protocol::InputCommandResult::Queued) {
                voxellancer::protocol::InputCommandAcknowledgementPacket acknowledgement{};
                if (!voxellancer::protocol::encodeInputCommandAcknowledgement(
                        {command.sequence, result}, acknowledgement) ||
                    !queuePacket(
                        connection,
                        acknowledgement.data(),
                        acknowledgement.size())) {
                    return false;
                }
            }
        }
        connection.incoming.erase(
            connection.incoming.begin(),
            connection.incoming.begin() + static_cast<std::ptrdiff_t>(frameSize));
    }
    return true;
}

bool TcpServer::sendPending(Connection& connection) {
    while (true) {
        if (connection.outgoing.empty() && !flushDeferred(connection)) {
            return false;
        }
        if (connection.outgoing.empty()) {
            return true;
        }
        std::vector<std::uint8_t>& packet = connection.outgoing.front();
        std::size_t sent = 0;
        const sf::Socket::Status status = connection.socket->send(
            packet.data() + connection.outgoingOffset,
            packet.size() - connection.outgoingOffset,
            sent
        );
        if (status == sf::Socket::Status::NotReady) {
            return true;
        }
        if (status == sf::Socket::Status::Disconnected ||
            status == sf::Socket::Status::Error) {
            return false;
        }
        if (sent == 0) {
            return true;
        }
        connection.bytesSent += sent;
        connection.outgoingOffset += sent;
        connection.outgoingBytes -= sent;
        if (connection.outgoingOffset == packet.size()) {
            connection.outgoing.pop_front();
            connection.outgoingOffset = 0;
        }
    }
}

bool TcpServer::flushDeferred(Connection& connection) {
    while (!connection.deferredOutgoing.empty()) {
        const std::vector<std::uint8_t>& data =
            connection.deferredOutgoing.front();
        if (connection.outgoingBytes + data.size() + FrameHeaderSize >
            MaxOutboundBytes) {
            return true;
        }

        std::vector<std::uint8_t> frame(data.size() + FrameHeaderSize);
        writeFrameLength(frame.data(), static_cast<std::uint32_t>(data.size()));
        std::copy(data.begin(), data.end(), frame.begin() + FrameHeaderSize);
        connection.outgoingBytes += frame.size();
        connection.outgoing.push_back(std::move(frame));
        connection.deferredOutgoingBytes -= data.size() + FrameHeaderSize;
        connection.deferredOutgoing.pop_front();
    }
    return true;
}

bool TcpServer::queuePacket(
    Connection& connection,
    const std::uint8_t* data,
    std::size_t size
) {
    if (size == 0 || size > MaxOutboundBytes - FrameHeaderSize) {
        return false;
    }
    if (!connection.deferredOutgoing.empty() ||
        connection.outgoingBytes + size + FrameHeaderSize > MaxOutboundBytes) {
        const std::size_t framedSize = size + FrameHeaderSize;
        if (framedSize > MaxDeferredOutboundBytes -
                connection.deferredOutgoingBytes) {
            return false;
        }
        connection.deferredOutgoing.emplace_back(data, data + size);
        connection.deferredOutgoingBytes += framedSize;
        return true;
    }
    std::vector<std::uint8_t> frame(size + FrameHeaderSize);
    writeFrameLength(frame.data(), static_cast<std::uint32_t>(size));
    std::copy(data, data + size, frame.begin() + FrameHeaderSize);
    connection.outgoingBytes += frame.size();
    connection.outgoing.push_back(std::move(frame));
    return true;
}

bool TcpServer::sendSnapshot(Connection& connection, std::uint64_t sequence) {
    WorldObject* actor = m_world.worldObject(connection.actor);
    std::vector<voxellancer::protocol::EntitySnapshot> current;
    voxellancer::protocol::SnapshotFrame frame;
    if (actor == nullptr || !m_session.createSnapshotState(current)) {
        return false;
    }

    const glm::vec3 center = actor->position();
    current.erase(
        std::remove_if(
            current.begin(),
            current.end(),
            [this, &connection, &center](const auto& snapshot) {
                if (snapshot.id == connection.actor) {
                    return false;
                }
                const glm::vec3 offset = snapshot.position - center;
                return glm::dot(offset, offset) > m_interestRadiusSquared;
            }
        ),
        current.end()
    );
    if (!voxellancer::protocol::buildSnapshotDelta(
            sequence,
            m_session.tickIndex(),
            connection.snapshotSequence,
            current,
            connection.snapshotBaseline,
            frame)) {
        return false;
    }

    std::vector<voxellancer::protocol::EntityId> newEntities;
    for (const auto& update : frame.updates) {
        const bool alreadyVisible = std::any_of(
            connection.snapshotBaseline.begin(),
            connection.snapshotBaseline.end(),
            [&update](const auto& existing) {
                return existing.id == update.id;
            });
        if (!alreadyVisible) {
            newEntities.push_back(update.id);
        }
    }

    std::vector<std::uint8_t> packet;
    if (!voxellancer::protocol::encodeSnapshotFrame(frame, packet) ||
        !queuePacket(connection, packet.data(), packet.size())) {
        return false;
    }
    for (const auto entity : newEntities) {
        if (!sendEntityContent(connection, entity)) {
            return false;
        }
    }
    if (!voxellancer::protocol::applySnapshotFrame(
            frame, connection.snapshotSequence, connection.snapshotBaseline)) {
        return false;
    }
    connection.snapshotSequence = sequence;
    for (const auto removed : frame.removals) {
        connection.equipmentBaseline.erase(entityIdKey(removed));
    }
    return sendEquipmentUpdates(connection, current);
}

void TcpServer::sendVoxelRemovalEvents(
    const std::vector<voxellancer::protocol::VoxelRemovalEvent>& events
) {
    for (std::size_t index = 0; index < m_connections.size();) {
        Connection& connection = m_connections[index];
        bool connectionFailed = false;
        for (const auto& event : events) {
            const bool interested = std::any_of(
                connection.snapshotBaseline.begin(),
                connection.snapshotBaseline.end(),
                [&event](const auto& snapshot) {
                    return snapshot.id == event.entity;
                });
            if (!interested) {
                continue;
            }

            voxellancer::protocol::VoxelRemovalPacket packet{};
            if (!voxellancer::protocol::encodeVoxelRemovalEvent(event, packet)) {
                throw std::runtime_error("Simulation produced an invalid voxel removal event.");
            }
            if (!queuePacket(connection, packet.data(), packet.size())) {
                connectionFailed = true;
                break;
            }
        }
        if (connectionFailed) {
            removeConnection(index);
        } else {
            ++index;
        }
    }
}

void TcpServer::sendVoxelDamageEvents(
    const std::vector<voxellancer::protocol::VoxelDamageEvent>& events
) {
    for (std::size_t index = 0; index < m_connections.size();) {
        Connection& connection = m_connections[index];
        bool connectionFailed = false;
        for (const auto& event : events) {
            const bool interested = std::any_of(
                connection.snapshotBaseline.begin(),
                connection.snapshotBaseline.end(),
                [&event](const auto& snapshot) {
                    return snapshot.id == event.entity;
                });
            if (!interested) {
                continue;
            }

            voxellancer::protocol::VoxelDamagePacket packet{};
            if (!voxellancer::protocol::encodeVoxelDamageEvent(event, packet)) {
                throw std::runtime_error("Simulation produced an invalid voxel damage event.");
            }
            if (!queuePacket(connection, packet.data(), packet.size())) {
                connectionFailed = true;
                break;
            }
        }
        if (connectionFailed) {
            removeConnection(index);
        } else {
            ++index;
        }
    }
}

void TcpServer::sendShieldHitEvents(
    const std::vector<voxellancer::protocol::ShieldHitEvent>& events
) {
    for (std::size_t index = 0; index < m_connections.size();) {
        Connection& connection = m_connections[index];
        bool connectionFailed = false;
        for (const auto& event : events) {
            const bool interested = std::any_of(
                connection.snapshotBaseline.begin(),
                connection.snapshotBaseline.end(),
                [&event](const auto& snapshot) {
                    return snapshot.id == event.entity;
                });
            if (!interested) {
                continue;
            }

            voxellancer::protocol::ShieldHitEventPacket packet{};
            if (!voxellancer::protocol::encodeShieldHitEvent(event, packet)) {
                throw std::runtime_error(
                    "Simulation produced an invalid shield-hit event.");
            }
            if (!queuePacket(connection, packet.data(), packet.size())) {
                connectionFailed = true;
                break;
            }
        }
        if (connectionFailed) {
            removeConnection(index);
        } else {
            ++index;
        }
    }
}

void TcpServer::sendMissionStateEvents(
    const std::vector<voxellancer::protocol::MissionStateEvent>& events
) {
    for (std::size_t index = 0; index < m_connections.size();) {
        Connection& connection = m_connections[index];
        bool connectionFailed = false;
        for (const auto& event : events) {
            voxellancer::protocol::MissionStateEventPacket packet{};
            if (!voxellancer::protocol::encodeMissionStateEvent(event, packet)) {
                throw std::runtime_error(
                    "Simulation produced an invalid mission-state event.");
            }
            if (!queuePacket(connection, packet.data(), packet.size())) {
                connectionFailed = true;
                break;
            }
        }
        if (connectionFailed) {
            removeConnection(index);
        } else {
            ++index;
        }
    }
}

void TcpServer::sendWeaponFiredEvents(
    const std::vector<voxellancer::protocol::WeaponFiredEvent>& events
) {
    for (std::size_t index = 0; index < m_connections.size();) {
        Connection& connection = m_connections[index];
        bool connectionFailed = false;
        for (const auto& event : events) {
            const bool interested = std::any_of(
                connection.snapshotBaseline.begin(),
                connection.snapshotBaseline.end(),
                [&event](const auto& snapshot) {
                    return snapshot.id == event.entity;
                });
            if (!interested) {
                continue;
            }

            voxellancer::protocol::WeaponFiredEventPacket packet{};
            if (!voxellancer::protocol::encodeWeaponFiredEvent(event, packet)) {
                throw std::runtime_error(
                    "Simulation produced an invalid weapon-fired event.");
            }
            if (!queuePacket(connection, packet.data(), packet.size())) {
                connectionFailed = true;
                break;
            }
        }
        if (connectionFailed) {
            removeConnection(index);
        } else {
            ++index;
        }
    }
}

bool TcpServer::sendEntityContent(
    Connection& connection,
    voxellancer::protocol::EntityId entity
) {
    WorldObject* worldObject = m_world.worldObject(entity);
    if (worldObject == nullptr) {
        return false;
    }

    voxellancer::protocol::EntityContentSnapshot content;
    content.id = entity;
    content.definitionKey = worldObject->info().definitionKey();
    content.voxels.reserve(worldObject->voxelMap().size());
    for (const auto& entry : worldObject->voxelMap()) {
        const Voxel* voxel = entry.second;
        if (voxel == nullptr) {
            throw std::runtime_error("World object contains a null voxel.");
        }
        content.voxels.push_back(voxellancer::protocol::EntityVoxelState{
            entry.first,
            voxel->visuals().color(),
            voxel->density(),
            voxel->hp(),
            voxel->maxHP()
        });
    }
    content.equipment = collectEquipmentState(worldObject->components());
    std::sort(
        content.voxels.begin(),
        content.voxels.end(),
        [](const auto& left, const auto& right) {
            if (left.cell.x != right.cell.x) {
                return left.cell.x < right.cell.x;
            }
            if (left.cell.y != right.cell.y) {
                return left.cell.y < right.cell.y;
            }
            return left.cell.z < right.cell.z;
        });
    std::vector<std::uint8_t> packet;
    if (!voxellancer::protocol::encodeEntityContent(content, packet)) {
        throw std::runtime_error(
            "World object exceeds the supported replicated content limits.");
    }
    if (!queuePacket(connection, packet.data(), packet.size())) {
        return false;
    }
    connection.equipmentBaseline[entityIdKey(entity)] =
        std::move(content.equipment);
    return true;
}

bool TcpServer::sendEquipmentUpdates(
    Connection& connection,
    const std::vector<voxellancer::protocol::EntitySnapshot>& entities
) {
    for (const auto& entity : entities) {
        const auto baseline = connection.equipmentBaseline.find(
            entityIdKey(entity.id));
        if (baseline == connection.equipmentBaseline.end()) {
            return false;
        }
        WorldObject* worldObject = m_world.worldObject(entity.id);
        if (worldObject == nullptr) {
            return false;
        }
        auto equipment = collectEquipmentState(worldObject->components());
        if (equipment == baseline->second) {
            continue;
        }

        voxellancer::protocol::EntityEquipmentSnapshot snapshot{
            entity.id,
            std::move(equipment)
        };
        std::vector<std::uint8_t> packet;
        if (!voxellancer::protocol::encodeEntityEquipmentSnapshot(
                snapshot, packet)) {
            throw std::runtime_error(
                "World object exceeds the supported equipment-state limits.");
        }
        if (!queuePacket(connection, packet.data(), packet.size())) {
            return false;
        }
        baseline->second = std::move(snapshot.equipment);
    }
    return true;
}

void TcpServer::broadcastSnapshot() {
    const std::uint64_t sequence = ++m_snapshotSequence;
    for (std::size_t index = 0; index < m_connections.size();) {
        if (!sendSnapshot(m_connections[index], sequence)) {
            removeConnection(index);
        } else {
            ++index;
        }
    }
}

void TcpServer::sendAcknowledgements() {
    const auto acknowledgements = m_session.drainInputCommandAcknowledgements();
    for (const SimulationSession::InputCommandAcknowledgement& acknowledgement :
        acknowledgements) {
        const auto connection = std::find_if(
            m_connections.begin(),
            m_connections.end(),
            [&acknowledgement](const Connection& candidate) {
                return candidate.actor == acknowledgement.actor;
            });
        if (connection == m_connections.end()) {
            continue;
        }
        voxellancer::protocol::InputCommandAcknowledgementPacket packet{};
        if (!voxellancer::protocol::encodeInputCommandAcknowledgement(
                {acknowledgement.sequence, acknowledgement.result}, packet) ||
            !queuePacket(*connection, packet.data(), packet.size())) {
            const std::size_t index =
                static_cast<std::size_t>(connection - m_connections.begin());
            removeConnection(index);
        }
    }
}

void TcpServer::removeConnection(std::size_t index) {
    Connection& connection = m_connections[index];
    std::cerr << "Client disconnected from local simulation.\n";
    connection.socket->disconnect();
    m_controlledActors.erase(entityIdKey(connection.actor));
    if (!m_session.requestActorRemoval(connection.actor)) {
        std::cerr << "Could not queue disconnected actor removal.\n";
    }
    m_connections.erase(m_connections.begin() + static_cast<std::ptrdiff_t>(index));
}

std::optional<voxellancer::protocol::EntityId> TcpServer::availableActor() const {
    if (m_preferredActor &&
        m_world.worldObject(*m_preferredActor) != nullptr &&
        m_controlledActors.find(entityIdKey(*m_preferredActor)) ==
            m_controlledActors.end()) {
        return m_preferredActor;
    }
    for (Ship* ship : m_world.ships()) {
        const auto id = m_world.entityId(ship);
        if (id && m_controlledActors.find(entityIdKey(*id)) == m_controlledActors.end()) {
            return id;
        }
    }
    return std::nullopt;
}
