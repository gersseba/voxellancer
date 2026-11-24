#pragma once

#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_map>
#include <vector>

#include "protocol/serverprotocol.h"
#include "utils/simulationtickrunner.h"

class World;

class SimulationSession {
public:
    struct InputCommandAcknowledgement {
        voxellancer::protocol::EntityId actor;
        std::uint64_t sequence = 0;
        voxellancer::protocol::InputCommandResult result =
            voxellancer::protocol::InputCommandResult::Invalid;
    };

    explicit SimulationSession(
        World& world,
        std::uint32_t maxTicksPerAdvance = SimulationTickRunner::DefaultMaxTicksPerAdvance
    );

    std::uint32_t advance(double elapsedSeconds);
    void setPlayerInputEnabled(bool enabled);
    voxellancer::protocol::InputCommandResult submitInputCommand(
        voxellancer::protocol::EntityId actor,
        const voxellancer::protocol::InputCommand& command
    );
    bool requestActorRemoval(voxellancer::protocol::EntityId actor);
    bool actorRemovalPending(voxellancer::protocol::EntityId actor) const;
    std::vector<InputCommandAcknowledgement> drainInputCommandAcknowledgements();
    std::vector<voxellancer::protocol::VoxelRemovalEvent>
    drainVoxelRemovalEvents();
    std::vector<voxellancer::protocol::VoxelDamageEvent>
    drainVoxelDamageEvents();
    bool createSnapshotState(
        std::vector<voxellancer::protocol::EntitySnapshot>& state
    ) const;
    bool createSnapshotFrame(
        std::uint64_t sequence,
        std::uint64_t baselineSequence,
        const std::vector<voxellancer::protocol::EntitySnapshot>& baseline,
        voxellancer::protocol::SnapshotFrame& frame
    ) const;
    std::uint64_t tickIndex() const;
    double backlogSeconds() const;

private:
    struct ActorInput {
        voxellancer::protocol::EntityId actor;
        voxellancer::protocol::InputCommandBuffer commands;
    };

    struct PendingInput {
        voxellancer::protocol::EntityId actor;
        voxellancer::protocol::InputCommand command;
    };

    static constexpr std::size_t MaxPendingInputCommands = 1024;
    static constexpr std::size_t MaxPendingActorRemovals = 64;

    World& m_world;
    SimulationTickRunner m_tickRunner;
    std::unordered_map<std::uint64_t, ActorInput> m_actorInputs;
    std::mutex mutable m_inputQueueMutex;
    std::deque<PendingInput> m_pendingInputCommands;
    std::deque<voxellancer::protocol::EntityId> m_pendingActorRemovals;
    std::vector<InputCommandAcknowledgement> m_inputAcknowledgements;
    std::vector<voxellancer::protocol::VoxelRemovalEvent> m_voxelRemovalEvents;
    std::vector<voxellancer::protocol::VoxelDamageEvent> m_voxelDamageEvents;
    std::size_t m_processingInputCommandCount = 0;
    bool m_playerInputEnabled = true;

    void applyInputCommands(std::uint64_t tick);
};
