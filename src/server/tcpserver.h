#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <SFML/Network/TcpListener.hpp>
#include <SFML/Network/TcpSocket.hpp>

#include "protocol/serverprotocol.h"

class SimulationSession;
class World;
class ContractRun;

class TcpServer {
public:
    static constexpr std::size_t MaxClients = 16;

    struct ClientStatistics {
        voxellancer::protocol::EntityId actor;
        std::uint64_t bytesSent = 0;
        std::size_t visibleEntityCount = 0;
    };

    TcpServer(
        World& world,
        SimulationSession& session,
        std::uint16_t port,
        float interestRadius = 650.0f,
        std::optional<voxellancer::protocol::EntityId> preferredActor =
            std::nullopt,
        std::size_t targetPlayers = 0
    );
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;

    std::uint16_t port() const;
    std::size_t clientCount() const;
    bool worldStarted() const;
    void startWorldForPlayers(
        const std::vector<voxellancer::protocol::EntityId>& actors
    );
    std::vector<ClientStatistics> clientStatistics() const;
    void poll();
    void enableContracts(const std::vector<voxellancer::protocol::EntityId>& players);

private:
    struct Connection {
        std::unique_ptr<sf::TcpSocket> socket;
        voxellancer::protocol::EntityId actor;
        std::vector<std::uint8_t> incoming;
        std::deque<std::vector<std::uint8_t>> outgoing;
        std::size_t outgoingOffset = 0;
        std::size_t outgoingBytes = 0;
        std::deque<std::vector<std::uint8_t>> deferredOutgoing;
        std::size_t deferredOutgoingBytes = 0;
        std::uint64_t snapshotSequence = 0;
        std::uint64_t lastResyncTick = 0;
        std::uint64_t bytesSent = 0;
        bool hasResynced = false;
        std::optional<voxellancer::protocol::ContractStatusPacket> contractStatus;
        std::vector<voxellancer::protocol::EntitySnapshot> snapshotBaseline;
        std::vector<voxellancer::protocol::EntityId> contentRefresh;
        std::unordered_map<
            std::uint64_t,
            std::vector<voxellancer::protocol::EntityEquipmentState>
        > equipmentBaseline;
    };

    static constexpr std::size_t MaxIncomingBytes = 4096;
    static constexpr std::size_t MaxOutboundBytes = 1024 * 1024;
    static constexpr std::size_t MaxDeferredOutboundBytes = 16 * 1024 * 1024;
    static constexpr std::uint64_t SnapshotIntervalTicks = 2;

    World& m_world;
    SimulationSession& m_session;
    sf::TcpListener m_listener;
    std::uint16_t m_port;
    float m_interestRadiusSquared;
    std::optional<voxellancer::protocol::EntityId> m_preferredActor;
    std::size_t m_targetPlayers;
    bool m_worldStarted = true;
    std::size_t m_lastLobbyConnected = static_cast<std::size_t>(-1);
    bool m_lastLobbyStarted = false;
    std::uint64_t m_snapshotSequence = 0;
    std::uint64_t m_nextSnapshotTick = SnapshotIntervalTicks;
    std::unordered_set<std::uint64_t> m_controlledActors;
    std::vector<Connection> m_connections;
    std::unique_ptr<ContractRun> m_contracts;

    bool acceptConnections();
    bool receiveInput(Connection& connection);
    bool processInputFrames(Connection& connection);
    bool sendPending(Connection& connection);
    bool flushDeferred(Connection& connection);
    bool queuePacket(Connection& connection, const std::uint8_t* data, std::size_t size);
    bool sendSnapshot(Connection& connection, std::uint64_t sequence);
    bool sendSnapshot(
        Connection& connection,
        std::uint64_t sequence,
        const std::vector<voxellancer::protocol::EntitySnapshot>& worldState
    );
    bool sendEntityContent(
        Connection& connection,
        voxellancer::protocol::EntityId entity
    );
    bool sendEquipmentUpdates(
        Connection& connection,
        const std::vector<voxellancer::protocol::EntitySnapshot>& entities
    );
    void sendVoxelRemovalEvents(
        const std::vector<voxellancer::protocol::VoxelRemovalEvent>& events
    );
    void sendVoxelDamageEvents(
        const std::vector<voxellancer::protocol::VoxelDamageEvent>& events
    );
    void sendShieldHitEvents(
        const std::vector<voxellancer::protocol::ShieldHitEvent>& events
    );
    void sendMissionStateEvents(
        const std::vector<voxellancer::protocol::MissionStateEvent>& events
    );
    void sendWeaponFiredEvents(
        const std::vector<voxellancer::protocol::WeaponFiredEvent>& events
    );
    void broadcastSnapshot();
    void broadcastLobbyStatus();
    void sendContractStatus();
    void sendAcknowledgements();
    void removeConnection(std::size_t index, const char* reason);
    std::optional<voxellancer::protocol::EntityId> availableActor() const;
};
