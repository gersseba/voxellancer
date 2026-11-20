#include "gameplayrunning.h"

#include "benchmark/benchmarkprofiler.h"

#ifndef VOXELLANCER_HEADLESS_SERVER
#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

#include <glow/logging.h>

#include "client/tcpclient.h"
#include "property/property.h"
#include "equipment/enginepower.h"
#endif
#include "gamestate/game.h"
#include "gamestate/gameplay/gameplay.h"
#include "gamestate/gameplay/gameplayscene.h"
#include "gamestate/gameplay/input/gameplaynormalinput.h"

#include "camera/camerahead.h"
#include "camera/cameradolly.h"
#include "world/world.h"
#include "world/worldlogic.h"
#include "player.h"
#include "worldobject/ship.h"
#include "sound/soundmanager.h"

#ifndef VOXELLANCER_HEADLESS_SERVER
namespace {
    constexpr std::size_t MaxPendingRemoteInputCommands = 128;
    constexpr std::uint64_t RemoteInputLeadTicks = 2;
    constexpr std::uint64_t RemoteInputPredictionTicks = 1;
    constexpr double RemoteInterpolationDelaySeconds = 0.05;

    std::int16_t quantizeInputAxis(float value) {
        const float clamped = glm::clamp(value, -1.0f, 1.0f);
        const float scale = clamped < 0.0f ? 32768.0f : 32767.0f;
        return static_cast<std::int16_t>(std::lround(clamped * scale));
    }

    std::uint64_t inputTargetTick(std::uint64_t tick) {
        return tick > std::numeric_limits<std::uint64_t>::max() -
                RemoteInputLeadTicks
            ? std::numeric_limits<std::uint64_t>::max()
            : tick + RemoteInputLeadTicks;
    }

    std::uint64_t predictionTargetTick(std::uint64_t tick) {
        const std::uint64_t commandTick = inputTargetTick(tick);
        return commandTick > std::numeric_limits<std::uint64_t>::max() -
                RemoteInputPredictionTicks
            ? std::numeric_limits<std::uint64_t>::max()
            : commandTick + RemoteInputPredictionTicks;
    }
}
#endif


GamePlayRunning::GamePlayRunning(GamePlay* gamePlay, World& world):
    GameState("GamePlay Main", gamePlay),
    m_gamePlay(gamePlay),
    m_world(world),
    m_pauseTrigger(GLFW_KEY_P),
    m_simulationSession(world)
{

}

GamePlayRunning::~GamePlayRunning() = default;

Trigger& GamePlayRunning::pauseTrigger() {
    return m_pauseTrigger;
}

std::uint64_t GamePlayRunning::simulationTickIndex() const {
    return m_simulationSession.tickIndex();
}

double GamePlayRunning::simulationBacklogSeconds() const {
    return m_simulationSession.backlogSeconds();
}

#ifndef VOXELLANCER_HEADLESS_SERVER
bool GamePlayRunning::connectRemotePreview(std::uint16_t port) {
    if (m_remotePreview) {
        return false;
    }

    auto client = std::make_unique<TcpClient>();
    if (!client->connectLocal(port)) {
        glow::warning("Could not start remote snapshot preview: %;", client->lastError());
        return false;
    }
    return connectRemotePreview(std::move(client));
}

bool GamePlayRunning::connectRemotePreview(std::unique_ptr<TcpClient> client) {
    if (m_remotePreview || !client) {
        return false;
    }
    m_remotePreview = std::move(client);
    m_contractSessionEnded = false;
    m_submittedInvulnerability.reset();
    m_remoteConnectionError.clear();
    m_remoteRenderTick.reset();
    m_remoteInputSequence = 0;
    m_lastRemoteInputSnapshotSequence = 0;
    m_lastRemoteInputCommand.reset();
    m_remoteAppliedInputCommand.reset();
    m_remoteInputCommands.clear();
    m_remoteInputEquipment.clear();
    m_remoteInputEnginePower = EnginePower();
    return true;
}

bool GamePlayRunning::remotePreviewActive() const {
    return m_remotePreview != nullptr || m_contractSessionEnded;
}

std::optional<voxellancer::protocol::LobbyStatus>
GamePlayRunning::remoteLobbyStatus() const {
    return m_remotePreview ? m_remotePreview->lobbyStatus() : std::nullopt;
}

std::uint64_t GamePlayRunning::remotePreviewTick() const {
    return m_remotePreview ? m_remotePreview->snapshotTick() : 0;
}

const std::optional<voxellancer::protocol::EntitySnapshot>&
GamePlayRunning::remotePreviewSnapshot() const {
    return m_remotePreviewSnapshot;
}

const std::vector<voxellancer::protocol::EntitySnapshot>&
GamePlayRunning::remoteReplicaSnapshots() const {
    return m_remoteReplicaSnapshots;
}

const std::vector<voxellancer::protocol::EntityContentSnapshot>&
GamePlayRunning::remoteEntityContents() const {
    static const std::vector<voxellancer::protocol::EntityContentSnapshot> Empty;
    return m_remotePreview ? m_remotePreview->entityContents() : Empty;
}

std::vector<voxellancer::protocol::EntityId>
GamePlayRunning::drainRemoteEntityContentChanges() {
    return m_remotePreview
        ? m_remotePreview->drainEntityContentChanges()
        : std::vector<voxellancer::protocol::EntityId>();
}

std::vector<voxellancer::protocol::VoxelDamageEvent>
GamePlayRunning::drainRemoteVoxelDamageEvents() {
    return m_remotePreview
        ? m_remotePreview->drainVoxelDamageEvents()
        : std::vector<voxellancer::protocol::VoxelDamageEvent>();
}

std::vector<voxellancer::protocol::ShieldHitEvent>
GamePlayRunning::drainShieldHitEvents() {
    return m_remotePreview
        ? m_remotePreview->drainShieldHitEvents()
        : m_world.worldLogic().drainShieldHitEvents();
}

std::vector<voxellancer::protocol::MissionStateEvent>
GamePlayRunning::drainRemoteMissionStateEvents() {
    return m_remotePreview
        ? m_remotePreview->drainMissionStateEvents()
        : std::vector<voxellancer::protocol::MissionStateEvent>();
}

std::vector<voxellancer::protocol::WeaponFiredEvent>
GamePlayRunning::drainRemoteWeaponFiredEvents() {
    return m_remotePreview
        ? m_remotePreview->drainWeaponFiredEvents()
        : std::vector<voxellancer::protocol::WeaponFiredEvent>();
}

std::optional<voxellancer::protocol::ContractStatus> GamePlayRunning::contractStatus() const {
    return m_remotePreview ? m_remotePreview->contractStatus() : std::nullopt;
}

bool GamePlayRunning::contractSessionEnded() const {
    return m_contractSessionEnded;
}

bool GamePlayRunning::remoteWorldReady() const {
    const auto lobby = remoteLobbyStatus();
    return m_remotePreview && m_remotePreview->snapshotSequence() != 0 &&
        lobby && lobby->worldStarted;
}

const std::string& GamePlayRunning::remoteConnectionError() const {
    return m_remoteConnectionError;
}

bool GamePlayRunning::submitContractAction(voxellancer::protocol::ContractActionKind action,
    std::uint8_t weapon, glm::ivec3 cell, std::uint8_t node,
    voxellancer::protocol::RewardChoice choice, std::uint8_t shield) {
    if (!m_remotePreview || !m_remotePreview->sendContractAction(action, weapon, cell, node, choice, shield)) {
        glow::warning("Could not send station action: %;",
            m_remotePreview ? m_remotePreview->lastError() : "not connected");
        return false;
    }

    return true;
}

std::uint64_t GamePlayRunning::contractActionSequence() const {
    return m_remotePreview ? m_remotePreview->contractActionSequence() : 0;
}

void GamePlayRunning::submitRemoteInput(
    const glm::vec3& movement,
    const glm::vec3& aim,
    bool firePrimary
) {
    if (!m_remotePreview || !m_remotePreview->actor() ||
        m_remotePreview->snapshotSequence() == 0) {
        return;
    }
    const auto contract = contractStatus();
    if (contract && !contract->alive) return;
    if (m_remoteInputSequence == std::numeric_limits<std::uint64_t>::max()) {
        glow::warning("Remote input sequence exhausted; refusing further input.");
        return;
    }
    if (m_remoteInputCommands.size() >= MaxPendingRemoteInputCommands) {
        glow::warning("Remote input prediction queue reached its limit.");
        return;
    }

    voxellancer::protocol::InputCommand command;
    const bool docked = contract &&
        contract->phase != voxellancer::protocol::ContractPhase::Mission;
    command.sequence = m_remoteInputSequence + 1;
    command.tick = inputTargetTick(m_remotePreview->snapshotTick());
    command.moveX = quantizeInputAxis(docked ? 0.0f : movement.x);
    command.moveY = quantizeInputAxis(docked ? 0.0f : movement.y);
    command.moveZ = quantizeInputAxis(docked ? 0.0f : movement.z);
    command.aimX = quantizeInputAxis(docked ? 0.0f : aim.x);
    command.aimY = quantizeInputAxis(docked ? 0.0f : aim.y);
    command.roll = quantizeInputAxis(docked ? 0.0f : aim.z);
    if (firePrimary && !docked) {
        Ship* ship = World::instance()->player().ship();
        if (ship == nullptr) {
            glow::warning("Cannot aim remote fire without a presentation ship.");
            return;
        }
        command.firePoint = glm::inverse(ship->orientation()) *
            (World::instance()->player().aimPoint() - ship->position());
        const float distance = glm::length(command.firePoint);
        if (std::isfinite(distance) &&
            distance > voxellancer::protocol::MaxFirePointDistance) {
            command.firePoint *= voxellancer::protocol::MaxFirePointDistance / distance;
        }
        command.buttons |= voxellancer::protocol::FirePrimary;
        if (const auto target = m_gamePlay->scene().remoteLeadTarget()) {
            command.buttons |= voxellancer::protocol::FireWithLead;
            command.target = *target;
        }
    }
    const bool inputChanged = !m_lastRemoteInputCommand ||
        command.moveX != m_lastRemoteInputCommand->moveX ||
        command.moveY != m_lastRemoteInputCommand->moveY ||
        command.moveZ != m_lastRemoteInputCommand->moveZ ||
        command.aimX != m_lastRemoteInputCommand->aimX ||
        command.aimY != m_lastRemoteInputCommand->aimY ||
        command.roll != m_lastRemoteInputCommand->roll ||
        command.buttons != m_lastRemoteInputCommand->buttons ||
        command.target != m_lastRemoteInputCommand->target ||
        command.firePoint != m_lastRemoteInputCommand->firePoint;
    const bool fireStateChanged = m_lastRemoteInputCommand &&
        ((command.buttons ^ m_lastRemoteInputCommand->buttons) &
            voxellancer::protocol::FirePrimary) != 0;
    const bool snapshotChanged =
        m_remotePreview->snapshotSequence() != m_lastRemoteInputSnapshotSequence;
    if (!inputChanged && !snapshotChanged) {
        return;
    }
    if (m_lastRemoteInputCommand) {
        const std::uint64_t nextStateTick =
            m_lastRemoteInputCommand->tick + (fireStateChanged ? 1 : 0);
        command.tick = std::max(command.tick, nextStateTick);
    }

    if (!m_remotePreview->sendInput(command)) {
        glow::warning("Could not queue remote input: %;", m_remotePreview->lastError());
        return;
    }
    m_remoteInputSequence = command.sequence;
    m_remoteInputCommands.push_back(RemoteInputCommand{command, false});
    m_lastRemoteInputSnapshotSequence = m_remotePreview->snapshotSequence();
    m_lastRemoteInputCommand = command;
}
#endif

void GamePlayRunning::update(float deltaSec) {
    GameState::update(deltaSec);
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (!m_gamePlay->game()->mainMenuEnabled()) {
        m_pauseTrigger.update(deltaSec);
    }
#else
    m_pauseTrigger.update(deltaSec);
#endif

#ifndef VOXELLANCER_HEADLESS_SERVER
    if (!m_remotePreview && !m_contractSessionEnded &&
        !m_gamePlay->game()->mainMenuEnabled()) {
        m_simulationSession.advance(deltaSec);
    }
    if (m_remotePreview) {
        const std::uint64_t previousSnapshotSequence =
            m_remotePreview->snapshotSequence();
        bool pollSucceeded = false;
        {
            BenchmarkCpuScope scope(BenchmarkMetric::ClientNetworkPoll);
            pollSucceeded = m_remotePreview->poll();
        }
        const std::uint64_t currentSnapshotSequence =
            m_remotePreview->snapshotSequence();
        if (currentSnapshotSequence > previousSnapshotSequence) {
            BenchmarkProfiler::instance().addCounter(
                BenchmarkCounter::RemoteSnapshotsReceived,
                currentSnapshotSequence - previousSnapshotSequence);
        }
        if (!pollSucceeded) {
            glow::warning(
                "Remote snapshot preview disconnected: %;",
                m_remotePreview->lastError()
            );
            m_remoteConnectionError = m_remotePreview->lastError();
            m_contractSessionEnded = m_gamePlay->game()->mainMenuEnabled() ||
                m_remotePreview->contractStatus().has_value();
            m_remotePreview.reset();
            m_remotePreviewSnapshot.reset();
            m_remoteReplicaSnapshots.clear();
            m_remoteRenderTick.reset();
            return;
        }
        if (const auto contract = m_remotePreview->contractStatus()) {
            const bool protection = Property<bool>("general.playerShipUndestroyable");
            if (!m_submittedInvulnerability && contract->invulnerable == protection) {
                m_submittedInvulnerability = protection;
            } else if (!m_submittedInvulnerability ||
                *m_submittedInvulnerability != protection) {
                using namespace voxellancer::protocol;
                if (submitContractAction(protection
                        ? ContractActionKind::EnableInvulnerability
                        : ContractActionKind::DisableInvulnerability)) {
                    m_submittedInvulnerability = protection;
                }
            }
        }
        static_cast<void>(m_remotePreview->drainVoxelRemovalEvents());
        for (const auto& acknowledgement : m_remotePreview->drainAcknowledgements()) {
            const auto pending = std::find_if(
                m_remoteInputCommands.begin(),
                m_remoteInputCommands.end(),
                [&acknowledgement](const RemoteInputCommand& candidate) {
                    return candidate.command.sequence == acknowledgement.sequence;
                });
            if (pending != m_remoteInputCommands.end()) {
                if (acknowledgement.result ==
                    voxellancer::protocol::InputCommandResult::Accepted) {
                    pending->accepted = true;
                } else {
                    m_remoteInputCommands.erase(pending);
                }
            }
            if (acknowledgement.result !=
                voxellancer::protocol::InputCommandResult::Accepted) {
                glow::warning(
                    "Remote input command %; was rejected with result %;.",
                    acknowledgement.sequence,
                    static_cast<int>(acknowledgement.result)
                );
            }
        }
        const auto contract = contractStatus();
        const auto actor = contract
            ? std::optional<voxellancer::protocol::EntityId>(contract->viewActor)
            : m_remotePreview->actor();
        if (actor != m_remoteViewActor) {
            m_remoteViewActor = actor;
            m_remoteInputCommands.clear();
            m_remoteAppliedInputCommand.reset();
            m_lastRemoteInputCommand.reset();
            m_remoteInputEquipment.clear();
            m_remoteInputEnginePower = EnginePower();
            m_remotePreviewSnapshot.reset();
            m_remoteRenderTick.reset();
        }
        m_remoteReplicaSnapshots.clear();
        if (actor && m_remotePreview->snapshotSequence() != 0) {
            const std::uint64_t tick = m_remotePreview->snapshotTick();
            for (auto command = m_remoteInputCommands.begin();
                 command != m_remoteInputCommands.end();) {
                if (command->accepted && command->command.tick < tick) {
                    if (!m_remoteAppliedInputCommand ||
                        command->command.tick >
                            m_remoteAppliedInputCommand->tick ||
                        (command->command.tick ==
                            m_remoteAppliedInputCommand->tick &&
                            command->command.sequence >
                                m_remoteAppliedInputCommand->sequence)) {
                        m_remoteAppliedInputCommand = command->command;
                    }
                    command = m_remoteInputCommands.erase(command);
                } else {
                    ++command;
                }
            }
            const double interpolationDelayTicks =
                RemoteInterpolationDelaySeconds *
                SimulationTickRunner::TickRate;
            const double earliestBufferedTick = std::max(
                0.0,
                static_cast<double>(tick) - interpolationDelayTicks);
            if (!m_remoteRenderTick) {
                m_remoteRenderTick = earliestBufferedTick;
            } else {
                if (m_remotePreview->interpolationReady()) {
                    *m_remoteRenderTick += static_cast<double>(deltaSec) *
                        SimulationTickRunner::TickRate;
                }
                *m_remoteRenderTick = std::max(
                    *m_remoteRenderTick, earliestBufferedTick);
            }
            std::optional<voxellancer::protocol::EntitySnapshot> authoritativeActor;
            {
                BenchmarkCpuScope scope(
                    BenchmarkMetric::ClientSnapshotSampling);
                for (const auto& snapshot : m_remotePreview->replicaState()) {
                    const auto sampled = m_remotePreview->sample(
                        snapshot.id, *m_remoteRenderTick);
                    if (sampled) {
                        m_remoteReplicaSnapshots.push_back(*sampled);
                    }
                }
                authoritativeActor = m_remotePreview->sample(
                    *actor, static_cast<double>(tick));
            }
            const auto actorContent = std::find_if(
                m_remotePreview->entityContents().begin(),
                m_remotePreview->entityContents().end(),
                [actor](const auto& content) {
                    return content.id == *actor;
                });
            if ((!contract || contract->alive) && authoritativeActor && actorContent !=
                    m_remotePreview->entityContents().end()) {
                if (actorContent->equipment != m_remoteInputEquipment) {
                    m_remoteInputEquipment = actorContent->equipment;
                    m_remoteInputEnginePower = EnginePower();
                    for (const auto& equipment : m_remoteInputEquipment) {
                        if (equipment.kind ==
                            voxellancer::protocol::EquipmentSlotKind::Engine) {
                            m_remoteInputEnginePower += EnginePower::fromProperties(
                                equipment.equipmentKey + ".general");
                        }
                    }
                }

                std::vector<voxellancer::protocol::InputCommand> commands;
                commands.reserve(
                    m_remoteInputCommands.size() +
                    (m_remoteAppliedInputCommand ? 1 : 0));
                if (m_remoteAppliedInputCommand) {
                    commands.push_back(*m_remoteAppliedInputCommand);
                }
                for (const RemoteInputCommand& command :
                     m_remoteInputCommands) {
                    commands.push_back(command.command);
                }
                std::optional<voxellancer::protocol::EntitySnapshot> predicted;
                {
                    BenchmarkCpuScope scope(
                        BenchmarkMetric::ClientInputPrediction);
                    predicted = m_remoteInputPredictor.predict(
                        *authoritativeActor,
                        tick,
                        predictionTargetTick(tick),
                        commands,
                        m_remoteInputEnginePower,
                        Property<float>(
                            "physics.globalDirectionalDampening").get(),
                        Property<float>(
                            "physics.globalAngularDampening").get());
                }
                if (predicted) {
                    const auto actorSnapshot = std::find_if(
                        m_remoteReplicaSnapshots.begin(),
                        m_remoteReplicaSnapshots.end(),
                        [actor](const auto& snapshot) {
                            return snapshot.id == *actor;
                        });
                    if (actorSnapshot == m_remoteReplicaSnapshots.end()) {
                        m_remoteReplicaSnapshots.push_back(*predicted);
                    } else {
                        *actorSnapshot = *predicted;
                    }
                }
            }
            const auto actorSnapshot = std::find_if(
                m_remoteReplicaSnapshots.begin(),
                m_remoteReplicaSnapshots.end(),
                [actor](const auto& snapshot) {
                    return snapshot.id == *actor;
                });
            m_remotePreviewSnapshot = actorSnapshot == m_remoteReplicaSnapshots.end()
                ? (authoritativeActor
                    ? std::optional<voxellancer::protocol::EntitySnapshot>(
                        *authoritativeActor)
                    : std::nullopt)
                : std::optional<voxellancer::protocol::EntitySnapshot>(*actorSnapshot);

            if (m_remotePreviewSnapshot) {
                Ship* cameraShip = World::instance()->player().ship();
                if (cameraShip != nullptr) {
                    cameraShip->transform().setPosition(
                        m_remotePreviewSnapshot->position);
                    cameraShip->transform().setOrientation(
                        m_remotePreviewSnapshot->orientation);
                }
            }
        } else {
            m_remotePreviewSnapshot.reset();
            m_remoteRenderTick.reset();
        }
        World::instance()->player().update(deltaSec);
    }
#else
    m_simulationSession.advance(deltaSec);
#endif
}

void GamePlayRunning::onEntered() {
    GameState::onEntered();
#ifndef VOXELLANCER_HEADLESS_SERVER
    m_world.worldLogic().setShieldHitEventReplicationEnabled(
        !m_remotePreview);
#endif
    m_gamePlay->soundManager().activate();
}

void GamePlayRunning::onLeft() {
    GameState::onLeft();
#ifndef VOXELLANCER_HEADLESS_SERVER
    m_world.worldLogic().setShieldHitEventReplicationEnabled(false);
#endif
    m_gamePlay->soundManager().deactivate();
}
