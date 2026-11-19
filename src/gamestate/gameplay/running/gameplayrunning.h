#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "gamestate/gamestate.h"

#include "utils/statemachine/keytrigger.h"
#include "protocol/serverprotocol.h"
#include "simulation/simulationsession.h"
#ifndef VOXELLANCER_HEADLESS_SERVER
#include "client/inputpredictor.h"
#endif


class GamePlay;
class GamePlayNormalInput;
class World;
#ifndef VOXELLANCER_HEADLESS_SERVER
class TcpClient;
#endif

class GamePlayRunning: public GameState {
public:
    GamePlayRunning(GamePlay* gamePlay, World& world);
    ~GamePlayRunning();

    Trigger& pauseTrigger();
    std::uint64_t simulationTickIndex() const;
    double simulationBacklogSeconds() const;
#ifndef VOXELLANCER_HEADLESS_SERVER
    bool connectRemotePreview(std::uint16_t port);
    bool connectRemotePreview(std::unique_ptr<TcpClient> client);
    bool remotePreviewActive() const;
    std::optional<voxellancer::protocol::LobbyStatus> remoteLobbyStatus() const;
    std::uint64_t remotePreviewTick() const;
    void submitRemoteInput(
        const glm::vec3& movement,
        const glm::vec3& aim,
        bool firePrimary
    );
    const std::optional<voxellancer::protocol::EntitySnapshot>&
    remotePreviewSnapshot() const;
    const std::vector<voxellancer::protocol::EntitySnapshot>&
    remoteReplicaSnapshots() const;
    const std::vector<voxellancer::protocol::EntityContentSnapshot>&
    remoteEntityContents() const;
    std::vector<voxellancer::protocol::EntityId>
    drainRemoteEntityContentChanges();
    std::vector<voxellancer::protocol::VoxelDamageEvent>
    drainRemoteVoxelDamageEvents();
    std::vector<voxellancer::protocol::ShieldHitEvent>
    drainShieldHitEvents();
    std::vector<voxellancer::protocol::MissionStateEvent>
    drainRemoteMissionStateEvents();
    std::vector<voxellancer::protocol::WeaponFiredEvent>
    drainRemoteWeaponFiredEvents();
#endif

    virtual void update(float deltaSec) override;

    virtual void onEntered() override;
    virtual void onLeft() override;


protected:
    GamePlay* m_gamePlay;
    World& m_world;
    KeyTrigger m_pauseTrigger;
    SimulationSession m_simulationSession;
#ifndef VOXELLANCER_HEADLESS_SERVER
    struct RemoteInputCommand {
        voxellancer::protocol::InputCommand command;
        bool accepted = false;
    };

    std::unique_ptr<TcpClient> m_remotePreview;
    std::optional<voxellancer::protocol::EntitySnapshot> m_remotePreviewSnapshot;
    std::vector<voxellancer::protocol::EntitySnapshot> m_remoteReplicaSnapshots;
    InputPredictor m_remoteInputPredictor;
    std::optional<double> m_remoteRenderTick;
    std::uint64_t m_remoteInputSequence = 0;
    std::uint64_t m_lastRemoteInputSnapshotSequence = 0;
    std::optional<voxellancer::protocol::InputCommand> m_lastRemoteInputCommand;
    std::optional<voxellancer::protocol::InputCommand> m_remoteAppliedInputCommand;
    std::vector<RemoteInputCommand> m_remoteInputCommands;
    std::vector<voxellancer::protocol::EntityEquipmentState>
        m_remoteInputEquipment;
    EnginePower m_remoteInputEnginePower;
#endif

    std::unique_ptr<GamePlayNormalInput> m_input;
};
