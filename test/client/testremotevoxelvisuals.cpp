#include <bandit/bandit.h>

#include "client/remotevoxelvisuals.h"
#include "property/propertydirectory.h"
#include "property/propertymanager.h"
#include "voxel/voxel.h"

using namespace bandit;
using namespace voxellancer::protocol;

go_bandit([]() {
    describe("remote voxel equipment visuals", []() {
        it("reconstructs mounted and empty engine and weapon slot visuals", []() {
            PropertyManager::instance()->load("data/voxels.ini", "voxels");
            PropertyDirectory("data/equipment/engines").read();
            PropertyDirectory("data/equipment/weapons").read();

            EntityVoxelState voxel;
            voxel.cell = glm::ivec3(2, 3, 4);
            voxel.color = 0x0000FF;

            const auto engineVisuals = remoteVoxelVisuals(
                voxel,
                {{
                    EquipmentSlotKind::Engine,
                    0,
                    voxel.cell,
                    "enginemk1"
                }});
            AssertThat(engineVisuals.color(), Equals(0x0022FFu));
            AssertThat(engineVisuals.emissiveness(), Equals(0.5f));
            const Voxel renderedEngineSlot(
                voxel.cell,
                engineVisuals.color(),
                voxel.density,
                voxel.maxHP,
                engineVisuals.emissiveness());
            AssertThat(
                renderedEngineSlot.visuals().emissiveness(),
                Equals(0.5f));

            const auto weaponVisuals = remoteVoxelVisuals(
                voxel,
                {{
                    EquipmentSlotKind::Weapon,
                    0,
                    voxel.cell,
                    "gun"
                }});
            AssertThat(weaponVisuals.color(), Equals(0x00FF00u));
            AssertThat(weaponVisuals.emissiveness(), Equals(0.3f));

            const auto emptyEngineVisuals = remoteVoxelVisuals(
                voxel,
                {{
                    EquipmentSlotKind::Engine,
                    0,
                    voxel.cell,
                    ""
                }});
            AssertThat(emptyEngineVisuals.color(), Equals(0x0000FFu));
            AssertThat(emptyEngineVisuals.emissiveness(), Equals(0.0f));
        });
    });
});
