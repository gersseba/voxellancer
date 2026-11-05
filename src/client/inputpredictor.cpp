#include "inputpredictor.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtx/quaternion.hpp>

#include "equipment/enginestate.h"
#include "utils/simulationtickrunner.h"

namespace {
    using voxellancer::protocol::InputCommand;
    using voxellancer::protocol::EntitySnapshot;

    constexpr std::uint64_t MaxPredictionTicks = 8;

    float normalizedAxis(std::int16_t value) {
        const float divisor = value < 0 ? 32768.0f : 32767.0f;
        return glm::clamp(static_cast<float>(value) / divisor, -1.0f, 1.0f);
    }

    bool finiteVector(const glm::vec3& value) {
        return std::isfinite(value.x) &&
            std::isfinite(value.y) &&
            std::isfinite(value.z);
    }

    EngineState engineState(const InputCommand* command) {
        if (command == nullptr) {
            return EngineState();
        }

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
        return EngineState(directional, angular);
    }
}

std::optional<voxellancer::protocol::EntitySnapshot> InputPredictor::predict(
    const voxellancer::protocol::EntitySnapshot& authoritativeState,
    std::uint64_t authoritativeTick,
    std::uint64_t targetTick,
    const std::vector<voxellancer::protocol::InputCommand>& commands,
    const EnginePower& enginePower,
    float directionalDampening,
    float angularDampening
) const {
    if (authoritativeState.kind != EntitySnapshot::Kind::Ship ||
        !std::isfinite(authoritativeState.maxMass) ||
        authoritativeState.maxMass < 0.0f ||
        !std::isfinite(directionalDampening) ||
        directionalDampening < 0.0f ||
        !std::isfinite(angularDampening) ||
        angularDampening < 0.0f ||
        !finiteVector(authoritativeState.position) ||
        !finiteVector(authoritativeState.directionalVelocity) ||
        !finiteVector(authoritativeState.angularVelocity) ||
        !std::isfinite(enginePower.directional().x) ||
        !std::isfinite(enginePower.directional().y) ||
        !std::isfinite(enginePower.directional().z) ||
        !std::isfinite(enginePower.directional().w) ||
        !finiteVector(enginePower.angular())) {
        return std::nullopt;
    }

    EntitySnapshot predicted = authoritativeState;
    if (targetTick <= authoritativeTick) {
        return predicted;
    }
    const std::uint64_t tickLimit =
        authoritativeTick > std::numeric_limits<std::uint64_t>::max() -
                MaxPredictionTicks
            ? std::numeric_limits<std::uint64_t>::max()
            : authoritativeTick + MaxPredictionTicks;
    const std::uint64_t finalTick = std::min(targetTick, tickLimit);
    std::vector<const InputCommand*> orderedCommands;
    orderedCommands.reserve(commands.size());
    for (const InputCommand& command : commands) {
        orderedCommands.push_back(&command);
    }
    std::stable_sort(
        orderedCommands.begin(),
        orderedCommands.end(),
        [](const InputCommand* left, const InputCommand* right) {
            return left->tick < right->tick ||
                (left->tick == right->tick &&
                    left->sequence < right->sequence);
        });

    std::size_t nextCommandIndex = 0;
    const InputCommand* activeCommand = nullptr;
    while (nextCommandIndex < orderedCommands.size() &&
        orderedCommands[nextCommandIndex]->tick < authoritativeTick) {
        activeCommand = orderedCommands[nextCommandIndex++];
    }

    constexpr float TickDuration =
        1.0f / static_cast<float>(SimulationTickRunner::TickRate);
    for (std::uint64_t tick = authoritativeTick;
         tick < finalTick;
         ++tick) {
        // World updates movement before applying engine acceleration.
        while (nextCommandIndex < orderedCommands.size() &&
            orderedCommands[nextCommandIndex]->tick < tick) {
            activeCommand = orderedCommands[nextCommandIndex++];
        }
        predicted.engineState = activeCommand != nullptr
            ? engineState(activeCommand).directional()
            : authoritativeState.engineState;

        EnginePower power = enginePower;
        const Acceleration localAcceleration =
            authoritativeState.maxMass > 0.0f
                ? power.accelerationAt(engineState(activeCommand)) /
                    authoritativeState.maxMass
                : Acceleration();
        const glm::vec3 globalDirectionalAcceleration =
            predicted.orientation * localAcceleration.directional();
        const glm::vec3 globalAngularAcceleration =
            predicted.orientation * localAcceleration.angular();
        predicted.directionalVelocity *= 1.0f - glm::min(
            1.0f, directionalDampening * TickDuration);
        predicted.directionalVelocity +=
            globalDirectionalAcceleration * TickDuration;
        predicted.angularVelocity *= 1.0f - glm::min(
            1.0f, angularDampening * TickDuration);
        predicted.angularVelocity +=
            globalAngularAcceleration * TickDuration;
        predicted.position += predicted.directionalVelocity * TickDuration;
        predicted.orientation = glm::normalize(
            glm::quat(predicted.angularVelocity * TickDuration) *
            predicted.orientation);
        if (!finiteVector(predicted.position) ||
            !finiteVector(predicted.directionalVelocity) ||
            !finiteVector(predicted.angularVelocity) ||
            !std::isfinite(predicted.orientation.w) ||
            !std::isfinite(predicted.orientation.x) ||
            !std::isfinite(predicted.orientation.y) ||
            !std::isfinite(predicted.orientation.z)) {
            return std::nullopt;
        }
    }

    return predicted;
}
