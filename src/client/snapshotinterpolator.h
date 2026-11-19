#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "protocol/serverprotocol.h"

class SnapshotInterpolator {
public:
    void reset();
    bool hasInterpolationPair() const;
    bool push(
        std::uint64_t tick,
        const std::vector<voxellancer::protocol::EntitySnapshot>& state
    );
    std::optional<voxellancer::protocol::EntitySnapshot> sample(
        voxellancer::protocol::EntityId entity,
        double renderTick
    ) const;

private:
    bool m_initialized = false;
    std::uint64_t m_previousTick = 0;
    std::uint64_t m_currentTick = 0;
    std::vector<voxellancer::protocol::EntitySnapshot> m_previousState;
    std::vector<voxellancer::protocol::EntitySnapshot> m_currentState;
};
