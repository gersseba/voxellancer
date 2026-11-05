#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "equipment/enginepower.h"
#include "protocol/serverprotocol.h"

class InputPredictor {
public:
    std::optional<voxellancer::protocol::EntitySnapshot> predict(
        const voxellancer::protocol::EntitySnapshot& authoritativeState,
        std::uint64_t authoritativeTick,
        std::uint64_t targetTick,
        const std::vector<voxellancer::protocol::InputCommand>& commands,
        const EnginePower& enginePower,
        float directionalDampening,
        float angularDampening
    ) const;
};
