#include "aimer.h"

#include <glm/glm.hpp>
#include <algorithm>
#include <limits>
#include <stdexcept>

#include "collision/collisiondetector.h"
#include "equipment/hardpoint.h"
#include "equipment/weapons/gun.h"
#include "geometry/voxelray.h"
#include "property/property.h"

#include "voxel/voxel.h"
#include "voxel/voxeltree.h"
#include "voxel/voxeltreenode.h"

#include "world/world.h"
#include "worldobject/worldobject.h"
#include "worldobject/worldobjectcomponents.h"

#include "worldtree/worldtreegeode.h"
#include "worldtree/worldtreequery.h"


Aimer::Aimer(WorldObject* worldObject) :
    m_worldObject(worldObject)
{
}

void Aimer::setWorldObject(WorldObject* worldObject) {
    m_worldObject = worldObject;
}

CrosshairAim Aimer::aim(const Ray& ray) {
    assert(m_worldObject);
    assert(m_worldObject->collisionDetector().geode());
    if (m_worldObject->world() == nullptr) {
        throw std::logic_error("Aiming requires an object attached to a world.");
    }

    float range = std::numeric_limits<float>::infinity();
    for (const auto& hardpoint : m_worldObject->components().hardpoints()) {
        const auto* gun = dynamic_cast<Gun*>(hardpoint->weapon().get());
        if (!gun) continue;
        const float travel = gun->bulletSpeed() * gun->bulletLifetime();
        range = std::min(range, gun->profile().enabled
            ? std::min(travel, gun->profile().range) : travel);
    }
    if (!std::isfinite(range)) range = Property<float>("general.infinityAimDistance");
    const glm::vec3 direction = glm::normalize(ray.direction());
    const Ray normalized(ray.origin(), direction);
    WorldTreeQuery query(&m_worldObject->world()->worldTree(), &normalized,
        m_worldObject->collisionDetector().geode()->hint(), nullptr);
    float nearest = range;
    float aimDistance = range;
    for (Voxel* voxel : query.intersectingVoxels()) {
        WorldObject* object = voxel->voxelTreeNode()->voxelTree()->worldObject();
        if (object == m_worldObject) continue;
        const auto& transform = object->transform();
        const auto hit = voxelRayInterval(ray.origin(), direction, transform.position(),
            transform.orientation(), transform.scale(), transform.center(), voxel->cell(), range);
        if (hit && hit->x < nearest) {
            nearest = hit->x;
            aimDistance = (hit->x + hit->y) * 0.5f;
        }
    }
    return {ray.origin() + direction * aimDistance, nearest < range};
}
