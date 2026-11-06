#include "gameplayscene.h"

#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>

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

#include "gamestate/gameplay/gameplay.h"
#ifndef VOXELLANCER_HEADLESS_SERVER
#include "gamestate/gameplay/running/gameplayrunning.h"
#endif

#include "geometry/viewport.h"

#include "sound/soundmanager.h"

#include "ui/hud/hud.h"
#include "ui/voxelfont.h"
#include "ui/voxelfontconstants.h"

#include "voxel/voxelrenderer.h"
#include "voxeleffect/voxelparticleengine.h"
#ifndef VOXELLANCER_HEADLESS_SERVER
#include "voxel/voxel.h"
#include "voxel/voxelcluster.h"
#include "voxeleffect/voxelexplosiongenerator.h"
#endif

#include "world/world.h"
#include "worldobject/worldobject.h"

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
{
    addRenderPass(m_starField, 0);
}

GamePlayScene::~GamePlayScene() = default;

void GamePlayScene::update(float deltaSec) {
    m_starField->update(deltaSec, m_gamePlay.cameraHead().position());
#ifndef VOXELLANCER_HEADLESS_SERVER
    updateRemoteVoxelProxies();
#endif
}

#ifndef VOXELLANCER_HEADLESS_SERVER
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

    const auto shieldHitEvents =
        running.drainRemoteShieldHitEvents();
    constexpr std::size_t MaxShieldHitFlashesPerFrame = 32;
    const std::size_t firstShieldHitEvent = shieldHitEvents.size() >
        MaxShieldHitFlashesPerFrame
        ? shieldHitEvents.size() - MaxShieldHitFlashesPerFrame
        : 0;
    for (std::size_t index = firstShieldHitEvent;
         index < shieldHitEvents.size(); ++index) {
        const auto& event = shieldHitEvents[index];
        const auto snapshot = snapshotsById.find(remoteEntityKey(event.entity));
        if (snapshot == snapshotsById.end()) {
            continue;
        }

        const auto& state = *snapshot->second;
        const glm::vec3 impactPosition = state.position +
            state.orientation * (glm::vec3(event.cell) * state.scale);
        VoxelExplosionGenerator flash(nullptr, World::instance());
        flash.setPosition(impactPosition);
        flash.setRadius(state.scale * 0.04f);
        flash.setScale(state.scale * 0.06f);
        flash.setForce(0.02f);
        flash.setLifetime(0.14f);
        flash.setCount(3);
        flash.setColor(0x66ccff);
        flash.setEmissiveness(0.9f);
        flash.spawn();
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
#endif

bool GamePlayScene::worldTreeRendererEnabled() const {
    return m_worldTreeRendererEnabled;
}

void GamePlayScene::setWorldTreeRendererEnabled(bool enabled) {
    m_worldTreeRendererEnabled = enabled;
    glow::debug()<< "Drawing Worldtree " << enabled;
}

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

    {
        BenchmarkCpuScope cpuScope(BenchmarkMetric::HudDraw);
        BenchmarkGpuScope gpuScope(BenchmarkMetric::HudDraw);
#ifndef VOXELLANCER_HEADLESS_SERVER
        const bool waitingForRemoteWorld = m_gamePlay.waitingForRemoteWorld();
#else
        const bool waitingForRemoteWorld = false;
#endif
        constexpr float overlayDistance = 8.0f;
        const glm::mat4 projection = camera.projection();
        const float halfWidth = overlayDistance / projection[0][0];
        const float halfHeight = overlayDistance / projection[1][1];
        const glm::vec3 overlayPosition = camera.eye() + camera.orientation() *
            glm::vec3(-halfWidth + 0.3f, halfHeight - 0.9f, -overlayDistance);
        std::ostringstream fpsText;
        if (!waitingForRemoteWorld) {
            World::instance()->player().hud().draw();
            fpsText << "FPS: " << std::fixed << std::setprecision(1) << m_fps;
        }

        const GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
        glDisable(GL_DEPTH_TEST);
        if (!waitingForRemoteWorld) {
            VoxelFont::instance()->drawString(
                fpsText.str(), overlayPosition, camera.orientation(),
                FontSize::SIZE5x7, 0.12f, FontAlign::LEFT);
        }
#ifndef VOXELLANCER_HEADLESS_SERVER
        const auto& remoteSnapshot = m_gamePlay.running().remotePreviewSnapshot();
        if (remoteSnapshot) {
            std::ostringstream remoteText;
            remoteText << "SERVER X " << std::fixed << std::setprecision(1)
                << remoteSnapshot->position.x << " Y " << remoteSnapshot->position.y
                << " Z " << remoteSnapshot->position.z
                << " SHIELD " << std::setprecision(0)
                << remoteSnapshot->shieldFraction * 100.0f
                << "% HULL " << remoteSnapshot->hullFraction * 100.0f << "%";
            VoxelFont::instance()->drawString(
                remoteText.str(),
                overlayPosition + camera.orientation() * glm::vec3(0.0f, -0.5f, 0.0f),
                camera.orientation(),
                FontSize::SIZE5x7,
                0.12f,
                FontAlign::LEFT
            );

            if (glm::length(remoteSnapshot->engineState) > 0.01f) {
                std::ostringstream engineText;
                engineText << "ENGINE X " << std::fixed << std::setprecision(0)
                    << remoteSnapshot->engineState.x * 100.0f
                    << "% Y " << remoteSnapshot->engineState.y * 100.0f
                    << "% Z " << remoteSnapshot->engineState.z * 100.0f
                    << "%";
                VoxelFont::instance()->drawString(
                    engineText.str(),
                    overlayPosition + camera.orientation() *
                        glm::vec3(0.0f, -0.9f, 0.0f),
                    camera.orientation(),
                    FontSize::SIZE5x7,
                    0.12f,
                    FontAlign::LEFT
                );
            }

            const auto& remoteContents =
                m_gamePlay.running().remoteEntityContents();
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
                    equipmentText << kind << " " << equipment.group << " "
                        << key;
                    VoxelFont::instance()->drawString(
                        equipmentText.str(),
                        overlayPosition + camera.orientation() *
                            glm::vec3(
                                0.0f,
                                -1.35f - static_cast<float>(index) * 0.35f,
                                0.0f),
                        camera.orientation(),
                        FontSize::SIZE5x7,
                        0.12f,
                        FontAlign::LEFT
                    );
                }
                if (actorContent->equipment.size() > visibleRows) {
                    std::ostringstream remainingText;
                    remainingText << "EQUIPMENT +"
                        << actorContent->equipment.size() - visibleRows
                        << " MORE";
                    VoxelFont::instance()->drawString(
                        remainingText.str(),
                        overlayPosition + camera.orientation() *
                            glm::vec3(
                                0.0f,
                                -1.35f - static_cast<float>(visibleRows) * 0.35f,
                                0.0f),
                        camera.orientation(),
                        FontSize::SIZE5x7,
                        0.12f,
                        FontAlign::LEFT
                    );
                }
            }
        } else if (waitingForRemoteWorld) {
            const glm::vec3 waitingPosition = camera.eye() +
                camera.orientation() * glm::vec3(0.0f, 0.0f, -overlayDistance);
            VoxelFont::instance()->drawString(
                "WAITING",
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

    m_voxelRenderer->afterDraw();

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
