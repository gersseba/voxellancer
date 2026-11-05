#include "simulationsession.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <glm/glm.hpp>

#include "equipment/enginestate.h"
#include "equipment/shield.h"
#include "equipment/shieldslot.h"
#include "physics/physics.h"
#include "voxel/voxel.h"
#include "world/god.h"
#include "world/world.h"
#include "world/worldlogic.h"
#include "worldobject/ship.h"
#include "worldobject/worldobject.h"
#include "worldobject/worldobjectcomponents.h"

namespace {
    using voxellancer::protocol::EntityId;

    std::uint64_t entityIdKey(EntityId id) {
        return (static_cast<std::uint64_t>(id.generation) << 32) | id.index;
    }

    float normalizedAxis(std::int16_t value) {
        const float divisor = value < 0 ? 32768.0f : 32767.0f;
        return glm::clamp(static_cast<float>(value) / divisor, -1.0f, 1.0f);
    }

    voxellancer::protocol::EntitySnapshot::Kind entityKind(
        const WorldObject& worldObject
    ) {
        using Kind = voxellancer::protocol::EntitySnapshot::Kind;
        switch (worldObject.objectType()) {
        case WorldObjectType::Ship:
            return Kind::Ship;
        case WorldObjectType::Bullet:
            return Kind::Bullet;
        case WorldObjectType::Rocket:
            return Kind::Rocket;
        default:
            return Kind::Other;
        }
    }

    float shieldFraction(WorldObject& worldObject) {
        float current = 0.0f;
        float maximum = 0.0f;
        for (const std::shared_ptr<ShieldSlot>& slot :
            worldObject.components().shieldSlots()) {
            if (slot && slot->shield()) {
                current += slot->shield()->hp();
                maximum += slot->shield()->maxHP();
            }
        }
        if (!std::isfinite(current) || !std::isfinite(maximum)) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return maximum > 0.0f
            ? glm::clamp(current / maximum, 0.0f, 1.0f)
            : 0.0f;
    }

    float hullFraction(WorldObject& worldObject) {
        float current = 0.0f;
        float maximum = 0.0f;
        for (const auto& entry : worldObject.voxelMap()) {
            const Voxel* voxel = entry.second;
            if (voxel == nullptr) {
                return std::numeric_limits<float>::quiet_NaN();
            }
            current += voxel->hp();
            maximum += voxel->maxHP();
        }
        if (!std::isfinite(current) || !std::isfinite(maximum)) {
            return std::numeric_limits<float>::quiet_NaN();
        }
        return maximum > 0.0f
            ? glm::clamp(current / maximum, 0.0f, 1.0f)
            : 0.0f;
    }
}

SimulationSession::SimulationSession(World& world, std::uint32_t maxTicksPerAdvance) :
    m_world(world),
    m_tickRunner(maxTicksPerAdvance)
{
}

std::uint32_t SimulationSession::advance(double elapsedSeconds) {
    return m_tickRunner.advance(elapsedSeconds,
        [this](float tickDurationSeconds, std::uint64_t tick) {
            applyInputCommands(tick);
            m_world.update(tickDurationSeconds);
            std::vector<voxellancer::protocol::VoxelRemovalEvent> events =
                m_world.worldLogic().drainVoxelRemovalEvents();
            m_voxelRemovalEvents.insert(
                m_voxelRemovalEvents.end(), events.begin(), events.end());
            std::vector<voxellancer::protocol::VoxelDamageEvent> damageEvents =
                m_world.worldLogic().drainVoxelDamageEvents();
            for (const auto& damageEvent : damageEvents) {
                const auto existing = std::find_if(
                    m_voxelDamageEvents.begin(),
                    m_voxelDamageEvents.end(),
                    [&damageEvent](const auto& candidate) {
                        return candidate.entity == damageEvent.entity &&
                            candidate.cell == damageEvent.cell;
                    });
                if (existing == m_voxelDamageEvents.end()) {
                    m_voxelDamageEvents.push_back(damageEvent);
                } else {
                    existing->hp = damageEvent.hp;
                }
            }
        });
}

voxellancer::protocol::InputCommandResult SimulationSession::submitInputCommand(
    voxellancer::protocol::EntityId actor,
    const voxellancer::protocol::InputCommand& command
) {
    using namespace voxellancer::protocol;
    InputCommandPacket encoded;
    if (!actor.valid() || !encodeInputCommand(command, encoded)) {
        return InputCommandResult::Invalid;
    }

    std::lock_guard<std::mutex> lock(m_inputQueueMutex);
    if (m_pendingInputCommands.size() + m_inputAcknowledgements.size() +
            m_processingInputCommandCount >= MaxPendingInputCommands) {
        return InputCommandResult::QueueFull;
    }
    m_pendingInputCommands.push_back(PendingInput{actor, command});
    return InputCommandResult::Queued;
}

std::vector<SimulationSession::InputCommandAcknowledgement>
SimulationSession::drainInputCommandAcknowledgements() {
    std::lock_guard<std::mutex> lock(m_inputQueueMutex);
    std::vector<InputCommandAcknowledgement> acknowledgements;
    acknowledgements.swap(m_inputAcknowledgements);
    return acknowledgements;
}

std::vector<voxellancer::protocol::VoxelRemovalEvent>
SimulationSession::drainVoxelRemovalEvents() {
    std::vector<voxellancer::protocol::VoxelRemovalEvent> events;
    events.swap(m_voxelRemovalEvents);
    return events;
}

std::vector<voxellancer::protocol::VoxelDamageEvent>
SimulationSession::drainVoxelDamageEvents() {
    std::vector<voxellancer::protocol::VoxelDamageEvent> events;
    events.swap(m_voxelDamageEvents);
    return events;
}

bool SimulationSession::requestActorRemoval(
    voxellancer::protocol::EntityId actor
) {
    if (!actor.valid()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_inputQueueMutex);
    if (m_pendingActorRemovals.size() >= MaxPendingActorRemovals) {
        return false;
    }
    m_pendingActorRemovals.push_back(actor);
    return true;
}

bool SimulationSession::createSnapshotFrame(
    std::uint64_t sequence,
    std::uint64_t baselineSequence,
    const std::vector<voxellancer::protocol::EntitySnapshot>& baseline,
    voxellancer::protocol::SnapshotFrame& frame
) const {
    std::vector<voxellancer::protocol::EntitySnapshot> current;
    if (!createSnapshotState(current)) {
        return false;
    }
    return voxellancer::protocol::buildSnapshotDelta(
        sequence,
        tickIndex(),
        baselineSequence,
        current,
        baseline,
        frame
    );
}

bool SimulationSession::createSnapshotState(
    std::vector<voxellancer::protocol::EntitySnapshot>& state
) const {
    std::vector<voxellancer::protocol::EntitySnapshot> current;
    current.reserve(m_world.worldObjects().size());
    for (WorldObject* worldObject : m_world.worldObjects()) {
        const auto id = m_world.entityId(worldObject);
        if (!id) {
            return false;
        }
        const Speed& speed = worldObject->physics().speed();
        const glm::vec3 engineState =
            worldObject->objectType() == WorldObjectType::Ship
                ? worldObject->components().engineState().directional()
                : glm::vec3(0.0f);
        current.push_back(voxellancer::protocol::EntitySnapshot{
            *id,
            worldObject->position(),
            worldObject->orientation(),
            speed.directional(),
            speed.angular(),
            entityKind(*worldObject),
            shieldFraction(*worldObject),
            hullFraction(*worldObject),
            worldObject->transform().scale(),
            worldObject->physics().maxMass(),
            engineState
        });
    }
    state.swap(current);
    return true;
}

std::uint64_t SimulationSession::tickIndex() const {
    return m_tickRunner.tickIndex();
}

double SimulationSession::backlogSeconds() const {
    return m_tickRunner.backlogSeconds();
}

void SimulationSession::applyInputCommands(std::uint64_t tick) {
    using namespace voxellancer::protocol;
    std::deque<PendingInput> pendingCommands;
    std::deque<EntityId> pendingRemovals;
    {
        std::lock_guard<std::mutex> lock(m_inputQueueMutex);
        pendingCommands.swap(m_pendingInputCommands);
        pendingRemovals.swap(m_pendingActorRemovals);
        m_processingInputCommandCount += pendingCommands.size();
    }
    bool hasScheduledRemoval = false;
    for (EntityId actor : pendingRemovals) {
        m_actorInputs.erase(entityIdKey(actor));
        WorldObject* worldObject = m_world.worldObject(actor);
        if (worldObject != nullptr) {
            m_world.god().scheduleRemoval(worldObject);
            hasScheduledRemoval = true;
        }
    }
    if (hasScheduledRemoval) {
        m_world.god().remove();
    }

    for (const PendingInput& pending : pendingCommands) {
        InputCommandResult result = InputCommandResult::UnknownActor;
        WorldObject* actorObject = m_world.worldObject(pending.actor);
        if (actorObject != nullptr && actorObject->objectType() == WorldObjectType::Ship) {
            const std::uint64_t key = entityIdKey(pending.actor);
            auto [input, inserted] = m_actorInputs.try_emplace(key);
            if (inserted) {
                input->second.actor = pending.actor;
            }
            result = input->second.commands.enqueue(pending.command, tick);
        }
        std::lock_guard<std::mutex> lock(m_inputQueueMutex);
        --m_processingInputCommandCount;
        m_inputAcknowledgements.push_back(InputCommandAcknowledgement{
            pending.actor, pending.command.sequence, result
        });
    }

    for (auto input = m_actorInputs.begin(); input != m_actorInputs.end();) {
        WorldObject* worldObject = m_world.worldObject(input->second.actor);
        if (worldObject == nullptr || worldObject->objectType() != WorldObjectType::Ship) {
            input = m_actorInputs.erase(input);
            continue;
        }

        const std::optional<InputCommand> command =
            input->second.commands.advanceToTick(tick);
        if (command) {
            Ship* ship = static_cast<Ship*>(worldObject);
            glm::vec3 directional(
                normalizedAxis(command->moveX),
                normalizedAxis(command->moveY),
                normalizedAxis(command->moveZ));
            glm::vec3 angular(
                normalizedAxis(command->aimX),
                normalizedAxis(command->aimY),
                normalizedAxis(command->roll));
            if (glm::length(directional) > 1.0f) {
                directional = glm::normalize(directional);
            }
            if (glm::length(angular) > 1.0f) {
                angular = glm::normalize(angular);
            }
            ship->components().setEngineState(EngineState(directional, angular));

            if ((command->buttons & FirePrimary) != 0) {
                const glm::vec3 forward = ship->orientation() * glm::vec3(0, 0, -1);
                ship->components().fireAtPoint(ship->position() + forward * 1000.0f, false);
            }
            if ((command->buttons & FireSecondary) != 0) {
                WorldObject* target = m_world.worldObject(command->target);
                if (target != nullptr && target != ship) {
                    ship->components().fireAtObject(target);
                }
            }
        }
        ++input;
    }
}
