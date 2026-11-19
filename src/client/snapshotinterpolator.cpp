#include "snapshotinterpolator.h"

#include <algorithm>
#include <cmath>
#include <glm/gtx/quaternion.hpp>
#include <utility>

#include "utils/simulationtickrunner.h"

namespace {
    using voxellancer::protocol::EntityId;
    using voxellancer::protocol::EntitySnapshot;

    bool idLess(EntityId left, EntityId right) {
        return left.index < right.index ||
            (left.index == right.index && left.generation < right.generation);
    }

    const EntitySnapshot* findEntity(
        const std::vector<EntitySnapshot>& state,
        EntityId id
    ) {
        const auto entity = std::lower_bound(
            state.begin(),
            state.end(),
            id,
            [](const EntitySnapshot& candidate, EntityId key) {
                return idLess(candidate.id, key);
            });
        return entity != state.end() && entity->id == id ? &*entity : nullptr;
    }
}

void SnapshotInterpolator::reset() {
    m_initialized = false;
    m_previousTick = 0;
    m_currentTick = 0;
    m_previousState.clear();
    m_currentState.clear();
}

bool SnapshotInterpolator::hasInterpolationPair() const {
    return m_initialized && m_previousTick < m_currentTick;
}

bool SnapshotInterpolator::push(
    std::uint64_t tick,
    const std::vector<voxellancer::protocol::EntitySnapshot>& state
) {
    if (state.size() > voxellancer::protocol::MaxSnapshotEntries ||
        !std::is_sorted(state.begin(), state.end(),
            [](const auto& left, const auto& right) {
                return idLess(left.id, right.id);
            })) {
        return false;
    }
    for (std::size_t index = 0; index < state.size(); ++index) {
        if (!state[index].id.valid() ||
            (index > 0 && state[index - 1].id == state[index].id)) {
            return false;
        }
    }
    if (m_initialized && tick < m_currentTick) {
        return false;
    }

    if (!m_initialized) {
        m_initialized = true;
        m_previousTick = tick;
        m_currentTick = tick;
        m_currentState = state;
        return true;
    }
    if (tick == m_currentTick) {
        m_currentState = state;
        return true;
    }

    m_previousTick = m_currentTick;
    m_previousState = std::move(m_currentState);
    m_currentTick = tick;
    m_currentState = state;
    return true;
}

std::optional<voxellancer::protocol::EntitySnapshot> SnapshotInterpolator::sample(
    voxellancer::protocol::EntityId entity,
    double renderTick
) const {
    if (!m_initialized || !entity.valid() || !std::isfinite(renderTick)) {
        return std::nullopt;
    }

    const EntitySnapshot* previous = findEntity(m_previousState, entity);
    const EntitySnapshot* current = findEntity(m_currentState, entity);
    if (previous == nullptr) {
        if (current == nullptr ||
            (m_previousTick != m_currentTick &&
                renderTick < static_cast<double>(m_currentTick))) {
            return std::nullopt;
        }
        return *current;
    }
    if (current == nullptr) {
        return renderTick < static_cast<double>(m_currentTick)
            ? std::optional<EntitySnapshot>(*previous)
            : std::nullopt;
    }
    if (renderTick >= static_cast<double>(m_currentTick)) {
        constexpr double MaxPredictionSeconds = 0.05;
        const float predictionSeconds = static_cast<float>(std::min(
            (renderTick - static_cast<double>(m_currentTick)) /
                SimulationTickRunner::TickRate,
            MaxPredictionSeconds));
        EntitySnapshot predicted = *current;
        predicted.position += current->directionalVelocity * predictionSeconds;
        predicted.orientation = glm::normalize(
            glm::quat(current->angularVelocity * predictionSeconds) *
            current->orientation);
        return predicted;
    }

    const double duration = static_cast<double>(m_currentTick - m_previousTick);
    const float alpha = duration > 0.0
        ? static_cast<float>(std::clamp(
            (renderTick - static_cast<double>(m_previousTick)) / duration,
            0.0,
            1.0))
        : 1.0f;
    EntitySnapshot interpolated = *current;
    interpolated.position = glm::mix(previous->position, current->position, alpha);
    interpolated.orientation = glm::normalize(
        glm::slerp(previous->orientation, current->orientation, alpha));
    interpolated.directionalVelocity = glm::mix(
        previous->directionalVelocity, current->directionalVelocity, alpha);
    interpolated.angularVelocity = glm::mix(
        previous->angularVelocity, current->angularVelocity, alpha);
    interpolated.engineState = glm::mix(
        previous->engineState, current->engineState, alpha);
    interpolated.shieldFraction = glm::mix(
        previous->shieldFraction, current->shieldFraction, alpha);
    interpolated.hullFraction = glm::mix(
        previous->hullFraction, current->hullFraction, alpha);
    interpolated.scale = glm::mix(previous->scale, current->scale, alpha);
    return interpolated;
}
