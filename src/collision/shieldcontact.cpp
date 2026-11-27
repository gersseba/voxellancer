#include "shieldcontact.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <unordered_set>

#include "collision/collisiondetector.h"
#include "equipment/shield.h"
#include "equipment/shieldslot.h"
#include "geometry/capsule.h"
#include "geometry/sphere.h"
#include "geometry/transform.h"
#include "utils/safenormalize.h"
#include "voxel/voxel.h"
#include "voxel/voxelclusterbounds.h"
#include "voxel/voxeltreequery.h"
#include "worldobject/ship.h"
#include "worldobject/worldobjectcomponents.h"

namespace {
    bool sphereSweep(glm::vec3 start, glm::vec3 end, float radius, float& time) {
        const auto d = end - start;
        const float c = glm::dot(start,start) - radius*radius;
        if (c <= 0) { time = 0; return true; }
        const float a = glm::dot(d,d);
        if (a <= 1e-12f) return false;
        const float b = glm::dot(start,d);
        const float discriminant = b*b-a*c;
        if (discriminant < 0) return false;
        time = (-b-std::sqrt(discriminant))/a;
        return time >= 0 && time <= 1;
    }

    bool voxelSweep(glm::vec3 start, glm::vec3 end, glm::ivec3 cell,
                    float radius, float& time, bool includeSeparating) {
        const auto d = end-start;
        const glm::vec3 lower = glm::vec3(cell)-0.5f;
        const glm::vec3 upper = glm::vec3(cell)+0.5f;
        const auto initialOffset = start-glm::clamp(start,lower,upper);
        if (!includeSeparating && glm::dot(initialOffset,initialOffset) <= radius*radius &&
            glm::dot(initialOffset,d) >= 0) return false;
        std::array<float,8> cuts{};
        int count = 0;
        cuts[count++] = 0;
        cuts[count++] = 1;
        for (int axis = 0; axis < 3; ++axis) {
            if (std::abs(d[axis]) < 1e-12f) continue;
            for (float plane : {lower[axis],upper[axis]}) {
                const float t = (plane-start[axis])/d[axis];
                if (t > 0 && t < 1) cuts[count++] = t;
            }
        }
        std::sort(cuts.begin(),cuts.begin()+count);
        for (int i = 0; i+1 < count; ++i) {
            const float lo = cuts[i], hi = cuts[i+1], mid = (lo+hi)*0.5f;
            float a = 0, b = 0, c = -radius*radius;
            for (int axis = 0; axis < 3; ++axis) {
                const float p = start[axis]+d[axis]*mid;
                if (p >= lower[axis] && p <= upper[axis]) continue;
                const float offset = start[axis]-(p < lower[axis] ? lower[axis] : upper[axis]);
                a += d[axis]*d[axis];
                b += 2*offset*d[axis];
                c += offset*offset;
            }
            if ((a*lo+b)*lo+c <= 1e-6f) { time = lo; return true; }
            const float discriminant = b*b-4*a*c;
            if (a > 1e-12f && discriminant >= 0) {
                const float root = (-b-std::sqrt(discriminant))/(2*a);
                if (root >= lo && root <= hi) { time = root; return true; }
            }
        }
        return false;
    }
}

bool hasSphericalShield(WorldObject& object) {
    auto* ship = dynamic_cast<Ship*>(&object);
    if (!ship || ship->shieldType() != ShieldType::Sphere) return false;
    for (const auto& slot : ship->components().shieldSlots())
        if (slot && slot->shield() && !slot->shield()->depleted()) return true;
    return false;
}

std::optional<ShieldContact> sweepShieldContact(
    WorldObject& moving, const Transform& start, const Transform& end, WorldObject& other,
    bool includeSeparating) {
    const bool movingShield = hasSphericalShield(moving);
    const bool otherShield = hasSphericalShield(other);
    if ((!movingShield && !otherShield) || !moving.voxelCount() || !other.voxelCount())
        return std::nullopt;
    const auto movingStart = moving.bounds().sphere(start);
    const auto movingEnd = moving.bounds().sphere(end);
    const auto otherBounds = other.bounds().sphere(other.transform());
    float broadTime;
    if (!sphereSweep(movingStart.position()-otherBounds.position(),
                     movingEnd.position()-otherBounds.position(),
                     movingStart.radius()+otherBounds.radius(),broadTime))
        return std::nullopt;
    if (movingShield && otherShield) {
        if (!includeSeparating && broadTime == 0 &&
            glm::dot(movingStart.position()-otherBounds.position(),
                     movingEnd.position()-movingStart.position()) >= 0)
            return std::nullopt;
        const auto center = glm::mix(movingStart.position(),movingEnd.position(),broadTime);
        const auto normal = safeNormalize(otherBounds.position()-center,glm::vec3(1,0,0));
        return ShieldContact{broadTime,normal,center+normal*movingStart.radius(),
            nullptr,nullptr,true,true};
    }

    WorldObject& hull = movingShield ? other : moving;
    const auto& hullStart = movingShield ? other.transform() : start;
    const auto& hullEnd = movingShield ? other.transform() : end;
    const auto sphereStart = movingShield ? movingStart.position() : otherBounds.position();
    const auto sphereEnd = movingShield ? movingEnd.position() : otherBounds.position();
    const auto localStart = hullStart.inverseApplyTo(sphereStart);
    const auto localEnd = hullEnd.inverseApplyTo(sphereEnd);
    const float radius = (movingShield ? movingStart.radius() : otherBounds.radius()) /
        hullStart.scale();
    const auto queryStart = hullStart.applyTo(localStart);
    const auto queryEnd = hullStart.applyTo(localEnd);
    // Voxel-tree leaves use inscribed spheres, so cover the cube corners too.
    const float queryRadius = (radius + std::sqrt(3.0f)*0.5f-0.5f)*hullStart.scale();
    std::unordered_set<Voxel*> candidates;
    if (glm::distance(queryStart,queryEnd) < 1e-6f) {
        const Sphere query(queryStart,queryRadius);
        candidates = VoxelTreeQuery(&hull.collisionDetector().voxelTree(),&query).intersectingVoxels();
    } else {
        const Capsule query = Capsule::fromTo(queryStart,queryEnd,queryRadius);
        candidates = VoxelTreeQuery(&hull.collisionDetector().voxelTree(),&query).intersectingVoxels();
    }
    float earliest = std::numeric_limits<float>::infinity();
    Voxel* hit = nullptr;
    for (auto* voxel : candidates) {
        const auto cell = voxel->cell();
        float time;
        if (voxelSweep(localStart,localEnd,cell,radius,time,includeSeparating) &&
            (time < earliest || (time == earliest && hit &&
             (cell.x < hit->cell().x ||
              (cell.x == hit->cell().x && cell.y < hit->cell().y) ||
              (cell.x == hit->cell().x && cell.y == hit->cell().y &&
               cell.z < hit->cell().z))))) {
            earliest = time;
            hit = voxel;
        }
    }
    if (!hit) return std::nullopt;
    Transform hullAtContact = Transform(hullStart).mixed(hullEnd,earliest);
    const glm::vec3 center = glm::mix(localStart,localEnd,earliest);
    const glm::vec3 voxelCenter(hit->cell());
    const auto nearest = glm::clamp(center,voxelCenter-glm::vec3(0.5f),voxelCenter+glm::vec3(0.5f));
    const auto fallback = movingShield ? otherBounds.position()-movingStart.position() :
        movingStart.position()-otherBounds.position();
    const auto sphereToHull = safeNormalize(hullAtContact.orientation()*(nearest-center),
        safeNormalize(fallback,glm::vec3(1,0,0)));
    const auto normal = movingShield ? sphereToHull : -sphereToHull;
    return ShieldContact{earliest,normal,hullAtContact.applyTo(nearest),
        movingShield ? nullptr : hit,movingShield ? hit : nullptr,movingShield,otherShield};
}
