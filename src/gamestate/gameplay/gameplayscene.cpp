#include "gameplayscene.h"
#include "scenarios/aicomparisonscenario.h"

#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <limits>
#include <unordered_map>
#include <unordered_set>

#include <glm/gtc/constants.hpp>
#include <GL/glew.h>
#include <glow/logging.h>
#include <glow/Program.h>

#include "benchmark/benchmarkprofiler.h"
#include "camera/camera.h"
#include "camera/camerahead.h"
#include "client/remotevoxelvisuals.h"

#include "display/rendering/starfield.h"
#include "display/rendering/skybox.h"
#include "display/rendering/worldtreerenderer.h"

#include "equipment/shield.h"
#include "equipment/shieldslot.h"
#include "equipment/shieldtype.h"
#include "equipment/weaponcatalog.h"
#include "gamestate/gameplay/gameplay.h"
#ifndef VOXELLANCER_HEADLESS_SERVER
#include "gamestate/gameplay/running/gameplayrunning.h"
#endif

#include "geometry/viewport.h"
#include "geometry/sphere.h"

#include "sound/soundmanager.h"

#include "ui/hud/hud.h"
#include "ui/hud/crosshair.h"
#ifndef VOXELLANCER_HEADLESS_SERVER
#include "ui/hud/hudoverlay.h"
#include "ui/contactmarker.h"
#include "ui/stationoutfitter.h"
#endif
#include "ui/uifont.h"
#include "ui/voxelfontconstants.h"

#include "voxel/voxelrenderer.h"
#include "voxel/voxelclusterbounds.h"
#include "voxeleffect/voxelparticleengine.h"
#include "voxeleffect/voxelparticlesetup.h"
#ifndef VOXELLANCER_HEADLESS_SERVER
#include "voxel/voxel.h"
#include "voxel/voxelcluster.h"
#include "voxeleffect/voxelexplosiongenerator.h"
#endif

#include "world/world.h"
#include "worldobject/ship.h"
#include "worldobject/worldobject.h"
#include "worldobject/worldobjectcomponents.h"
#include "worldobject/helper/hardpointaimhelper.h"

#include "player.h"

#ifndef VOXELLANCER_HEADLESS_SERVER
namespace {
    std::uint64_t remoteEntityKey(voxellancer::protocol::EntityId entity) {
        return (static_cast<std::uint64_t>(entity.index) << 32) |
            static_cast<std::uint64_t>(entity.generation);
    }
}

struct GamePlayScene::RemoteVoxelProxy {
    voxellancer::protocol::EntityId id;
    std::unique_ptr<VoxelCluster> cluster;
    std::vector<voxellancer::protocol::EntityVoxelState> voxelStates;
    std::vector<voxellancer::protocol::EntityEquipmentState> equipmentStates;
};
#endif

GamePlayScene::GamePlayScene(GamePlay& gamePlay):
    m_gamePlay(gamePlay),
    m_voxelRenderer(VoxelRenderer::instance()),
    m_starField(std::make_shared<Starfield>()),
    m_worldTreeRendererEnabled(false),
    m_worldTreeRenderer(new WorldTreeRenderer()),
    m_defaultLightDir("vfx.lightdir"),
    m_fps(0.0f)
#ifndef VOXELLANCER_HEADLESS_SERVER
    , m_hudOverlay(new HudOverlay())
#endif
{
#ifndef VOXELLANCER_HEADLESS_SERVER
    m_hudOverlay->setMinimized(true);
    m_hudOverlay->setVisible(true);
    m_hudOverlay->setFocusChangedCallback(
        [this]() { m_gamePlay.refreshMouseCursorMode(); });
    m_outfitter = std::make_unique<StationOutfitter>(
        [this](auto kind, auto weapon, auto cell, auto node, auto choice, auto shield) -> std::optional<std::uint64_t> {
            if (!m_gamePlay.running().submitContractAction(kind, weapon, cell, node, choice, shield)) return {};
            return m_gamePlay.running().contractActionSequence();
        });
#endif
    addRenderPass(m_starField, 0);
}

GamePlayScene::~GamePlayScene() = default;

void GamePlayScene::update(float deltaSec) {
    m_starField->update(deltaSec, m_gamePlay.cameraHead().position());
#ifndef VOXELLANCER_HEADLESS_SERVER
    updateRemoteVoxelProxies();
    updateRemoteInfoOverlay();
    const auto actor = m_gamePlay.running().remotePreviewSnapshot();
    const auto& contents = m_gamePlay.running().remoteEntityContents();
    const auto content = std::find_if(contents.begin(), contents.end(),
        [&](const auto& entry) { return actor && entry.id == actor->id; });
    const auto contract = m_gamePlay.running().contractStatus();
    const bool servicesWereVisible = m_outfitter->visible();
    m_outfitter->update(contract, content == contents.end() ? nullptr : &*content, deltaSec);
    if (contract && contract->phase != voxellancer::protocol::ContractPhase::Mission) {
        m_hudOverlay->setVisible(false);
        m_gamePlay.setMouseCursorFree(m_outfitter->visible());
    } else if (servicesWereVisible) {
        m_gamePlay.setMouseCursorFree(false);
    }
    updateShieldShimmer(deltaSec);
#endif
}

#ifndef VOXELLANCER_HEADLESS_SERVER
bool GamePlayScene::stationKey(int key) {
    const bool captured = m_outfitter->key(key);
    if (captured) m_gamePlay.setMouseCursorFree(m_outfitter->visible());
    return captured;
}

bool GamePlayScene::stationMouse(int button, int action, glm::vec2 pointer) {
    const bool captured = m_outfitter->mouse(button, action, pointer);
    if (captured) m_gamePlay.setMouseCursorFree(m_outfitter->visible());
    return captured;
}

bool GamePlayScene::closeEquipment() {
    return m_outfitter->closeEquipment();
}

void GamePlayScene::updateRemoteVoxelProxies() {
    BenchmarkCpuScope updateScope(BenchmarkMetric::ClientReplicaUpdate);
    GamePlayRunning& running = m_gamePlay.running();
    const auto& snapshots = running.remoteReplicaSnapshots();
    const auto& contents = running.remoteEntityContents();
    const bool remotePreviewActive = running.remotePreviewActive();
    const bool syncAllContents = m_remoteContentSyncNeeded ||
        (remotePreviewActive && !m_remotePreviewWasActive);
    if (remotePreviewActive && !m_remotePreviewWasActive) {
        m_remoteVoxelProxies.clear();
        m_remoteVoxelProxyIndex.clear();
        m_pendingRemoteContentKeys.clear();
    }
    m_remotePreviewWasActive = remotePreviewActive;
    m_remoteContentSyncNeeded = !remotePreviewActive;

    std::unordered_map<std::uint64_t, const voxellancer::protocol::EntitySnapshot*>
        snapshotsById;
    snapshotsById.reserve(snapshots.size());
    for (const auto& snapshot : snapshots) {
        snapshotsById.emplace(remoteEntityKey(snapshot.id), &snapshot);
    }

    const auto updateProxyContent = [this, &snapshotsById](
        const voxellancer::protocol::EntityContentSnapshot& content)
    {
        const std::uint64_t key = remoteEntityKey(content.id);
        if (snapshotsById.find(key) == snapshotsById.end()) {
            return;
        }

        auto proxyEntry = m_remoteVoxelProxyIndex.find(key);
        if (proxyEntry == m_remoteVoxelProxyIndex.end()) {
            auto created = std::make_unique<RemoteVoxelProxy>();
            created->id = content.id;
            RemoteVoxelProxy* proxy = created.get();
            m_remoteVoxelProxies.push_back(std::move(created));
            proxyEntry = m_remoteVoxelProxyIndex.emplace(key, proxy).first;
        }
        RemoteVoxelProxy& proxy = *proxyEntry->second;

        bool rebuildCluster = !proxy.cluster ||
            proxy.voxelStates.size() != content.voxels.size() ||
            proxy.equipmentStates != content.equipment;
        if (!rebuildCluster) {
            for (std::size_t index = 0; index < content.voxels.size(); ++index) {
                const auto& previous = proxy.voxelStates[index];
                const auto& current = content.voxels[index];
                Voxel* proxyVoxel = proxy.cluster->voxel(current.cell);
                if (previous.cell != current.cell ||
                    previous.color != current.color ||
                    previous.density != current.density ||
                    previous.maxHP != current.maxHP ||
                    current.hp > previous.hp ||
                    proxyVoxel == nullptr) {
                    rebuildCluster = true;
                    break;
                }
            }
        }

        if (rebuildCluster) {
            auto cluster = std::make_unique<VoxelCluster>(1.0f);
            for (const auto& voxel : content.voxels) {
                const Visuals visuals =
                    remoteVoxelVisuals(voxel, content.equipment);
                Voxel* proxyVoxel = new Voxel(
                    voxel.cell,
                    visuals.color(),
                    voxel.density,
                    voxel.maxHP,
                    visuals.emissiveness());
                proxyVoxel->applyDamage(voxel.maxHP - voxel.hp);
                cluster->addVoxel(proxyVoxel);
            }
            proxy.cluster = std::move(cluster);
        } else {
            for (const auto& voxel : content.voxels) {
                Voxel* proxyVoxel = proxy.cluster->voxel(voxel.cell);
                if (voxel.hp < proxyVoxel->hp()) {
                    proxyVoxel->applyDamage(proxyVoxel->hp() - voxel.hp);
                }
            }
        }
        proxy.voxelStates = content.voxels;
        proxy.equipmentStates = content.equipment;
    };

    const auto changedContents = running.drainRemoteEntityContentChanges();
    std::unordered_set<std::uint64_t> removedContentIds;
    if (!remotePreviewActive) {
        m_pendingRemoteContentKeys.clear();
    } else {
        if (syncAllContents) {
            m_pendingRemoteContentKeys.clear();
            for (const auto& content : contents) {
                m_pendingRemoteContentKeys.insert(remoteEntityKey(content.id));
            }
        } else {
            for (const auto id : changedContents) {
                m_pendingRemoteContentKeys.insert(remoteEntityKey(id));
            }
        }
    }
    if (!m_pendingRemoteContentKeys.empty()) {
        std::unordered_map<std::uint64_t,
            const voxellancer::protocol::EntityContentSnapshot*> contentsById;
        contentsById.reserve(contents.size());
        for (const auto& content : contents) {
            contentsById.emplace(remoteEntityKey(content.id), &content);
        }
        for (auto pending = m_pendingRemoteContentKeys.begin();
             pending != m_pendingRemoteContentKeys.end();) {
            const std::uint64_t key = *pending;
            const auto content = contentsById.find(key);
            if (content == contentsById.end()) {
                removedContentIds.insert(key);
                pending = m_pendingRemoteContentKeys.erase(pending);
                continue;
            }
            if (snapshotsById.find(key) == snapshotsById.end()) {
                ++pending;
                continue;
            }
            updateProxyContent(*content->second);
            pending = m_pendingRemoteContentKeys.erase(pending);
        }
    }

    for (const std::uint64_t key : removedContentIds) {
        m_remoteVoxelProxyIndex.erase(key);
    }
    m_remoteVoxelProxies.erase(
        std::remove_if(
            m_remoteVoxelProxies.begin(),
            m_remoteVoxelProxies.end(),
            [&removedContentIds](const auto& proxy) {
                return removedContentIds.find(remoteEntityKey(proxy->id)) !=
                    removedContentIds.end();
            }),
        m_remoteVoxelProxies.end());

    m_remoteVoxelProxies.erase(
        std::remove_if(
            m_remoteVoxelProxies.begin(),
            m_remoteVoxelProxies.end(),
            [this, &snapshotsById](const auto& proxy) {
                const std::uint64_t key = remoteEntityKey(proxy->id);
                const auto snapshot = snapshotsById.find(key);
                if (snapshot == snapshotsById.end()) {
                    m_remoteVoxelProxyIndex.erase(key);
                    return true;
                }
                BenchmarkProfiler::instance().addCounter(
                    BenchmarkCounter::RemoteVoxelProxiesUpdated);
                if (proxy->cluster) {
                    proxy->cluster->transform().setPosition(
                        snapshot->second->position);
                    proxy->cluster->transform().setOrientation(
                        snapshot->second->orientation);
                    proxy->cluster->transform().setScale(
                        snapshot->second->scale);
                }
                return false;
            }),
        m_remoteVoxelProxies.end());

    const auto damageEvents =
        running.drainRemoteVoxelDamageEvents();
    constexpr std::size_t MaxVoxelDamageFlashesPerFrame = 64;
    const std::size_t firstDamageEvent = damageEvents.size() >
        MaxVoxelDamageFlashesPerFrame
        ? damageEvents.size() - MaxVoxelDamageFlashesPerFrame
        : 0;
    for (std::size_t index = firstDamageEvent;
         index < damageEvents.size(); ++index) {
        const auto& event = damageEvents[index];
        const auto snapshot = snapshotsById.find(remoteEntityKey(event.entity));
        if (snapshot == snapshotsById.end()) {
            continue;
        }

        const auto& state = *snapshot->second;
        const glm::vec3 impactPosition = state.position +
            state.orientation * (glm::vec3(event.cell) * state.scale);
        VoxelExplosionGenerator flash(nullptr, World::instance());
        flash.setPosition(impactPosition);
        flash.setRadius(state.scale * 0.02f);
        flash.setScale(state.scale * 0.04f);
        flash.setForce(0.01f);
        flash.setLifetime(0.12f);
        flash.setCount(2);
        flash.setColor(0xff6644);
        flash.setEmissiveness(0.7f);
        flash.spawn();
    }

    const auto shieldHitEvents = running.drainShieldHitEvents();
    constexpr std::size_t MaxShieldHitFlashesPerFrame = 8;
    const std::size_t firstShieldHitEvent = shieldHitEvents.size() >
        MaxShieldHitFlashesPerFrame
        ? shieldHitEvents.size() - MaxShieldHitFlashesPerFrame
        : 0;
    for (std::size_t index = firstShieldHitEvent;
         index < shieldHitEvents.size(); ++index) {
        const auto& event = shieldHitEvents[index];
        glm::vec3 center(0.0f);
        glm::vec3 impactPosition(0.0f);
        float scale = 1.0f;
        float shieldRadius = 8.0f;
        if (running.remotePreviewActive()) {
            const auto snapshot =
                snapshotsById.find(remoteEntityKey(event.entity));
            if (snapshot == snapshotsById.end()) {
                continue;
            }

            const auto& state = *snapshot->second;
            center = state.position;
            impactPosition = state.position + state.orientation *
                (event.localImpactPosition * state.scale);
            scale = state.scale;
            const auto proxy = m_remoteVoxelProxyIndex.find(
                remoteEntityKey(event.entity));
            if (proxy != m_remoteVoxelProxyIndex.end() &&
                proxy->second->cluster) {
                shieldRadius = proxy->second->cluster->bounds().sphere(
                    proxy->second->cluster->transform()).radius();
            }
        } else {
            WorldObject* impactedObject =
                World::instance()->worldObject(event.entity);
            Ship* ship = dynamic_cast<Ship*>(impactedObject);
            if (ship == nullptr) {
                continue;
            }

            const Sphere shipSphere =
                ship->bounds().sphere(ship->transform());
            center = shipSphere.position();
            impactPosition = ship->transform().applyTo(
                event.localImpactPosition);
            scale = ship->transform().scale();
            shieldRadius = shipSphere.radius();
        }

        glm::vec3 normal = impactPosition - center;
        if (glm::length(normal) < 0.001f) {
            normal = glm::vec3(0.0f, 1.0f, 0.0f);
        } else {
            normal = glm::normalize(normal);
        }
        glm::vec3 tangent = glm::cross(normal, glm::vec3(0.0f, 1.0f, 0.0f));
        if (glm::length(tangent) < 0.001f) {
            tangent = glm::cross(normal, glm::vec3(1.0f, 0.0f, 0.0f));
        }
        tangent = glm::normalize(tangent);
        const glm::vec3 bitangent = glm::normalize(glm::cross(normal, tangent));
        constexpr int RippleParticleCount = 12;
        const auto mixChannel = [](std::uint32_t first, std::uint32_t second) {
            return (first + second) / 2;
        };
        const std::uint32_t red = mixChannel(
            (0x3388ffu >> 16) & 0xffu,
            (event.impactColor >> 16) & 0xffu);
        const std::uint32_t green = mixChannel(
            (0x3388ffu >> 8) & 0xffu,
            (event.impactColor >> 8) & 0xffu);
        const std::uint32_t blue = mixChannel(
            0x3388ffu & 0xffu,
            event.impactColor & 0xffu);
        const std::uint32_t rippleColor =
            (red << 16) | (green << 8) | blue;
        for (int particle = 0; particle < RippleParticleCount; ++particle) {
            const float angle = glm::two_pi<float>() * particle /
                RippleParticleCount;
            const glm::vec3 direction =
                tangent * std::cos(angle) + bitangent * std::sin(angle);
            Transform particleTransform;
            particleTransform.setPosition(
                impactPosition + direction * shieldRadius * 0.015f);
            particleTransform.setScale(
                std::max(scale * 0.1f, shieldRadius * 0.004f));
            const Visuals visuals(rippleColor, 0.9f);
            const Speed speed(
                direction * std::max(scale * 1.6f, shieldRadius * 0.35f),
                glm::vec3(0.0f));
            const VoxelParticleSetup rippleParticle(
                particleTransform, visuals, speed, 0.26f);
            World::instance()->particleEngine().addParticle(
                rippleParticle, nullptr);
        }
    }

    const auto missionEvents =
        running.drainRemoteMissionStateEvents();
    if (!missionEvents.empty()) {
        const auto& event = missionEvents.back();
        const char* state = "STARTED";
        switch (event.state) {
        case voxellancer::protocol::MissionEventState::Failed:
            state = "FAILED";
            break;
        case voxellancer::protocol::MissionEventState::Succeeded:
            state = "SUCCEEDED";
            break;
        case voxellancer::protocol::MissionEventState::Started:
            break;
        }
        World::instance()->player().hud().showMessage(
            std::string("MISSION ") + state + ": " + event.missionKey);
    }

    const auto weaponEvents =
        running.drainRemoteWeaponFiredEvents();
    constexpr std::size_t MaxWeaponMuzzleFlashesPerFrame = 32;
    const std::size_t firstEvent = weaponEvents.size() >
        MaxWeaponMuzzleFlashesPerFrame
        ? weaponEvents.size() - MaxWeaponMuzzleFlashesPerFrame
        : 0;
    for (std::size_t index = firstEvent;     index < weaponEvents.size(); ++index) {
        const auto& event = weaponEvents[index];
        const auto snapshot = snapshotsById.find(remoteEntityKey(event.entity));
        if (snapshot == snapshotsById.end()) {
       continue;
        }

        const auto& state = *snapshot->second;
        const glm::vec3 firingPosition = state.position +
       state.orientation * (glm::vec3(event.cell) * state.scale);
        VoxelExplosionGenerator flash(nullptr, World::instance());
        flash.setPosition(firingPosition);
        flash.setRadius(0.03f);
        flash.setScale(0.05f);
        flash.setForce(0.02f);
        flash.setLifetime(0.08f);
        flash.setCount(1);
        flash.setColor(0xffdd88);
        flash.setEmissiveness(0.8f);
        flash.spawn();
    }
}

void GamePlayScene::updateShieldShimmer(float deltaSec) {
    constexpr float shimmerInterval = 0.1f;
    constexpr std::size_t maxShimmersPerUpdate = 24;
    constexpr std::size_t maxPulseParticlesPerShip = 8;
    constexpr float maxDistance = 1600.0f;
    constexpr float pulsePeriod = 6.0f;
    constexpr float pulseDuration = 2.0f;
    constexpr float pulseHalfWidth = 0.8f;
    constexpr glm::ivec3 surfaceNeighbors[] = {
        glm::ivec3(-1, 0, 0),
        glm::ivec3(1, 0, 0),
        glm::ivec3(0, -1, 0),
        glm::ivec3(0, 1, 0),
        glm::ivec3(0, 0, -1),
        glm::ivec3(0, 0, 1)
    };

    m_shieldPulseTime += deltaSec;
    m_shieldShimmerAccumulator += deltaSec;
    if (m_shieldShimmerAccumulator < shimmerInterval) {
        return;
    }
    m_shieldShimmerAccumulator = std::fmod(
        m_shieldShimmerAccumulator, shimmerInterval);

    World& world = *World::instance();
    const glm::vec3 viewerPosition = m_gamePlay.cameraHead().position();
    const float maxDistanceSquared = maxDistance * maxDistance;
    const float pulsePhase = std::fmod(m_shieldPulseTime, pulsePeriod);
    std::size_t emitted = 0;
    std::unordered_set<const Ship*> activeShips;
    activeShips.reserve(world.ships().size());
    for (Ship* ship : world.ships()) {
        activeShips.insert(ship);
        if (emitted >= maxShimmersPerUpdate ||
            ship->shieldType() == ShieldType::None) {
            continue;
        }

        Shield* activeShield = nullptr;
        for (const auto& slot : ship->components().shieldSlots()) {
            if (slot && slot->shield() && !slot->shield()->depleted()) {
                activeShield = slot->shield().get();
                break;
            }
        }
        if (activeShield == nullptr) {
            continue;
        }

        const Sphere boundsSphere = ship->bounds().sphere(ship->transform());
        const glm::vec3 fromViewer =
            boundsSphere.position() - viewerPosition;
        if (glm::dot(fromViewer, fromViewer) > maxDistanceSquared) {
            continue;
        }

        if (ship->shieldType() == ShieldType::Hull &&
            pulsePhase < pulseDuration) {
            ShieldSurfaceCache& cache = m_shieldSurfaceCache[ship];
            const std::uint64_t voxelRevision = ship->voxelRevision();
            if (!cache.initialized ||
                cache.voxelRevision != voxelRevision) {
                cache.points.clear();
                cache.minimumX = std::numeric_limits<int>::max();
                cache.maximumX = std::numeric_limits<int>::min();
                for (const auto& entry : ship->voxelMap()) {
                    cache.minimumX = std::min(
                        cache.minimumX, entry.first.x);
                    cache.maximumX = std::max(
                        cache.maximumX, entry.first.x);
                    for (const glm::ivec3& neighbor : surfaceNeighbors) {
                        if (ship->voxel(entry.first + neighbor) == nullptr) {
                            cache.points.push_back(
                                {entry.first, neighbor});
                            break;
                        }
                    }
                }
                std::sort(
                    cache.points.begin(),
                    cache.points.end(),
                    [](const ShieldSurfacePoint& left,
                       const ShieldSurfacePoint& right) {
                        if (left.cell.x != right.cell.x) {
                            return left.cell.x < right.cell.x;
                        }
                        if (left.cell.y != right.cell.y) {
                            return left.cell.y < right.cell.y;
                        }
                        return left.cell.z < right.cell.z;
                    });
                cache.voxelRevision = voxelRevision;
                cache.initialized = true;
            }

            if (!cache.points.empty()) {
                const float xSpan = static_cast<float>(
                    cache.maximumX - cache.minimumX);
                const float waveX = static_cast<float>(cache.minimumX) -
                    pulseHalfWidth +
                    (xSpan + 2.0f * pulseHalfWidth) *
                    (pulsePhase / pulseDuration);
                const auto first = std::lower_bound(
                    cache.points.begin(),
                    cache.points.end(),
                    waveX - pulseHalfWidth,
                    [](const ShieldSurfacePoint& point, float x) {
                        return static_cast<float>(point.cell.x) < x;
                    });
                const auto last = std::upper_bound(
                    first,
                    cache.points.end(),
                    waveX + pulseHalfWidth,
                    [](float x, const ShieldSurfacePoint& point) {
                        return x < static_cast<float>(point.cell.x);
                    });
                const std::size_t pointCount =
                    static_cast<std::size_t>(last - first);
                const std::size_t pulseCount = std::min({
                    maxPulseParticlesPerShip,
                    maxShimmersPerUpdate - emitted,
                    pointCount
                });
                const glm::vec3 worldOrigin =
                    ship->transform().applyTo(glm::vec3(0.0f));
                for (std::size_t pulse = 0;
                     pulse < pulseCount && pointCount > 0; ++pulse) {
                    const std::size_t pointIndex = pulseCount == 1
                        ? 0
                        : pulse * (pointCount - 1) / (pulseCount - 1);
                    const ShieldSurfacePoint& point =
                        *(first + pointIndex);
                    const glm::vec3 normal = glm::normalize(
                        ship->transform().applyTo(
                            glm::vec3(point.normal)) - worldOrigin);
                    Transform particleTransform;
                    particleTransform.setPosition(
                        ship->transform().applyTo(
                            glm::vec3(point.cell) +
                            glm::vec3(point.normal) * 0.82f));
                    particleTransform.setScale(std::max(
                        ship->transform().scale() * 0.1f,
                        boundsSphere.radius() * 0.0025f));
                    const Visuals visuals(0x91dcff, 0.9f);
                    const Speed speed(
                        normal * ship->transform().scale() * 0.008f,
                        glm::vec3(0.0f));
                    const VoxelParticleSetup pulseParticle(
                        particleTransform, visuals, speed, 0.2f);
                    world.particleEngine().addParticle(
                        pulseParticle, nullptr);
                    ++emitted;
                }
            }
        }

        for (int glint = 0; glint < 2 &&
             emitted < maxShimmersPerUpdate; ++glint) {
            glm::vec3 position(0.0f);
            glm::vec3 normal(0.0f, 1.0f, 0.0f);
            if (ship->shieldType() == ShieldType::Sphere) {
                const float z = 2.0f * nextShieldShimmerRandom() - 1.0f;
                const float angle =
                    glm::two_pi<float>() * nextShieldShimmerRandom();
                const float radial = std::sqrt(
                    std::max(0.0f, 1.0f - z * z));
                normal = glm::vec3(
                    radial * std::cos(angle),
                    z,
                    radial * std::sin(angle));
                position = boundsSphere.position() + normal *
                    (boundsSphere.radius() +
                     ship->transform().scale() * 0.08f);
            } else {
                const auto& voxels = ship->voxelMap();
                if (voxels.empty()) {
                    break;
                }
                auto voxel = voxels.begin();
                const std::size_t startIndex =
                    static_cast<std::size_t>(
                        nextShieldShimmerRandom() * voxels.size());
                std::advance(voxel, startIndex);
                bool foundSurface = false;
                const std::size_t samples =
                    std::min<std::size_t>(voxels.size(), 32);
                for (std::size_t sample = 0; sample < samples &&
                     !foundSurface; ++sample) {
                    if (voxel == voxels.end()) {
                        voxel = voxels.begin();
                    }
                    const glm::ivec3 cell = voxel->first;
                    const std::size_t firstFace =
                        static_cast<std::size_t>(
                            nextShieldShimmerRandom() * 6.0f);
                    for (std::size_t face = 0; face < 6; ++face) {
                        const glm::ivec3& neighbor =
                            surfaceNeighbors[(firstFace + face) % 6];
                        if (ship->voxel(cell + neighbor) != nullptr) {
                            continue;
                        }
                        const glm::vec3 localNormal(neighbor);
                        const glm::vec3 worldOrigin =
                            ship->transform().applyTo(glm::vec3(0.0f));
                        normal = glm::normalize(
                            ship->transform().applyTo(localNormal) -
                            worldOrigin);
                        position = ship->transform().applyTo(
                            glm::vec3(cell) +
                            localNormal * 0.82f);
                        foundSurface = true;
                        break;
                    }
                    ++voxel;
                }
                if (!foundSurface) {
                    break;
                }
            }

            Transform particleTransform;
            particleTransform.setPosition(position);
            particleTransform.setScale(std::max(
                ship->transform().scale() * 0.1f,
                boundsSphere.radius() * 0.0025f));
            const Visuals visuals(
                0x66ccff, 0.25f + nextShieldShimmerRandom() * 0.2f);
            const Speed speed(
                normal * ship->transform().scale() * 0.015f,
                glm::vec3(0.0f));
            const VoxelParticleSetup shimmer(
                particleTransform, visuals, speed, 0.16f);
            world.particleEngine().addParticle(shimmer, nullptr);
            ++emitted;
        }
    }

    for (auto cache = m_shieldSurfaceCache.begin();
         cache != m_shieldSurfaceCache.end();) {
        if (activeShips.find(cache->first) == activeShips.end()) {
            cache = m_shieldSurfaceCache.erase(cache);
        } else {
            ++cache;
        }
    }
}

float GamePlayScene::nextShieldShimmerRandom() {
    m_shieldShimmerRandomState =
        1664525u * m_shieldShimmerRandomState + 1013904223u;
    return static_cast<float>(m_shieldShimmerRandomState >> 8) /
        16777216.0f;
}

std::optional<voxellancer::protocol::EntityId> GamePlayScene::remoteLeadTarget() const {
    const auto& actor = m_gamePlay.running().remotePreviewSnapshot();
    if (!actor || !m_remoteLeadCue || m_remoteLeadCue->actor != actor->id ||
        m_gamePlay.freecamActive() || !Property<bool>("vfx.drawhud")) {
        return std::nullopt;
    }
    auto& player = World::instance()->player();
    // Both the cue and the crosshair are tested as viewing directions.
    const glm::vec3 origin = player.cameraHead().position();
    const glm::vec3 cue = m_remoteLeadCue->point - origin;
    const glm::vec3 crosshair = player.hud().crossHair().worldPosition() - origin;
    const float alignment = glm::dot(cue, crosshair);
    constexpr float cosTolerance = 0.9997f; // About 1.4 degrees.
    if (alignment > 0 && alignment * alignment >=
        cosTolerance * cosTolerance * glm::dot(cue, cue) * glm::dot(crosshair, crosshair)) {
        return m_remoteLeadCue->target;
    }
    return std::nullopt;
}

void GamePlayScene::drawRemoteContacts(const Camera& camera) const {
    m_remoteLeadCue.reset();
    if (!Property<bool>("vfx.drawhud")) {
        return;
    }
    const auto actor = m_gamePlay.running().remotePreviewSnapshot();
    if (!actor) {
        return;
    }
    auto* font = UiFont::instance();
    const glm::vec4 color(1.0f, 0.32f, 0.22f, 1.0f);
    const auto contract = m_gamePlay.running().contractStatus();
    const bool showLead = !m_gamePlay.freecamActive() &&
        (!contract || (contract->alive &&
            contract->phase == voxellancer::protocol::ContractPhase::Mission));
    struct GunCue {
        glm::vec3 muzzle;
        float speed;
        float lifetime;
        float range;
        bool inheritsVelocity;
    };
    std::vector<GunCue> guns;
    if (showLead) {
        const auto& contents = m_gamePlay.running().remoteEntityContents();
        const auto content = std::find_if(contents.begin(), contents.end(),
            [&actor](const auto& entry) { return entry.id == actor->id; });
        if (content != contents.end()) {
            for (const auto& slot : content->equipment) {
                if (slot.kind != voxellancer::protocol::EquipmentSlotKind::Weapon ||
                    Property<std::string>::get(slot.equipmentKey + ".general.type") != "gun") {
                    continue;
                }
                const std::string bullet = Property<std::string>(slot.equipmentKey + ".general.bullet");
                const float speed = Property<float>(bullet + ".general.speed");
                const float lifetime = Property<float>(bullet + ".general.lifetime");
                const auto profile = WeaponProfile::load(bullet);
                guns.push_back({actor->position + actor->orientation * (glm::vec3(slot.cell) * actor->scale),
                    speed, lifetime, profile.enabled ? profile.range : speed * lifetime, profile.enabled});
            }
        }
    }
    float nearestLead = std::numeric_limits<float>::infinity();
    glm::vec2 cueScreen(0.0f);
    auto& player = World::instance()->player();
    const glm::vec3 origin = player.cameraHead().position();
    const glm::vec3 crosshairRay = glm::normalize(player.hud().crossHair().worldPosition() - origin);
    const auto& contents = m_gamePlay.running().remoteEntityContents();
    using SlotKind = voxellancer::protocol::EquipmentSlotKind;
    for (const auto& contact : m_gamePlay.running().remoteReplicaSnapshots()) {
        if (!contact.hostile || contact.id == actor->id ||
            contact.kind != voxellancer::protocol::EntitySnapshot::Kind::Ship) {
            continue;
        }
        const auto marker = projectContactMarker(
            camera.viewProjection() * glm::vec4(contact.position, 1.0f));
        if (marker.onScreen) {
            font->drawScreen("(", marker.position + glm::vec2(-0.02f, -0.014f),
                0.028f, color, FontAlign::CENTER);
            font->drawScreen(")", marker.position + glm::vec2(0.02f, -0.014f),
                0.028f, color, FontAlign::CENTER);
        } else {
            font->drawScreen("!", marker.position - glm::vec2(0.0f, 0.018f),
                0.028f, color, FontAlign::CENTER);
        }
        const auto distance = std::lround(glm::distance(actor->position, contact.position));
        const auto content = std::find_if(contents.begin(), contents.end(),
            [&contact](const auto& entry) { return entry.id == contact.id; });
        const auto root = content != contents.end() ? content->definitionKey + ".general." : std::string();
        const auto name = root.empty() ? std::string("ENEMY") :
            Property<std::string>::get(root + "contactName", "ENEMY");
        const std::string label = name + (marker.behind ? " BEHIND - " : " - ") +
            std::to_string(distance) + " U";
        font->drawScreen(label,
            glm::vec2(glm::clamp(marker.position.x, 0.15f, 0.85f),
                marker.position.y + 0.02f),
            0.017f, color, FontAlign::CENTER, 0.28f);

        if (showLead && marker.onScreen && distance < 450 && !root.empty()) {
            const int projector = Property<int>::get(root + "shieldProjectorGroup", -1);
            if (projector >= 0) {
                const bool projectorOnline = std::any_of(content->equipment.begin(), content->equipment.end(),
                    [](const auto& slot) { return slot.kind == SlotKind::Shield && !slot.equipmentKey.empty(); });
                const glm::vec4 moduleColor(1.0f,0.82f,0.38f,1.0f);
                for (const auto& slot : content->equipment) {
                    if (slot.kind != SlotKind::Weapon || slot.equipmentKey.empty()) continue;
                    const auto point = contact.position + contact.orientation *
                        (glm::vec3(slot.cell) * contact.scale);
                    const auto module = projectContactMarker(camera.viewProjection() * glm::vec4(point, 1));
                    if (!module.onScreen) continue;
                    const bool isProjector = slot.group == projector && projectorOnline;
                    font->drawScreen(isProjector ? "P" : "o", module.position - glm::vec2(0,0.009f),
                        0.018f, moduleColor, FontAlign::CENTER);
                }
                font->drawScreen(projectorOnline ? "P: PROJECTOR / o: GUN" : "PROJECTOR OFFLINE",
                    marker.position + glm::vec2(0,0.04f), 0.013f, moduleColor, FontAlign::CENTER);
            }
        }

        glm::vec3 sum(0.0f);
        int solutions = 0;
        for (const auto& gun : guns) {
            const auto point = HardpointAimHelper::interceptPoint(gun.muzzle,
                contact.position, contact.directionalVelocity -
                    (gun.inheritsVelocity ? actor->directionalVelocity : glm::vec3(0.0f)),
                gun.speed, gun.lifetime, gun.range);
            if (point) {
                sum += *point;
                ++solutions;
            }
        }
        if (solutions == 0) continue;
        const glm::vec3 point = sum / static_cast<float>(solutions);
        const auto lead = projectContactMarker(camera.viewProjection() * glm::vec4(point, 1.0f));
        if (!lead.onScreen) continue;
        const glm::vec3 direction = glm::normalize(point - origin);
        const float leadDistance = 1.0f - glm::dot(direction, crosshairRay);
        if (leadDistance < nearestLead) {
            nearestLead = leadDistance;
            cueScreen = lead.position;
            m_remoteLeadCue = RemoteLeadCue{actor->id, contact.id, point};
        }
    }
    if (m_remoteLeadCue) {
        const bool linedUp = remoteLeadTarget().has_value();
        font->drawScreen(linedUp ? "(+)" : "+", cueScreen - glm::vec2(0.0f, 0.012f),
            0.024f, glm::vec4(0.56f, 0.95f, 1.0f, 1.0f), FontAlign::CENTER);
        if (contract && contract->run == 1 && contract->attacksCleared == 0) {
            font->drawScreen(linedUp ? "LEAD ALIGNED - FIRE" : "LINE UP THE CYAN + TO LEAD",
                glm::vec2(0.5f, 0.73f), 0.017f, glm::vec4(0.56f, 0.95f, 1.0f, 1.0f),
                FontAlign::CENTER);
        }
    }
}

void GamePlayScene::updateRemoteInfoOverlay() {
    if (m_gamePlay.running().contractSessionEnded()) {
        m_hudOverlay->setPrimaryAction("", false, {});
        m_hudOverlay->setVisible(true);
        m_hudOverlay->setMinimized(false);
        m_hudOverlay->setTextLines({
            "CONTRACT SESSION ENDED",
            "YOUR SHIP WAS LOST OR THE SERVER DISCONNECTED",
            "CLOSE THE GAME AND START A NEW SESSION"
        });
        return;
    }
    const auto contract = m_gamePlay.running().contractStatus();
    if (contract) {
        using namespace voxellancer::protocol;
        const bool station = contract->phase == ContractPhase::Station;
        if (!m_contractOverlayActive || m_contractWasStation != station) {
            m_hudOverlay->setVisible(station);
            m_hudOverlay->setMinimized(false);
            m_gamePlay.setMouseCursorFree(station);
            m_hudOverlay->setBounds(station
                ? glm::vec4(0.025f, 0.42f, 0.68f, 0.55f)
                : glm::vec4(0.025f, 0.69f, 0.38f, 0.28f));
        }
        m_contractOverlayActive = true;
        m_contractWasStation = station;
        m_hudOverlay->setPrimaryAction(station
            ? contract->ready ? "WAITING FOR PARTY" : "LAUNCH - ENTER" : "",
            station && !contract->ready, [this]() {
                m_gamePlay.running().submitContractAction(ContractActionKind::Ready);
            });
        std::vector<std::string> lines;
        lines.push_back(station ? "STATION - CONTRACT BOARD AND OUTFITTER" : "ATTACK - CLEAR HOSTILES");
        lines.push_back("CREDITS " + std::to_string(contract->credits) +
            "   ROUTE STEP " + std::to_string(contract->selectedNode < contract->route.size()
                ? contract->route[contract->selectedNode].row : 0));
        lines.push_back(contract->invulnerable
            ? "INVULNERABILITY ON - CHANGE IN ESC / SETTINGS"
            : "INVULNERABILITY OFF - CHANGE IN ESC / SETTINGS");
        if (station) {
            lines.push_back("CLEAR RAIDERS - REWARD " + std::to_string(contract->reward) + " EACH");
            lines.push_back("B - RANDOM WEAPON OFFER  " +
                (contract->gun ? std::string("SOLD") : std::to_string(contract->gunPrice) + " CREDITS"));
            lines.push_back("E - EQUIPMENT AND STORED WEAPONS");
            lines.push_back("N - SHIELD MK2  " +
                (contract->shield ? std::string("OWNED") : std::to_string(contract->shieldPrice) + " CREDITS") +
                " / 1.5X CAPACITY + REGEN");
            lines.push_back(contract->ready ? "READY - WAITING FOR YOUR PARTY" : "ENTER - READY AND DEPLOY");
            lines.push_back("PARTY READY " + std::to_string(contract->readyPlayers) +
                "/" + std::to_string(contract->players));
        } else {
            lines.push_back("HOSTILES REMAINING " + std::to_string(contract->remaining));
            lines.push_back("REWARD " + std::to_string(contract->reward) + " CREDITS PER PILOT");
            lines.push_back("CLEAR ALL HOSTILES TO RETURN TO THE MAP");
        }
        const char* result = "";
        switch (contract->result) {
        case ContractResult::None: break;
        case ContractResult::Accepted: result = "REQUEST ACCEPTED"; break;
        case ContractResult::WrongPhase: result = "SHOP AVAILABLE ONLY AT STATION"; break;
        case ContractResult::InsufficientCredits: result = "NOT ENOUGH CREDITS - COMPLETE A CONTRACT"; break;
        case ContractResult::AlreadyOwned: result = "UPGRADE ALREADY OWNED"; break;
        case ContractResult::Incompatible: result = "NO COMPATIBLE EQUIPMENT SLOTS"; break;
        case ContractResult::StaleRequest: result = "SESSION CHANGED - PLEASE TRY AGAIN"; break;
        case ContractResult::Unavailable: result = "PLAYER UNAVAILABLE"; break;
        case ContractResult::MissionCompleted: result = "CONTRACT COMPLETE - CREDITS AWARDED"; break;
        case ContractResult::MissionAborted: result = "CONTRACT ABORTED - NO REWARD"; break;
        case ContractResult::InventoryFull: result = "INVENTORY FULL"; break;
        case ContractResult::InvalidSlot: result = "MOUNT NO LONGER AVAILABLE"; break;
        case ContractResult::InvalidRoute: result = "CHOOSE A CONNECTED DESTINATION"; break;
        case ContractResult::HullIntact: result = "NO SURVIVING HULL BLOCKS NEED REPAIR"; break;
        case ContractResult::CheckpointFailed: result = "CHECKPOINT SAVE FAILED"; break;
        }
        lines.push_back(result);
        lines.push_back("TAB - HIDE / SHOW PANEL   ESC - MENU");
        m_hudOverlay->setTextLines(lines);
        return;
    }
    m_contractOverlayActive = false;
    m_hudOverlay->setPrimaryAction("", false, {});
    const auto& remoteSnapshot = m_gamePlay.running().remotePreviewSnapshot();
    if (!remoteSnapshot) {
        m_hudOverlay->setTextLines({});
        return;
    }

    std::vector<std::string> lines;
    std::ostringstream positionText;
    positionText << "SERVER X " << std::fixed << std::setprecision(1)
        << remoteSnapshot->position.x << " Y " << remoteSnapshot->position.y;
    lines.push_back(positionText.str());

    std::ostringstream statusText;
    statusText << "Z " << std::fixed << std::setprecision(1)
        << remoteSnapshot->position.z << " SHIELD " << std::setprecision(0)
        << remoteSnapshot->shieldFraction * 100.0f
        << "% HULL " << remoteSnapshot->hullFraction * 100.0f << "%";
    lines.push_back(statusText.str());

    if (glm::length(remoteSnapshot->engineState) > 0.01f) {
        std::ostringstream engineText;
        engineText << "ENGINE X " << std::fixed << std::setprecision(0)
            << remoteSnapshot->engineState.x * 100.0f
            << "% Y " << remoteSnapshot->engineState.y * 100.0f
            << "% Z " << remoteSnapshot->engineState.z * 100.0f
            << "%";
        lines.push_back(engineText.str());
    }

    const auto& remoteContents = m_gamePlay.running().remoteEntityContents();
    const auto actorContent = std::find_if(
        remoteContents.begin(),
        remoteContents.end(),
        [&remoteSnapshot](const auto& content) {
            return content.id == remoteSnapshot->id;
        });
    if (actorContent != remoteContents.end()) {
        constexpr std::size_t MaxEquipmentRows = 6;
        const std::size_t visibleRows = std::min(
            actorContent->equipment.size(), MaxEquipmentRows);
        for (std::size_t index = 0; index < visibleRows; ++index) {
            const auto& equipment = actorContent->equipment[index];
            const char* kind = "ITEM";
            switch (equipment.kind) {
            case voxellancer::protocol::EquipmentSlotKind::Engine:
                kind = "ENGINE";
                break;
            case voxellancer::protocol::EquipmentSlotKind::Weapon:
                kind = "WEAPON";
                break;
            case voxellancer::protocol::EquipmentSlotKind::Shield:
                kind = "SHIELD";
                break;
            }
            constexpr std::size_t MaxDisplayedKeyLength = 24;
            const std::string key = equipment.equipmentKey.substr(
                0, MaxDisplayedKeyLength);
            std::ostringstream equipmentText;
            equipmentText << kind << " " << equipment.group << " " << key;
            lines.push_back(equipmentText.str());
        }
        if (actorContent->equipment.size() > visibleRows) {
            std::ostringstream remainingText;
            remainingText << "EQUIPMENT +"
                << actorContent->equipment.size() - visibleRows
                << " MORE";
            lines.push_back(remainingText.str());
        }
    }

    m_hudOverlay->setTextLines(lines);
}
#endif

bool GamePlayScene::worldTreeRendererEnabled() const {
    return m_worldTreeRendererEnabled;
}

void GamePlayScene::setWorldTreeRendererEnabled(bool enabled) {
    m_worldTreeRendererEnabled = enabled;
    glow::debug()<< "Drawing Worldtree " << enabled;
}

#ifndef VOXELLANCER_HEADLESS_SERVER
HudOverlay& GamePlayScene::hudOverlay() {
    return *m_hudOverlay;
}
#endif

void GamePlayScene::setFps(float fps) {
    m_fps = fps;
}

void GamePlayScene::drawImpl(const Camera& camera) const {
    {
        BenchmarkCpuScope cpuScope(BenchmarkMetric::SkyboxDraw);
        BenchmarkGpuScope gpuScope(BenchmarkMetric::SkyboxDraw);
        World::instance()->skybox().draw(camera);
    }

    m_voxelRenderer->program()->setUniform("lightdir", m_defaultLightDir.get());

    {
        BenchmarkCpuScope cpuScope(BenchmarkMetric::WorldObjectDraw);
        BenchmarkGpuScope gpuScope(BenchmarkMetric::WorldObjectDraw);
        m_voxelRenderer->prepareDraw(camera);
        UiFont::instance()->setCamera(camera);
        if (!m_gamePlay.running().remotePreviewActive()) {
            for (WorldObject* worldObject : World::instance()->worldObjects()) {
                VoxelRenderer::instance()->draw(*worldObject);
            }
        }
#ifndef VOXELLANCER_HEADLESS_SERVER
        for (const auto& proxy : m_remoteVoxelProxies) {
            if (proxy->cluster && proxy->cluster->voxelCount() > 0) {
                VoxelRenderer::instance()->draw(*proxy->cluster);
            }
        }
#endif
    }
    m_voxelRenderer->afterDraw();

    {
        BenchmarkCpuScope cpuScope(BenchmarkMetric::HudDraw);
        BenchmarkGpuScope gpuScope(BenchmarkMetric::HudDraw);
#ifndef VOXELLANCER_HEADLESS_SERVER
        const bool waitingForRemoteWorld = m_gamePlay.waitingForRemoteWorld();
#else
        const bool waitingForRemoteWorld = false;
#endif
        constexpr float overlayDistance = 8.0f;
        std::ostringstream fpsText;
        const GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
        glDisable(GL_DEPTH_TEST);
        m_voxelRenderer->prepareDraw(camera);
        if (depthTestEnabled) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        if (!waitingForRemoteWorld) {
#ifndef VOXELLANCER_HEADLESS_SERVER
            const auto contract = m_gamePlay.running().contractStatus();
            if (!m_gamePlay.aiComparison() && !m_outfitter->visible() &&
                (!contract || contract->alive))
#endif
                World::instance()->player().hud().draw();
            fpsText << "FPS: " << std::fixed << std::setprecision(1) << m_fps;
        }
        m_voxelRenderer->afterDraw();
        glDisable(GL_DEPTH_TEST);
        if (!waitingForRemoteWorld) {
            UiFont::instance()->drawScreen(
                fpsText.str(), glm::vec2(0.025f, 0.025f), 0.0225f);
        }
#ifndef VOXELLANCER_HEADLESS_SERVER
        if (!waitingForRemoteWorld) {
            drawRemoteContacts(camera);
        } else {
            m_remoteLeadCue.reset();
        }
        m_hudOverlay->drawBackground();
        m_hudOverlay->drawText(camera);
        if (const auto contract = m_gamePlay.running().contractStatus()) {
            if ((!m_hudOverlay->visible() || m_hudOverlay->minimized()) && !m_outfitter->visible()) {
                const bool station = contract->phase !=
                    voxellancer::protocol::ContractPhase::Mission;
                const bool summary = contract->phase == voxellancer::protocol::ContractPhase::Complete ||
                    contract->phase == voxellancer::protocol::ContractPhase::Defeat;
                const std::string prompt = summary ? "RUN ENDED / TAB DEBRIEF / ENTER NEW RUN / ESC MENU" :
                    !contract->alive ? "SHIP LOST - SPECTATING / NO REVIVES THIS RUN / TAB SERVICES" :
                    contract->phase == voxellancer::protocol::ContractPhase::Reward ?
                        "CHOOSE YOUR REWARD / TAB SALVAGE / 1 CREDITS / 2 WEAPON / 3 UPGRADE" : station
                    ? contract->ready
                        ? "READY - WAITING FOR PARTY / TAB ROUTE"
                        : "ENTER - ROUTE / TRAVEL / TAB SERVICES"
                    : "HOSTILES " + std::to_string(contract->remaining) +
                        " / TAB OBJECTIVES / ESC MENU";
                UiFont::instance()->drawScreen(prompt,
                    glm::vec2(0.5f, 0.08f), 0.02f,
                    glm::vec4(1.0f), FontAlign::CENTER, 0.9f);
            }
        }
        m_outfitter->draw();
        if (const auto* comparison = m_gamePlay.aiComparison()) {
            std::array<int,5> counts{};
            for (const auto& duel : comparison->duels())
                ++counts[static_cast<std::size_t>(duel.result)];
            const glm::vec4 cyan(0.25f,0.85f,0.95f,1);
            const auto& watched = comparison->duels()[comparison->watchedPair()];
            for (int side = 0; side < 2; ++side) {
                const auto* ship = watched.ships[side].get();
                if (!ship) continue;
                const auto marker = projectContactMarker(
                    camera.viewProjection()*glm::vec4(ship->position(),1));
                const glm::vec4 color = side == 0 ? glm::vec4(1,0.65f,0.25f,1) : cyan;
                UiFont::instance()->drawScreen(marker.onScreen ? "+" : "!",
                    marker.position,0.025f,color,FontAlign::CENTER);
                UiFont::instance()->drawScreen(
                    std::string(side == 0 ? "A " : "B ")+
                    AiComparisonScenario::behaviourName(watched.behaviours[side])+
                    (marker.behind ? " BEHIND" : "")+" / "+
                    std::to_string(std::lround(glm::distance(camera.eye(),ship->position())))+" U",
                    {glm::clamp(marker.position.x,0.16f,0.84f),marker.position.y+0.03f},
                    0.015f,color,FontAlign::CENTER,0.30f);
            }
            UiFont::instance()->drawScreen("AI LAB / 16 CONTROLLED PAIRS",
                {0.025f,0.11f},0.020f,cyan,FontAlign::LEFT,0.47f);
            UiFont::instance()->drawScreen("WINS  A "+std::to_string(counts[1])+
                " / B "+std::to_string(counts[2])+" / "+
                std::to_string(static_cast<std::uint64_t>(
                    comparison->elapsedTicks()/SimulationTickRunner::TickRate))+" S / VIEW "+
                std::to_string(comparison->watchedPair()+1),
                {0.025f,0.15f},0.015f,glm::vec4(1),FontAlign::LEFT,0.47f);
            for (std::size_t index = 0; index < comparison->duels().size(); ++index) {
                const auto& duel = comparison->duels()[index];
                const std::string row = std::to_string(index+1)+" "+
                    AiComparisonScenario::experimentName(duel.experiment)+" A "+
                    std::to_string(static_cast<int>(std::lround(duel.hull[0]*100)))+
                    "% B "+std::to_string(static_cast<int>(std::lround(duel.hull[1]*100)))+
                    "% / "+AiComparisonScenario::resultName(duel.result);
                UiFont::instance()->drawScreen(row,{0.025f,0.195f+float(index)*0.022f},
                    0.013f,index == comparison->watchedPair() ? cyan : glm::vec4(1),
                    FontAlign::LEFT,0.47f);
            }
            UiFont::instance()->drawScreen(comparison->finished() ?
                "FINISHED / TIMEOUTS ARE NOT WINS" : "120 S LIMIT / MATCHED HULLS AND GUNS",
                {0.025f,0.64f},0.014f,cyan,FontAlign::LEFT,0.47f);
            UiFont::instance()->drawScreen("1-9 / 0 FIRST 10 / COMMA PERIOD ALL / F CAMERA",
                {0.025f,0.68f},0.013f,glm::vec4(1),FontAlign::LEFT,0.47f);
            UiFont::instance()->drawScreen(
                std::string(AiComparisonScenario::experimentName(watched.experiment))+
                " / LEAD - TRACK - ACCURACY",
                {0.56f,0.11f},0.016f,cyan,FontAlign::LEFT,0.41f);
            for (int side = 0; side < 2; ++side) {
                const auto& skill = watched.skills[side];
                const auto& fire = watched.fire[side];
                const std::string label = std::string(side == 0 ? "A " : "B ")+
                    AiComparisonScenario::behaviourName(watched.behaviours[side]);
                const auto percent = [](float value) { return std::to_string(std::lround(value*100)); };
                UiFont::instance()->drawScreen(label+" / "+
                    percent(skill.lead)+" - "+percent(skill.tracking)+" - "+percent(skill.accuracy)+"%",
                    {0.56f,0.15f+side*0.075f},0.015f,glm::vec4(1),FontAlign::LEFT,0.41f);
                UiFont::instance()->drawScreen("SHOTS "+std::to_string(fire.shots)+" / HITS "+
                    std::to_string(fire.hits)+" / "+(fire.shots ?
                        std::to_string(std::lround(double(fire.hits)*100/fire.shots))+"%" : "NO SHOTS"),
                    {0.56f,0.18f+side*0.075f},0.014f,glm::vec4(1),FontAlign::LEFT,0.41f);
            }
        }
        if (waitingForRemoteWorld) {
            const glm::vec3 waitingPosition = camera.eye() +
                camera.orientation() * glm::vec3(0.0f, 0.0f, -overlayDistance);
            const auto lobby = m_gamePlay.running().remoteLobbyStatus();
            const char* waitingText =
                m_gamePlay.running().contractSessionEnded()
                    ? "SESSION ENDED"
                    : lobby && !lobby->worldStarted
                        ? "WAITING FOR PLAYERS"
                        : "CONNECTING";
            UiFont::instance()->drawString(
                waitingText,
                waitingPosition,
                camera.orientation(),
                FontSize::SIZE5x7,
                0.06f,
                FontAlign::CENTER
            );
        }
#endif
        if (depthTestEnabled) {
            glEnable(GL_DEPTH_TEST);
        }
    }

    {
        BenchmarkCpuScope cpuScope(BenchmarkMetric::ParticleDraw);
        BenchmarkGpuScope gpuScope(BenchmarkMetric::ParticleDraw);
        World::instance()->particleEngine().draw(camera);
    }

    if (m_worldTreeRendererEnabled) {
        BenchmarkCpuScope cpuScope(BenchmarkMetric::WorldTreeDraw);
        BenchmarkGpuScope gpuScope(BenchmarkMetric::WorldTreeDraw);
        m_worldTreeRenderer->draw(camera);
    }
}
