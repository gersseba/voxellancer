#pragma once

#include "protocol/serverprotocol.h"

inline glm::vec3 remoteVoxelCenter(
    const std::vector<voxellancer::protocol::EntityVoxelState>& voxels)
{
    glm::vec3 weightedCenter(0.0f);
    float density = 0.0f;
    for (const auto& voxel : voxels) {
        weightedCenter += glm::vec3(voxel.cell) * voxel.density;
        density += voxel.density;
    }
    return density > 0.0f ? weightedCenter / density : glm::vec3(0.0f);
}
