#include "gameplayrunning.h"

#ifndef VOXELLANCER_HEADLESS_SERVER
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <glow/logging.h>

#include "client/tcpclient.h"
#include "property/property.h"
#include "equipment/enginepower.h"
#endif
#include "gamestate/game.h"
#include "gamestate/gameplay/gameplay.h"
#include "gamestate/gameplay/input/gameplaynormalinput.h"

#include "camera/camerahead.h"
#include "camera/cameradolly.h"
#include "world/world.h"
#include "player.h"
#include "worldobject/ship.h"
#include "sound/soundmanager.h"

#ifndef VOXELLANCER_HEADLESS_SERVER
namespace {
    constexpr std::size_t MaxPendingRemoteInputCommands = 128;
    constexpr std::uint64_t RemoteInputLeadTicks = 4;

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
        return commandTick > std::numeric_limits<std::uint64_t>::max() - 2
            ? std::numeric_limits<std::uint64_t>::max()
            : commandTick + 2;
    }
}
#endif


GamePlayRunning::GamePlayRunning(GamePlay* gamePlay, World& world):
    GameState("GamePlay Main", gamePlay),
    m_gamePlay(gamePlay),
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
    return m_remotePreview != nullptr;
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

std::vector<voxellancer::protocol::VoxelDamageEvent>
GamePlayRunning::drainRemoteVoxelDamageEvents() {
    return m_remotePreview
        ? m_remotePreview->drainVoxelDamageEvents()
        : std::vector<voxellancer::protocol::VoxelDamageEvent>();
}

std::vector<voxellancer::protocol::ShieldHitEvent>
GamePlayRunning::drainRemoteShieldHitEvents() {
    return m_remotePreview
        ? m_remotePreview->drainShieldHitEvents()
        : std::vector<voxellancer::protocol::ShieldHitEvent>();
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

void GamePlayRunning::submitRemoteInput(
    const glm::vec3& movement,
    const glm::vec3& aim,
    bool firePrimary
) {
    if (!m_remotePreview || !m_remotePreview->actor() ||
        m_remotePreview->snapshotSequence() == 0) {
        return;
    }
    if (m_remoteInputSequence == std::numeric_limits<std::uint64_t>::max()) {
        glow::warning("Remote input sequence exhausted; refusing further input.");
        return;
    }
    if (m_remoteInputCommands.size() >= MaxPendingRemoteInputCommands) {
        glow::warning("Remote input prediction queue reached its limit.");
        return;
    }

    voxellancer::protocol::InputCommand command;
    command.sequence = m_remoteInputSequence + 1;
    command.tick = inputTargetTick(m_remotePreview->snapshotTick());
    command.moveX = quantizeInputAxis(movement.x);
    command.moveY = quantizeInputAxis(movement.y);
    command.moveZ = quantizeInputAxis(movement.z);
    command.aimX = quantizeInputAxis(aim.x);
    command.aimY = quantizeInputAxis(aim.y);
    command.roll = quantizeInputAxis(aim.z);
    if (firePrimary) {
        command.buttons |= voxellancer::protocol::FirePrimary;
    }
    const bool inputChanged = !m_lastRemoteInputCommand ||
        command.moveX != m_lastRemoteInputCommand->moveX ||
        command.moveY != m_lastRemoteInputCommand->moveY ||
        command.moveZ != m_lastRemoteInputCommand->moveZ ||
        command.aimX != m_lastRemoteInputCommand->aimX ||
        command.aimY != m_lastRemoteInputCommand->aimY ||
        command.roll != m_lastRemoteInputCommand->roll ||
        command.buttons != m_lastRemoteInputCommand->buttons;
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
    m_pauseTrigger.update(deltaSec);

#ifndef VOXELLANCER_HEADLESS_SERVER
    if (!m_remotePreview) {
        m_simulationSession.advance(deltaSec);
    }
    if (m_remotePreview) {
        if (!m_remotePreview->poll()) {
            glow::warning(
                "Remote snapshot preview disconnected: %;",
                m_remotePreview->lastError()
            );
            m_remotePreview.reset();
            m_remotePreviewSnapshot.reset();
            m_remoteReplicaSnapshots.clear();
            m_remoteRenderTick.reset();
            return;
        }
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
        const auto actor = m_remotePreview->actor();
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
            const double earliestBufferedTick = static_cast<double>(
                tick > 3 ? tick - 3 : tick);
            if (!m_remoteRenderTick) {
                m_remoteRenderTick = earliestBufferedTick;
            } else {
                *m_remoteRenderTick += static_cast<double>(deltaSec) *
                    SimulationTickRunner::TickRate;
                *m_remoteRenderTick = std::max(
                    *m_remoteRenderTick, earliestBufferedTick);
            }
            for (const auto& snapshot : m_remotePreview->replicaState()) {
                const auto sampled = m_remotePreview->sample(
                    snapshot.id, *m_remoteRenderTick);
                if (sampled) {
                    m_remoteReplicaSnapshots.push_back(*sampled);
                }
            }
            const auto authoritativeActor = m_remotePreview->sample(
                *actor, static_cast<double>(tick));
            const auto actorContent = std::find_if(
                m_remotePreview->entityContents().begin(),
                m_remotePreview->entityContents().end(),
                [actor](const auto& content) {
                    return content.id == *actor;
                });
            if (authoritativeActor && actorContent !=
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
                const auto predicted = m_remoteInputPredictor.predict(
                    *authoritativeActor,
                    tick,
                    predictionTargetTick(tick),
                    commands,
                    m_remoteInputEnginePower,
                    Property<float>(
                        "physics.globalDirectionalDampening").get(),
                    Property<float>(
                        "physics.globalAngularDampening").get());
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
    m_gamePlay->soundManager().activate();
}

void GamePlayRunning::onLeft() {
    GameState::onLeft();
    m_gamePlay->soundManager().deactivate();
}
