#include "mover.h"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <unordered_set>
#include <glow/logging.h>

#include "benchmark/benchmarkprofiler.h"
#include "collision/collisiondetector.h"
#include "collision/collisionfilter.h"
#include "equipment/shield.h"
#include "equipment/shieldslot.h"
#include "equipment/weapons/projectile.h"
#include "geometry/sphere.h"
#include "geometry/aabb.h"
#include "voxel/voxel.h"
#include "voxel/voxelclusterbounds.h"
#include "world/god.h"
#include "world/world.h"
#include "worldobject/worldobject.h"
#include "worldobject/worldobjectcomponents.h"
#include "worldobject/ship.h"
#include "physics/physics.h"
#include "worldtree/worldtreegeode.h"
#include "worldtree/worldtreequery.h"
#include "geometry/capsule.h"
#include "geometry/segment.h"
#include "voxel/voxeltreequery.h"
#include "world/worldlogic.h"
#include "world/helper/damageimpact.h"


Mover::Mover(World& world):
    m_world(world)
{
}

void Mover::moveWorldObjects(float deltaSec) {
    m_voxelCollisions.clear();
    std::vector<std::pair<WorldObject*, glm::vec3>> projectileStartPositions;

    for (WorldObject *worldObject : m_world.worldObjects()) {
        auto* projectile = dynamic_cast<Projectile*>(worldObject);
        if (projectile && projectile->profile.enabled) {
            continue;
        }
        if (worldObject->objectType() == WorldObjectType::Bullet ||
            worldObject->objectType() == WorldObjectType::Rocket) {
            projectileStartPositions.emplace_back(
                worldObject, worldObject->transform().position());
        }
        std::list<VoxelCollision>* collisionsPointer = nullptr;
        if (worldObject->objectType() == WorldObjectType::Bullet ||
            worldObject->objectType() == WorldObjectType::Rocket) {
            BenchmarkCpuScope scope(BenchmarkMetric::ProjectileMovement);
            collisionsPointer = &worldObject->physics().move(deltaSec);
        } else {
            collisionsPointer = &worldObject->physics().move(deltaSec);
        }
        std::list<VoxelCollision>& collisions = *collisionsPointer;
        m_voxelCollisions.splice(m_voxelCollisions.end(), collisions);
    }

    interceptShieldHits(projectileStartPositions);
}

void Mover::moveProfiledProjectiles(float deltaSec) {
    std::vector<Projectile *> projectiles;
    for (auto *object : m_world.worldObjects()) {
        auto *projectile = dynamic_cast<Projectile *>(object);
        if (projectile && projectile->profile.enabled)
            projectiles.push_back(projectile);
    }
    std::sort(projectiles.begin(), projectiles.end(), [this](const auto *a, const auto *b) {
        return m_world.entityId(a)->index < m_world.entityId(b)->index;
    });
    for (auto *projectile : projectiles)
        moveProfiledProjectile(*projectile, deltaSec);
}

void Mover::moveProfiledProjectile(Projectile &projectile, float deltaSec, unsigned impacts) {
    if (impacts >= 32) {
        glow::warning("Projectile traversal capped at 32 impacts in one tick.");
        projectile.onShieldImpact();
        return;
    }
    if (projectile.lifetime() <= 0 ||
        projectile.distanceTraveled >= projectile.profile.range - 1e-4f) {
        m_world.god().scheduleRemoval(&projectile);
        return;
    }
    deltaSec = std::min(deltaSec, projectile.lifetime());
    if (deltaSec <= 0)
        return;
    struct Contact {
        float time;
        WorldObject *object;
        glm::ivec3 cell;
        Shield *shield;
    };
    const auto &profile = projectile.profile;
    const auto start = projectile.position();
    const auto velocity = projectile.physics().speed().directional();
    if (glm::dot(velocity, velocity) < 1e-10f)
        return;
    const float relativeDistance = glm::length(velocity - projectile.launchVelocity) * deltaSec;
    const float remainingRange = std::max(0.0f, profile.range - projectile.distanceTraveled);
    const float portion =
        relativeDistance > 0 ? std::min(1.0f, remainingRange / relativeDistance) : 1;
    const auto end = start + velocity * (deltaSec * portion);
    const auto direction = glm::normalize(velocity);
    float padding = 0;
    for (auto *object : m_world.worldObjects()) {
        auto *ship = dynamic_cast<Ship *>(object);
        if (ship && ship->shieldType() != ShieldType::None)
            padding = std::max(padding, ship->bounds().sphere(ship->transform()).radius());
    }
    const IAABB broadphase(glm::ivec3(glm::floor(glm::min(start, end) - padding)),
                           glm::ivec3(glm::ceil(glm::max(start, end) + padding)));
    const auto candidates = WorldTreeQuery(&m_world.worldTree(), &broadphase,
                                           projectile.collisionDetector().geode()->hint(),
                                           &projectile.collisionFilter())
                                .nearGeodes();
    std::vector<Contact> contacts;
    for (auto *candidate : candidates) {
        auto *object = candidate->worldObject();
        if (dynamic_cast<Projectile *>(object) || object->voxelCount() == 0)
            continue;
        if (auto *ship = dynamic_cast<Ship *>(object)) {
            for (const auto &slot : ship->components().shieldSlots()) {
                if (!slot->shield())
                    continue;
                glm::ivec3 cell(0);
                float t = 0;
                if (slot->shield()->intersectsSegment(*ship, ship->shieldType(), start, end, 0,
                                                      cell, t))
                    contacts.push_back({t, object, cell, slot->shield().get()});
            }
        }
        const auto localStart = object->transform().inverseApplyTo(start);
        const auto localEnd = object->transform().inverseApplyTo(end);
        const auto ray = Capsule::fromTo(start, end, 0.001f);
        const auto voxels =
            VoxelTreeQuery(&object->collisionDetector().voxelTree(), &ray).intersectingVoxels();
        for (auto *voxel : voxels) {
            float t = 0;
            if (segmentIntersectsVoxel(localStart, localEnd, voxel->cell(), 0.5f, t))
                contacts.push_back({t, object, voxel->cell(), nullptr});
        }
    }
    const auto cellLess = [](const glm::ivec3 &a, const glm::ivec3 &b) {
        return a.x != b.x ? a.x < b.x : a.y != b.y ? a.y < b.y : a.z < b.z;
    };
    std::stable_sort(contacts.begin(), contacts.end(), [&](const Contact &a, const Contact &b) {
        if (a.time != b.time)
            return a.time < b.time;
        const auto aid = m_world.entityId(a.object).value();
        const auto bid = m_world.entityId(b.object).value();
        if (aid.index != bid.index)
            return aid.index < bid.index;
        if (a.shield != nullptr || b.shield != nullptr)
            return a.shield != nullptr && b.shield == nullptr;
        return cellLess(a.cell, b.cell);
    });
    bool stopped = false;
    float finalParameter = 1;
    glm::vec3 remainingVelocity = velocity;
    glm::vec3 finalPosition = end;
    for (const auto &contact : contacts) {
        auto *target = contact.object;
        auto found = target->voxelMap().find(contact.cell);
        if (!contact.shield && found == target->voxelMap().end())
            continue;
        if (projectile.shotRecord) projectile.shotRecord->recordHit(target);
        const auto point = glm::mix(start, end, contact.time);
        if (!projectile.impactCalibrated) {
            if (profile.physical) {
                const auto relative = velocity - target->physics().speed().directional();
                projectile.impactBudget *=
                    glm::dot(relative, relative) / (profile.speed * profile.speed);
            }
            projectile.impactCalibrated = true;
        }
        const float incomingBudget = projectile.impactBudget;
        const auto targetVelocity = target->physics().speed().directional();
        const auto transferMomentum = [&](float outgoingBudget) {
            if (!profile.physical || target->physics().mass() <= 0 || incomingBudget <= 0)
                return;
            const auto nextVelocity =
                targetVelocity + (remainingVelocity - targetVelocity) *
                                     std::sqrt(std::max(0.0f, outgoingBudget / incomingBudget));
            const auto impulse = profile.mass * (remainingVelocity - nextVelocity);
            const auto old = target->physics().speed();
            const float radius = target->bounds().sphere(target->transform()).radius();
            const float inertia = target->physics().mass() * std::max(1.0f, radius * radius);
            target->physics().setSpeed(
                Speed(old.directional() + impulse / target->physics().mass(),
                      old.angular() + glm::cross(point - target->position(), impulse) / inertia));
            remainingVelocity = nextVelocity;
        };
        if (target->info().invulnerable()) {
            stopped = true;
        } else if (contact.shield) {
            const float incoming = profile.shield * projectile.impactBudget;
            const float overflow = contact.shield->compensate(incoming);
            projectile.impactBudget = overflow / profile.shield;
            if (m_shieldHitEventReplicationEnabled && incoming > overflow) {
                if (m_shieldHitEvents.size() >= 4096)
                    throw std::overflow_error("Shield-hit event queue exceeded its valid limit.");
                m_shieldHitEvents.push_back({m_world.entityId(target).value(),
                                             m_world.simulationTickIndex(), contact.cell,
                                             target->transform().inverseApplyTo(point),
                                             projectile.impactColor(), incoming - overflow});
            }
            stopped = projectile.impactBudget <= 1e-6f;
            transferMomentum(stopped ? 0 : projectile.impactBudget);
        } else {
            const float budgetBefore = projectile.impactBudget;
            std::list<DamageImpact> impacts;
            const auto damageVoxel = [&](Voxel *voxel, float fraction) {
                const float materialDamage = voxel->armored() ? profile.armor : profile.normal;
                const float capacity = materialDamage * fraction;
                const float damage = capacity >= voxel->hp() - 1e-4f ? voxel->hp() : capacity;
                impacts.emplace_back(target, voxel, direction * damage, 0, true);
                impacts.back().setDamage(damage);
                return damage / materialDamage;
            };
            if (profile.radius > 0) {
                struct Surface {
                    Voxel *voxel;
                    float distance;
                };
                std::unordered_set<Voxel *> destroyed;
                std::unordered_set<Voxel *> affected;
                float remaining = profile.isBlast
                    ? budgetBefore * profile.propagationFraction : budgetBefore;
                if (!profile.isBlast) {
                    Voxel *first = found->second;
                    const float spent = damageVoxel(first, remaining);
                    remaining = std::max(0.0f, remaining - spent);
                    affected.insert(first);
                    if (spent * (first->armored() ? profile.armor : profile.normal) >=
                        first->hp() - 1e-4f) {
                        destroyed.insert(first);
                        remaining *= profile.propagationFraction;
                    } else {
                        remaining = 0;
                    }
                }

                const float scale = target->transform().scale();
                const Sphere area(point, profile.radius * scale);
                const auto nearby = VoxelTreeQuery(&target->collisionDetector().voxelTree(), &area)
                                        .intersectingVoxels();
                const auto origin = point - direction * (0.001f * scale);
                const auto localOrigin = target->transform().inverseApplyTo(origin);
                const float angleLimit = std::cos(glm::radians(profile.propagationAngle * 0.5f));
                std::vector<Surface> candidates;
                for (auto *voxel : nearby) {
                    const auto offset = voxel->position() - point;
                    const float length = glm::length(offset);
                    const float distance = length / scale;
                    if (distance >= profile.radius)
                        continue;
                    if (profile.propagationAngle < 360 && length > 1e-6f &&
                        glm::dot(offset / length, direction) + 1e-6f < angleLimit)
                        continue;
                    candidates.push_back({voxel, distance});
                }
                std::sort(candidates.begin(), candidates.end(),
                          [&](const Surface &a, const Surface &b) {
                              return a.distance != b.distance
                                         ? a.distance < b.distance
                                         : cellLess(a.voxel->cell(), b.voxel->cell());
                          });
                constexpr std::size_t MaxPropagationRecipients = 12;
                std::size_t recipients = 0;
                while (remaining > 1e-6f && recipients < MaxPropagationRecipients) {
                    std::vector<Surface> surfaces;
                    for (const auto &candidate : candidates) {
                        auto *voxel = candidate.voxel;
                        if (affected.count(voxel))
                            continue;
                        const glm::vec3 cell(voxel->cell());
                        const auto nearest = glm::clamp(localOrigin, cell - glm::vec3(0.5f),
                                                        cell + glm::vec3(0.5f));
                        const auto toOrigin = localOrigin - nearest;
                        const auto localSurface = glm::length(toOrigin) > 1e-6f
                            ? nearest + glm::normalize(toOrigin) * 0.001f : nearest;
                        const auto sight = Capsule::fromTo(origin,
                            target->transform().applyTo(localSurface), 0.001f * scale);
                        bool blocked = false;
                        for (auto *obstacle :
                             VoxelTreeQuery(&target->collisionDetector().voxelTree(), &sight)
                                 .intersectingVoxels()) {
                            float t;
                            if (obstacle != voxel && !destroyed.count(obstacle) &&
                                segmentIntersectsVoxel(localOrigin, localSurface,
                                                       obstacle->cell(), 0.5f, t)) {
                                blocked = true;
                                break;
                            }
                        }
                        if (!blocked) {
                            surfaces.push_back(candidate);
                            if (surfaces.size() == MaxPropagationRecipients - recipients)
                                break;
                        }
                    }
                    float totalWeight = 0;
                    for (const auto &surface : surfaces)
                        totalWeight += 1 - surface.distance / profile.radius;
                    if (totalWeight <= 0)
                        break;

                    float overflow = 0;
                    for (const auto &surface : surfaces) {
                        const float share = remaining *
                            (1 - surface.distance / profile.radius) / totalWeight;
                        const float spent = damageVoxel(surface.voxel, share);
                        affected.insert(surface.voxel);
                        const float materialDamage =
                            surface.voxel->armored() ? profile.armor : profile.normal;
                        if (spent * materialDamage >= surface.voxel->hp() - 1e-4f) {
                            destroyed.insert(surface.voxel);
                            overflow += std::max(0.0f, share - spent);
                        }
                    }
                    recipients += surfaces.size();
                    remaining = overflow * profile.propagationFraction;
                }
                projectile.impactBudget = 0;
            } else {
                projectile.impactBudget -= damageVoxel(found->second, budgetBefore);
                ++projectile.penetratedVoxels;
            }
            stopped = projectile.impactBudget <= 1e-6f ||
                      projectile.penetratedVoxels >= profile.penetration;
            transferMomentum(stopped ? 0 : projectile.impactBudget);
            m_world.worldLogic().processDamageImpacts(std::move(impacts));
        }
        if (stopped) {
            finalPosition = point;
            finalParameter = contact.time;
            break;
        }
        if (profile.physical) {
            projectile.distanceTraveled += relativeDistance * portion * contact.time;
            projectile.updateTransformAndGeode(point, projectile.orientation());
            projectile.physics().setSpeed(
                Speed(remainingVelocity, projectile.physics().speed().angular()));
            moveProfiledProjectile(projectile, deltaSec * (1 - portion * contact.time),
                                   impacts + 1);
            return;
        }
    }
    projectile.distanceTraveled += relativeDistance * portion * finalParameter;
    projectile.updateTransformAndGeode(finalPosition, projectile.orientation());
    if (profile.physical && !stopped) {
        projectile.physics().setSpeed(
            Speed(remainingVelocity, projectile.physics().speed().angular()));
    }
    if (stopped)
        projectile.onShieldImpact();
    else if (projectile.distanceTraveled >= profile.range - 1e-4f)
        m_world.god().scheduleRemoval(&projectile);
}

std::list<VoxelCollision> &Mover::voxelCollisions() {
    return m_voxelCollisions;
}

void Mover::setShieldHitEventReplicationEnabled(bool enabled) {
    m_shieldHitEventReplicationEnabled = enabled;
    if (!enabled) {
        m_shieldHitEvents.clear();
    }
}

std::vector<voxellancer::protocol::ShieldHitEvent>
Mover::drainShieldHitEvents() {
    std::vector<voxellancer::protocol::ShieldHitEvent> events;
    events.swap(m_shieldHitEvents);
    return events;
}

void Mover::interceptShieldHits(
    const std::vector<std::pair<WorldObject*, glm::vec3>>&
        projectileStartPositions
) {
    if (projectileStartPositions.empty()) {
        return;
    }

    float maximumShieldRadius = 0.0f;
    for (WorldObject* object : m_world.worldObjects()) {
        Ship* ship = dynamic_cast<Ship*>(object);
        if (ship == nullptr || ship->shieldType() == ShieldType::None) {
            continue;
        }
        for (const std::shared_ptr<ShieldSlot>& slot :
            object->components().shieldSlots()) {
            if (slot && slot->shield() && !slot->shield()->depleted()) {
                const Sphere boundSphere =
                    object->bounds().sphere(object->transform());
                maximumShieldRadius = std::max(maximumShieldRadius,
                    glm::length(
                        boundSphere.position() - object->transform().position()) +
                        boundSphere.radius());
                break;
            }
        }
    }

    std::unordered_set<WorldObject*> interceptedProjectiles;
    for (const auto& startEntry : projectileStartPositions) {
        WorldObject* projectileObject = startEntry.first;
        WorldTreeGeode* projectileGeode =
            projectileObject->collisionDetector().geode();
        if (projectileGeode == nullptr || maximumShieldRadius <= 0.0f) {
            continue;
        }

        const glm::vec3 start = startEntry.second;
        const glm::vec3 end = projectileObject->transform().position();
        const float projectileRadius =
            projectileObject->bounds().sphere(projectileObject->transform()).radius();
        const float broadphaseRadius = maximumShieldRadius + projectileRadius;
        const glm::vec3 lower = glm::min(start, end) -
            glm::vec3(broadphaseRadius);
        const glm::vec3 upper = glm::max(start, end) +
            glm::vec3(broadphaseRadius);
        const IAABB phaseAABB(
            glm::ivec3(glm::floor(lower)),
            glm::ivec3(glm::ceil(upper)));
        const auto candidates = WorldTreeQuery(
            projectileObject->collisionDetector().worldTree(),
            &phaseAABB,
            projectileGeode->hint(),
            &projectileObject->collisionFilter()).nearGeodes();

        Shield* hitShield = nullptr;
        WorldObject* protectedObject = nullptr;
        glm::ivec3 impactCell(0);
        float earliestHit = std::numeric_limits<float>::max();
        for (WorldTreeGeode* candidate : candidates) {
            WorldObject* object = candidate->worldObject();
            Ship* ship = dynamic_cast<Ship*>(object);
            if (ship == nullptr ||
                ship->shieldType() == ShieldType::None ||
                !projectileObject->collisionFilter().isCollideableWith(
                    &object->collisionFilter())) {
                continue;
            }
            for (const std::shared_ptr<ShieldSlot>& slot :
                object->components().shieldSlots()) {
                if (!slot || !slot->shield()) {
                    continue;
                }
                glm::ivec3 candidateCell(0);
                float hitParameter = 0.0f;
                Shield& shield = *slot->shield();
                if (shield.intersectsSegment(
                        *ship,
                        ship->shieldType(),
                        start,
                        end,
                        projectileRadius,
                        candidateCell,
                        hitParameter) &&
                    hitParameter < earliestHit) {
                    hitShield = &shield;
                    protectedObject = object;
                    impactCell = candidateCell;
                    earliestHit = hitParameter;
                }
            }
        }
        if (hitShield == nullptr || !hitShield->registerHit()) {
            continue;
        }

        interceptedProjectiles.insert(projectileObject);
        const glm::vec3 impactPosition = glm::mix(start, end, earliestHit);
        const glm::vec3 localImpactPosition =
            protectedObject->transform().inverseApplyTo(impactPosition);
        Projectile* projectile =
            static_cast<Projectile*>(projectileObject);
        if (projectile->shotRecord) projectile->shotRecord->recordHit(protectedObject);
        const auto impactColor = projectile->impactColor();
        projectileObject->updateTransformAndGeode(
            impactPosition,
            projectileObject->orientation());
        projectile->onShieldImpact();
        if (m_shieldHitEventReplicationEnabled) {
            const auto entity = m_world.entityId(protectedObject);
            if (!entity) {
                throw std::logic_error(
                    "Shield impact owner has no server entity ID.");
            }
            if (m_shieldHitEvents.size() >= 4096) {
                throw std::overflow_error(
                    "Shield-hit event queue exceeded its valid limit.");
            }
            m_shieldHitEvents.push_back({
                *entity,
                m_world.simulationTickIndex(),
                impactCell,
                localImpactPosition,
                impactColor,
                1.0f
            });
        }
    }

    if (!interceptedProjectiles.empty()) {
        m_voxelCollisions.remove_if(
            [&interceptedProjectiles](VoxelCollision& collision) {
                return interceptedProjectiles.count(
                        collision.a().worldObject()) != 0 ||
                    interceptedProjectiles.count(
                        collision.b().worldObject()) != 0;
            });
    }
}
