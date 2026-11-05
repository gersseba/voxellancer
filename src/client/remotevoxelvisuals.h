#pragma once

#include "display/rendering/visuals.h"
#include "protocol/serverprotocol.h"

Visuals remoteVoxelVisuals(
    const voxellancer::protocol::EntityVoxelState& voxel,
    const std::vector<voxellancer::protocol::EntityEquipmentState>& equipment);
