#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include <SFML/Network/TcpSocket.hpp>

#include "client/snapshotinterpolator.h"
#include "protocol/serverprotocol.h"

class TcpClient {
public:
    bool connectLocal(std::uint16_t port);
    void disconnect();
    bool poll();
    bool sendInput(const voxellancer::protocol::InputCommand& command);
    bool sendContractAction(voxellancer::protocol::ContractActionKind action,
        std::uint8_t weapon = 0, glm::ivec3 cell = glm::ivec3(0),
        std::uint8_t node = voxellancer::protocol::NoRouteNode,
        voxellancer::protocol::RewardChoice choice = voxellancer::protocol::RewardChoice::None,
        std::uint8_t shield = 0);
    std::optional<voxellancer::protocol::ContractStatus> contractStatus() const;
    std::uint64_t contractActionSequence() const { return m_contractActionSequence; }
    bool requestSnapshotResync();

    std::optional<voxellancer::protocol::EntityId> actor() const;
    std::optional<voxellancer::protocol::LobbyStatus> lobbyStatus() const;
    std::uint64_t snapshotSequence() const;
    std::uint64_t snapshotTick() const;
    bool interpolationReady() const;
    const std::vector<voxellancer::protocol::EntitySnapshot>& replicaState() const;
    const std::vector<voxellancer::protocol::EntityContentSnapshot>&
    entityContents() const;
    std::vector<voxellancer::protocol::EntityId> drainEntityContentChanges();
    std::optional<voxellancer::protocol::EntitySnapshot> sample(
        voxellancer::protocol::EntityId entity,
        double renderTick
    ) const;
    std::vector<voxellancer::protocol::InputCommandAcknowledgement>
    drainAcknowledgements();
    std::vector<voxellancer::protocol::VoxelRemovalEvent>
    drainVoxelRemovalEvents();
    std::vector<voxellancer::protocol::VoxelDamageEvent>
    drainVoxelDamageEvents();
    std::vector<voxellancer::protocol::ShieldHitEvent>
    drainShieldHitEvents();
    std::vector<voxellancer::protocol::MissionStateEvent>
    drainMissionStateEvents();
    std::vector<voxellancer::protocol::WeaponFiredEvent>
    drainWeaponFiredEvents();
    const std::string& lastError() const;

private:
    static constexpr std::size_t MaxFrameSize = 1024 * 1024;
    static constexpr std::size_t MaxPendingOutputBytes = 64 * 1024;
    static constexpr std::size_t MaxAcknowledgements = 1024;
    static constexpr std::size_t MaxVoxelRemovalEvents = 4096;
    static constexpr std::size_t MaxVoxelDamageEvents = 4096;
    static constexpr std::size_t MaxShieldHitEvents = 4096;
    static constexpr std::size_t MaxMissionStateEvents = 256;
    static constexpr std::size_t MaxWeaponFiredEvents = 4096;
    static constexpr std::size_t MaxReplicaVoxelStates = 65536;
    static constexpr std::size_t MaxReplicaEquipmentStates = 16384;

    sf::TcpSocket m_socket;
    bool m_connected = false;
    bool m_resyncPending = false;
    std::optional<voxellancer::protocol::EntityId> m_actor;
    std::optional<voxellancer::protocol::LobbyStatus> m_lobbyStatus;
    std::optional<voxellancer::protocol::ContractStatus> m_contractStatus;
    std::uint64_t m_contractActionSequence = 0;
    std::uint64_t m_snapshotSequence = 0;
    std::uint64_t m_snapshotTick = 0;
    std::vector<voxellancer::protocol::EntitySnapshot> m_replicaState;
    std::vector<voxellancer::protocol::EntityContentSnapshot> m_entityContents;
    std::vector<voxellancer::protocol::EntityId> m_changedEntityContents;
    std::unordered_set<std::uint64_t> m_changedEntityContentKeys;
    std::size_t m_replicaVoxelStateCount = 0;
    std::size_t m_replicaEquipmentStateCount = 0;
    SnapshotInterpolator m_snapshotInterpolator;
    std::vector<voxellancer::protocol::InputCommandAcknowledgement> m_acknowledgements;
    std::vector<voxellancer::protocol::VoxelRemovalEvent> m_voxelRemovalEvents;
    std::vector<voxellancer::protocol::VoxelDamageEvent> m_voxelDamageEvents;
    std::vector<voxellancer::protocol::ShieldHitEvent> m_shieldHitEvents;
    std::vector<voxellancer::protocol::MissionStateEvent> m_missionStateEvents;
    std::vector<voxellancer::protocol::WeaponFiredEvent> m_weaponFiredEvents;
    std::vector<std::uint8_t> m_incoming;
    std::deque<std::vector<std::uint8_t>> m_outgoing;
    std::size_t m_outgoingOffset = 0;
    std::size_t m_outgoingBytes = 0;
    std::string m_lastError;

    void markEntityContentChanged(voxellancer::protocol::EntityId entity);
    bool receiveFrames();
    bool processFrames();
    bool flushOutput();
    bool queuePacket(const std::uint8_t* data, std::size_t size);
    bool fail(const char* message);
};
