#include "remotevoxelvisuals.h"

#include <algorithm>

#include "property/property.h"

Visuals remoteVoxelVisuals(
    const voxellancer::protocol::EntityVoxelState& voxel,
    const std::vector<voxellancer::protocol::EntityEquipmentState>& equipment)
{
    using namespace voxellancer::protocol;

    const auto slot = std::find_if(
        equipment.begin(),
        equipment.end(),
        [&voxel](const EntityEquipmentState& state) {
            return state.cell == voxel.cell &&
                (state.kind == EquipmentSlotKind::Engine ||
                 state.kind == EquipmentSlotKind::Weapon);
        });
    if (slot == equipment.end()) {
        return Visuals(voxel.color, 0.0f);
    }

    if (!slot->equipmentKey.empty()) {
        return Visuals::fromProperties(slot->equipmentKey + ".visuals");
    }

    const char* baseVisuals = slot->kind == EquipmentSlotKind::Engine
        ? "voxels.engineSlot.color"
        : "voxels.hardpoint.color";
    return Visuals(Property<std::uint32_t>::get(baseVisuals), 0.0f);
}
