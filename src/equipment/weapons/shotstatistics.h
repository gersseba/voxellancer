#pragma once

#include <cstdint>
#include <memory>

#include "utils/handle/handle.h"

class WorldObject;

struct ShotStatistics {
    std::uint64_t shots = 0;
    std::uint64_t hits = 0;
};

struct ShotRecord {
    std::shared_ptr<ShotStatistics> statistics;
    Handle<WorldObject> target;
    bool hit = false;

    void recordHit(WorldObject* object) {
        if (!hit && statistics && target.get() == object) {
            hit = true;
            ++statistics->hits;
        }
    }
};
