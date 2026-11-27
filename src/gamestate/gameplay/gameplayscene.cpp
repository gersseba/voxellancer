#include "gameplayscene.h"
#include "scenarios/aicomparisonscenario.h"
#include "scenarios/weaponrangescenario.h"

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
#include <glm/gtc/matrix_transform.hpp>
#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glow/logging.h>
#include <glow/Program.h>

#include "benchmark/benchmarkprofiler.h"
#include "camera/camera.h"
#include "camera/camerahead.h"
#include "client/remotevoxelvisuals.h"
#include "client/remotevoxelgeometry.h"

#include "display/rendering/starfield.h"
#include "display/rendering/skybox.h"
#include "display/rendering/worldtreerenderer.h"

#include "equipment/shield.h"
#include "equipment/shieldslot.h"
#include "equipment/shieldtype.h"
#include "equipment/weaponcatalog.h"
#include "gamestate/gameplay/gameplay.h"
#include "gamestate/game.h"
#include "gamestate/gameplay/input/gameplayfreecaminput.h"
#ifndef VOXELLANCER_HEADLESS_SERVER
#include "gamestate/gameplay/running/gameplayrunning.h"
#endif

#include "geometry/viewport.h"
#include "geometry/sphere.h"
#include "geometry/ray.h"
#include "geometry/voxelray.h"

#include "resource/clustercache.h"
#include "sound/soundmanager.h"

#include "ui/hud/hud.h"
#include "ui/hud/crosshair.h"
#ifndef VOXELLANCER_HEADLESS_SERVER
#include "simulation/contractlevel.h"
#include "ui/hud/hudoverlay.h"
#include "ui/contactmarker.h"
#include "ui/stationoutfitter.h"
#include "voxeleffect/combateffects.h"
#include "voxeleffect/combateffectrenderer.h"
#include "equipment/weapons/bullet.h"
#include "worldobject/worldobjectinfo.h"
#include "physics/physics.h"
#include "world/worldlogic.h"
#include "utils/safenormalize.h"
#endif
#include "ui/uifont.h"
#include "ui/voxelfontconstants.h"

#include "voxel/voxelrenderer.h"
#include "voxel/voxelclusterbounds.h"
#include "voxel/voxelaimselection.h"
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
    std::unordered_map<std::string, float> projectileOffsets;
    std::string definitionKey;
    std::uint64_t geometryRevision = 0;
    ShieldType shieldType = ShieldType::None;
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
    m_effectRenderer = std::make_unique<CombatEffectRenderer>();
    m_outfitter = std::make_unique<StationOutfitter>(m_gamePlay.game()->settings(),World::instance()->combatEffects(),
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
    m_effectPreviewTime += deltaSec;
    updateRemoteVoxelProxies();
    updateRemoteInfoOverlay();
    const auto actor = m_gamePlay.running().remotePreviewSnapshot();
    const auto& contents = m_gamePlay.running().remoteEntityContents();
    const auto content = std::find_if(contents.begin(), contents.end(),
        [&](const auto& entry) { return actor && entry.id == actor->id; });
    const auto contract = m_gamePlay.running().contractStatus();
    const bool servicesWereVisible = m_outfitter->visible();
    if (m_gamePlay.running().localScenarioActive() && m_outfitter->developerVisible()) {
        m_outfitter->updateDeveloper(World::instance()->player().ship(), deltaSec);
    } else {
        if (m_outfitter->developerActive()) m_outfitter->setDeveloper(false);
        m_outfitter->update(contract, content == contents.end() ? nullptr : &*content, deltaSec);
    }
    if (m_outfitter->developerVisible()) {
        m_hudOverlay->setVisible(false);
        m_gamePlay.setMouseCursorFree(true);
    } else if (contract && contract->phase != voxellancer::protocol::ContractPhase::Mission) {
        m_hudOverlay->setVisible(false);
        m_gamePlay.setMouseCursorFree(m_outfitter->visible());
    } else if (servicesWereVisible) {
        m_gamePlay.setMouseCursorFree(false);
    }
    if (contract) {
        if (!m_particlePhase || *m_particlePhase != contract->phase) {
            World::instance()->particleEngine().clear();
            announceMission(*contract);
        }
        m_particlePhase = contract->phase;
    } else {
        m_particlePhase.reset();
    }
    updateCombatEffects();
#endif
}

void GamePlayScene::applyDeveloperConfiguration() {
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (!m_gamePlay.running().remotePreviewActive())
        m_outfitter->refreshDeveloper(*World::instance());
#endif
}

#ifndef VOXELLANCER_HEADLESS_SERVER
void GamePlayScene::announceMission(const voxellancer::protocol::ContractStatus& contract) {
    using voxellancer::protocol::RouteNodeKind;
    if (contract.phase != voxellancer::protocol::ContractPhase::Mission ||
        contract.selectedNode >= contract.route.size())
        return;
    const auto& node = contract.route[contract.selectedNode];
    if (node.kind == RouteNodeKind::Base) return;
    const auto level = node.kind == RouteNodeKind::Attack
        ? contractLevel(node.level) : contractSignal(node.level);
    World::instance()->player().hud().showMissionInfo(level.name, "OBJECTIVE: " + level.objective, 5.0f);
}

bool GamePlayScene::stationKey(int key) {
    if (key == GLFW_KEY_F2) {
        const bool opening = !m_outfitter->developerVisible();
        if (opening && !m_gamePlay.running().localScenarioActive())
            m_gamePlay.startLocalScenario(WeaponRangeScenario::ScenarioId);
        if (opening && m_gamePlay.freecamActive()) m_gamePlay.setFreecamActive(false);
        m_outfitter->setDeveloper(opening);
        if (opening) m_outfitter->updateDeveloper(World::instance()->player().ship(), 0);
        m_gamePlay.setMouseCursorFree(opening);
        return true;
    }
    if (m_gamePlay.running().localScenarioActive() && !m_outfitter->developerVisible()) return false;
    const bool captured = m_outfitter->key(key);
    if (captured) m_gamePlay.setMouseCursorFree(m_outfitter->visible());
    return captured;
}

bool GamePlayScene::stationMouse(int button, int action, glm::vec2 pointer) {
    if (m_gamePlay.running().localScenarioActive() && !m_outfitter->developerVisible()) return false;
    const bool captured = m_outfitter->mouse(button, action, pointer);
    if (captured) m_gamePlay.setMouseCursorFree(m_outfitter->visible());
    return captured;
}

bool GamePlayScene::closeEquipment() {
    const bool closed = m_outfitter->closeEquipment();
    if (closed) m_gamePlay.setMouseCursorFree(m_outfitter->visible());
    return closed;
}

bool GamePlayScene::developerConsoleVisible() const {
    return m_outfitter->developerVisible();
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
    CombatCueBudget combatCues;
    const auto actor = running.remotePreviewSnapshot();
    const auto contract = running.contractStatus();
    const bool cockpit = actor && !m_gamePlay.freecamActive() && (!contract || contract->alive);
    const glm::vec3 listener = m_gamePlay.freecamActive() ?
        m_gamePlay.freecamInput().cameraHead().position() :
        World::instance()->player().cameraHead().position();
    const auto queueCue = [&](voxellancer::protocol::EntityId id, CombatCueKind kind,
                             const glm::vec3& localPosition) {
        const auto key = remoteEntityKey(id);
        const auto snapshot = snapshotsById.find(key);
        const auto proxy = m_remoteVoxelProxyIndex.find(key);
        if (snapshot == snapshotsById.end() || proxy == m_remoteVoxelProxyIndex.end() ||
            !proxy->second->cluster) return;
        const auto& state = *snapshot->second;
        const glm::vec3 position = state.position +
            state.orientation * ((localPosition - proxy->second->cluster->transform().center()) * state.scale);
        const auto offset = position - listener;
        const bool personal = cockpit && id == actor->id;
        combatCues.submit({key, kind, position, personal, glm::dot(offset, offset)});
    };

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
            cluster->transform().setCenter(remoteVoxelCenter(content.voxels));
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
        proxy.definitionKey = content.definitionKey;
        if (rebuildCluster) ++proxy.geometryRevision;
        proxy.shieldType = content.shieldType;
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
    if (remotePreviewActive) for (const auto& event : damageEvents) {
        queueCue(event.entity, CombatCueKind::Hull, glm::vec3(event.cell));
        if (cockpit && event.entity == actor->id)
            World::instance()->combatEffects().kick(0.2f);
    }
    const auto impacts = running.drainProjectileImpactEvents();
    constexpr std::size_t MaxImpactsPerFrame = 64;
    const std::size_t firstImpact = impacts.size() >
        MaxImpactsPerFrame
        ? impacts.size() - MaxImpactsPerFrame
        : 0;
    for (std::size_t index = firstImpact; index < impacts.size(); ++index) {
        const auto& impact = impacts[index];
        if (const auto gun = CombatEffectSettings::gun(impact.projectileKey))
            World::instance()->combatEffects().impact(*gun,impact.position,impact.normal,impact.scale);
    }

    const auto shieldHitEvents = running.drainShieldHitEvents();
    if (remotePreviewActive) for (const auto& event : shieldHitEvents)
        queueCue(event.entity, CombatCueKind::Shield, event.localImpactPosition);
    constexpr std::size_t MaxShieldHitFlashesPerFrame = 8;
    const std::size_t firstShieldHitEvent = shieldHitEvents.size() >
        MaxShieldHitFlashesPerFrame
        ? shieldHitEvents.size() - MaxShieldHitFlashesPerFrame
        : 0;
    for (std::size_t index = firstShieldHitEvent;
         index < shieldHitEvents.size(); ++index) {
        const auto& event = shieldHitEvents[index];
        World::instance()->combatEffects().shieldHit(
            event.entity,event.localImpactPosition,event.absorbedDamage);
        const auto localActor = World::instance()->entityId(World::instance()->player().ship());
        if (!m_gamePlay.freecamActive() &&
            ((remotePreviewActive && actor && event.entity == actor->id) ||
             (!remotePreviewActive && localActor && event.entity == *localActor)))
            World::instance()->combatEffects().kick(std::min(0.45f,event.absorbedDamage/100.0f));
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
    if (remotePreviewActive) {
        for (const auto& event : weaponEvents)
            queueCue(event.entity, CombatCueKind::Fire, glm::vec3(event.cell));
        m_gamePlay.soundManager().playCombatCues(combatCues);
    } else {
        m_gamePlay.soundManager().stopCombatCues();
    }
    constexpr std::size_t MaxWeaponMuzzleFlashesPerFrame = 32;
    const std::size_t firstEvent = weaponEvents.size() >
        MaxWeaponMuzzleFlashesPerFrame
        ? weaponEvents.size() - MaxWeaponMuzzleFlashesPerFrame
        : 0;
    for (std::size_t index = firstEvent; index < weaponEvents.size(); ++index) {
        const auto& event = weaponEvents[index];
        const auto snapshot = snapshotsById.find(remoteEntityKey(event.entity));
        if (snapshot == snapshotsById.end()) {
            continue;
        }

        const auto& state = *snapshot->second;
        const auto proxy = m_remoteVoxelProxyIndex.find(remoteEntityKey(event.entity));
        if (proxy == m_remoteVoxelProxyIndex.end() || !proxy->second->cluster) continue;
        const glm::vec3 firingPosition = state.position +
            state.orientation * ((glm::vec3(event.cell) -
                proxy->second->cluster->transform().center()) * state.scale);
        for (const auto& mount : proxy->second->equipmentStates) {
            if (mount.kind != voxellancer::protocol::EquipmentSlotKind::Weapon ||
                mount.cell != event.cell) continue;
            if (const auto gun = CombatEffectSettings::gun(mount.equipmentKey)) {
                const auto direction = state.orientation*glm::vec3(0,0,-1);
                const float offset = Property<float>(mount.equipmentKey+".general.bulletSpawnOffset",1).get();
                World::instance()->combatEffects().fire(*gun,firingPosition+direction*offset,
                    direction,state.scale,event.entity);
                if (cockpit && event.entity == actor->id &&
                    (*gun == 2 || *gun == 3 || *gun == 4 || *gun == 8))
                    World::instance()->combatEffects().kick(0.35f);
            }
        }
    }
}

void GamePlayScene::updateCombatEffects() {
    auto& world = *World::instance();
    auto& effects = world.combatEffects();
    auto& running = m_gamePlay.running();
    world.worldLogic().setShieldHitEventReplicationEnabled(!running.remotePreviewActive());
    const auto contract = running.contractStatus();
    if (contract && contract->phase != voxellancer::protocol::ContractPhase::Mission) {
        effects.clear();
        m_shieldSurfaceCache.clear();
        return;
    }
    std::vector<ShieldEffectBody> shields;
    std::unordered_set<std::uint64_t> active;
    const glm::vec3 viewer = m_gamePlay.cameraHead().position();
    const float distance = effects.settings.value(CombatEffectSettings::Global,
        CombatEffectSettings::ViewDistance);
    const auto addShield = [&](voxellancer::protocol::EntityId id, VoxelCluster& cluster,
                               std::uint64_t revision, ShieldType type, float fraction) {
        if (type == ShieldType::None || glm::distance(viewer,cluster.transform().position()) > distance)
            return;
        const auto index = remoteEntityKey(id);
        active.insert(index);
        auto& cache = m_shieldSurfaceCache[index];
        const auto sphere = cluster.bounds().minimalGridSphere();
        if (!cache.points || cache.cluster != &cluster || cache.voxelRevision != revision ||
            cache.voxelCount != cluster.voxelCount() || cache.type != type) {
            if (type == ShieldType::Sphere) {
                cache.points = CombatEffects::sphereSurface(sphere.position(),sphere.radius());
            } else {
                auto points = std::make_shared<std::vector<ShieldEffectPoint>>();
                constexpr glm::ivec3 neighbors[] = {{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
                for (const auto& entry : cluster.voxelMap())
                    for (const auto& neighbor : neighbors)
                        if (!cluster.voxel(entry.first+neighbor))
                            points->push_back({glm::vec3(entry.first)+glm::vec3(neighbor)*0.76f,
                                glm::vec3(neighbor),1});
                std::sort(points->begin(),points->end(),[](const auto& a, const auto& b) {
                    if (a.position.x != b.position.x) return a.position.x < b.position.x;
                    if (a.position.y != b.position.y) return a.position.y < b.position.y;
                    return a.position.z < b.position.z;
                });
                constexpr std::size_t MaxSurfacePoints = 2048;
                if (points->size() > MaxSurfacePoints) {
                    auto sampled = std::make_shared<std::vector<ShieldEffectPoint>>();
                    for (std::size_t i = 0; i < MaxSurfacePoints; ++i)
                        sampled->push_back((*points)[i*points->size()/MaxSurfacePoints]);
                    points = std::move(sampled);
                }
                cache.points = points;
            }
            cache.cluster = &cluster; cache.voxelRevision = revision;
            cache.voxelCount = cluster.voxelCount(); cache.type = type;
        }
        shields.push_back({id,cluster.transform(),type,fraction,sphere.position(),sphere.radius(),cache.points});
    };
    effects.beginProjectiles();
    if (running.remotePreviewActive()) {
        for (const auto& state : running.remoteReplicaSnapshots()) {
            const auto proxy = m_remoteVoxelProxyIndex.find(remoteEntityKey(state.id));
            if (proxy == m_remoteVoxelProxyIndex.end() || !proxy->second->cluster) continue;
            if (state.kind == voxellancer::protocol::EntitySnapshot::Kind::Ship)
                addShield(state.id,*proxy->second->cluster,proxy->second->geometryRevision,
                    proxy->second->shieldType,state.shieldFraction);
            if (state.kind == voxellancer::protocol::EntitySnapshot::Kind::Bullet)
                if (const auto gun = CombatEffectSettings::gun(proxy->second->definitionKey))
                    effects.projectile(state.id,*gun,state.position,state.directionalVelocity);
        }
    } else {
        for (auto* object : world.worldObjects()) {
            const auto id = world.entityId(object);
            if (!id) continue;
            if (object->objectType() == WorldObjectType::Bullet)
                if (const auto gun = CombatEffectSettings::gun(object->info().definitionKey()))
                    effects.projectile(*id,*gun,object->position(),object->physics().speed().directional());
        }
        for (auto* ship : world.ships()) {
            float hp = 0, maximum = 0;
            for (const auto& slot : ship->components().shieldSlots()) {
                if (slot->shield()) {
                    maximum += slot->shield()->maxHP();
                    if (!slot->shield()->depleted()) hp += slot->shield()->hp();
                }
            }
            if (const auto id = world.entityId(ship))
                addShield(*id,*ship,ship->voxelRevision(),ship->shieldType(),maximum > 0 ? hp/maximum : 0);
        }
    }
    effects.endProjectiles();
    effects.shields(shields);
    effects.build(viewer);
    for (auto it = m_shieldSurfaceCache.begin(); it != m_shieldSurfaceCache.end();) {
        if (!active.count(it->first)) it = m_shieldSurfaceCache.erase(it);
        else ++it;
    }
}

void GamePlayScene::drawEffectPreview() const {
    if (!m_outfitter->effectsVisible()) return;
    GLint viewport[4], scissor[4];
    glGetIntegerv(GL_VIEWPORT,viewport);
    glGetIntegerv(GL_SCISSOR_BOX,scissor);
    const bool scissorEnabled = glIsEnabled(GL_SCISSOR_TEST);
    const int x = viewport[0]+static_cast<int>(viewport[2]*0.64f);
    const int y = viewport[1]+static_cast<int>(viewport[3]*0.31f);
    const int w = static_cast<int>(viewport[2]*0.28f);
    const int h = static_cast<int>(viewport[3]*0.36f);
    if (w <= 0 || h <= 0) return;
    glViewport(x,y,w,h);
    glEnable(GL_SCISSOR_TEST);
    glScissor(x,y,w,h);
    GLboolean depthMask;
    glGetBooleanv(GL_DEPTH_WRITEMASK,&depthMask);
    glDepthMask(GL_TRUE);
    glClear(GL_DEPTH_BUFFER_BIT);
    glDepthMask(depthMask);
    const auto view = glm::lookAt(glm::vec3(10,8,18),glm::vec3(0),glm::vec3(0,1,0));
    const auto projection = glm::perspective(glm::radians(45.0f),static_cast<float>(w)/h,0.1f,100.0f);
    m_effectRenderer->draw(World::instance()->combatEffects().preview(
        m_outfitter->effectGroup(),m_effectPreviewTime),projection*view);
    glViewport(viewport[0],viewport[1],viewport[2],viewport[3]);
    glScissor(scissor[0],scissor[1],scissor[2],scissor[3]);
    if (!scissorEnabled) glDisable(GL_SCISSOR_TEST);
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

std::optional<CrosshairAim> GamePlayScene::remoteAimPoint(
    const glm::vec3& origin, const glm::vec3& direction) const {
    const auto actor = m_gamePlay.running().remotePreviewSnapshot();
    if (!actor) return std::nullopt;
    const auto& contents = m_gamePlay.running().remoteEntityContents();
    const auto content = std::find_if(contents.begin(), contents.end(),
        [&actor](const auto& entry) { return entry.id == actor->id; });
    if (content == contents.end()) return std::nullopt;

    float range = std::numeric_limits<float>::infinity();
    for (const auto& slot : content->equipment) {
        if (slot.kind != voxellancer::protocol::EquipmentSlotKind::Weapon ||
            Property<std::string>::get(slot.equipmentKey + ".general.type") != "gun") continue;
        const std::string bullet = Property<std::string>(slot.equipmentKey + ".general.bullet");
        const float travel = Property<float>(bullet + ".general.speed").get() *
            Property<float>(bullet + ".general.lifetime").get();
        const auto profile = WeaponProfile::load(bullet);
        range = std::min(range, profile.enabled ? std::min(travel, profile.range) : travel);
    }
    if (!std::isfinite(range)) range = Property<float>("general.infinityAimDistance");
    const Ray ray(origin, direction);
    float nearest = range;
    float aimDistance = range;
    for (const auto& snapshot : m_gamePlay.running().remoteReplicaSnapshots()) {
        if (snapshot.id == actor->id) continue;
        const auto proxy = m_remoteVoxelProxyIndex.find(remoteEntityKey(snapshot.id));
        if (proxy == m_remoteVoxelProxyIndex.end() || !proxy->second->cluster) continue;
        auto& cluster = *proxy->second->cluster;
        if (!ray.intersects(cluster.bounds().sphere(cluster.transform()))) continue;
        const auto& transform = cluster.transform();
        for (const auto& entry : cluster.voxelMap()) {
            const auto hit = voxelRayInterval(origin, direction, transform.position(),
                transform.orientation(), transform.scale(), transform.center(), entry.first, range);
            if (hit && hit->x < nearest) {
                nearest = hit->x;
                aimDistance = (hit->x + hit->y) * 0.5f;
            }
        }
    }
    return CrosshairAim{origin + direction * aimDistance, nearest < range};
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
        glm::vec3 direction;
        float fieldOfAim;
        float speed;
        float lifetime;
        float range;
        float spawnOffset;
        bool inheritsVelocity;
    };
    std::vector<GunCue> guns;
    if (showLead) {
        const auto& contents = m_gamePlay.running().remoteEntityContents();
        const auto content = std::find_if(contents.begin(), contents.end(),
            [&actor](const auto& entry) { return entry.id == actor->id; });
        const auto proxy = m_remoteVoxelProxyIndex.find(remoteEntityKey(actor->id));
        if (content != contents.end() && proxy != m_remoteVoxelProxyIndex.end() &&
            proxy->second->cluster) {
            const glm::vec3 center = proxy->second->cluster->transform().center();
            for (const auto& slot : content->equipment) {
                if (slot.kind != voxellancer::protocol::EquipmentSlotKind::Weapon ||
                    Property<std::string>::get(slot.equipmentKey + ".general.type") != "gun") {
                    continue;
                }
                const std::string bullet = Property<std::string>(slot.equipmentKey + ".general.bullet");
                const float speed = Property<float>(bullet + ".general.speed");
                const float lifetime = Property<float>(bullet + ".general.lifetime");
                const auto profile = WeaponProfile::load(bullet);
                auto& offsets = proxy->second->projectileOffsets;
                auto offset = offsets.find(bullet);
                if (offset == offsets.end()) {
                    VoxelCluster projectile(1.0f);
                    const std::string file = Property<std::string>(bullet + ".general.voxelcluster");
                    ClusterCache::instance()->fillCluster(&projectile, "data/voxelcluster/" + file);
                    const float scale = Property<float>(bullet + ".general.scale", 1.0f);
                    const float length = projectile.bounds().minimalGridAABB().extent(ZAxis) * scale;
                    offset = offsets.emplace(bullet, length / 2.0f + glm::root_two<float>() * scale).first;
                }
                const std::string group = content->definitionKey + ".hardpointGroup" +
                    std::to_string(slot.group) + ".";
                guns.push_back({
                    actor->position + actor->orientation * ((glm::vec3(slot.cell) - center) * actor->scale),
                    actor->orientation * glm::normalize(Property<glm::vec3>(group + "direction").get()),
                    Property<float>(group + "fieldOfAim"),
                    speed, lifetime, profile.enabled ? profile.range : speed * lifetime,
                    offset->second + (profile.enabled ? actor->scale : 0.0f), profile.enabled});
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
        if (!contact.hostile && contract &&
            contract->phase == voxellancer::protocol::ContractPhase::Mission &&
            contact.kind == voxellancer::protocol::EntitySnapshot::Kind::Ship) {
            const auto ally = std::find_if(contents.begin(), contents.end(), [&contact](const auto& entry) {
                return entry.id == contact.id && entry.definitionKey == DistressWingmanDefinition;
            });
            if (ally != contents.end()) {
                const auto marker = projectContactMarker(
                    camera.viewProjection() * glm::vec4(contact.position, 1.0f));
                const glm::vec4 allyColor(0.42f, 1.0f, 0.72f, 1.0f);
                font->drawScreen("+", marker.position - glm::vec2(0,0.018f),
                    0.024f, allyColor, FontAlign::CENTER);
                font->drawScreen(marker.behind ? "WINGMAN BEHIND" : "WINGMAN",
                    {glm::clamp(marker.position.x, 0.15f, 0.85f), marker.position.y + 0.02f},
                    0.017f, allyColor, FontAlign::CENTER);
            }
        }
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
        const auto proxy = m_remoteVoxelProxyIndex.find(remoteEntityKey(contact.id));
        const auto root = content != contents.end() ? content->definitionKey + ".general." : std::string();
        const auto name = root.empty() ? std::string("ENEMY") :
            Property<std::string>::get(root + "contactName", "ENEMY");
        const std::string label = name + (marker.behind ? " BEHIND - " : " - ") +
            std::to_string(distance) + " U";
        font->drawScreen(label,
            glm::vec2(glm::clamp(marker.position.x, 0.15f, 0.85f),
                marker.position.y + 0.02f),
            0.017f, color, FontAlign::CENTER, 0.28f);

        if (showLead && marker.onScreen && distance < 450 && !root.empty() &&
            proxy != m_remoteVoxelProxyIndex.end() && proxy->second->cluster) {
            const int projector = Property<int>::get(root + "shieldProjectorGroup", -1);
            if (projector >= 0) {
                const bool projectorOnline = std::any_of(content->equipment.begin(), content->equipment.end(),
                    [](const auto& slot) { return slot.kind == SlotKind::Shield && !slot.equipmentKey.empty(); });
                const glm::vec4 moduleColor(1.0f,0.82f,0.38f,1.0f);
                for (const auto& slot : content->equipment) {
                    if (slot.kind != SlotKind::Weapon || slot.equipmentKey.empty()) continue;
                    const auto point = contact.position + contact.orientation *
                        ((glm::vec3(slot.cell) - proxy->second->cluster->transform().center()) * contact.scale);
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

        if (content == contents.end() || proxy == m_remoteVoxelProxyIndex.end() ||
            !proxy->second->cluster) continue;
        VoxelAimSelection selection(proxy->second->cluster->transform().center());
        for (const auto& voxel : content->voxels) selection.consider(voxel.cell);
        if (!selection.cell()) continue;
        const glm::vec3 targetPoint = contact.position + contact.orientation *
            ((glm::vec3(*selection.cell()) - proxy->second->cluster->transform().center()) * contact.scale);
        std::optional<glm::vec3> point;
        for (const auto& gun : guns) {
            point = HardpointAimHelper::interceptPoint(gun.muzzle,
                targetPoint, contact.directionalVelocity -
                    (gun.inheritsVelocity ? actor->directionalVelocity : glm::vec3(0.0f)),
                gun.speed, gun.lifetime, gun.range, gun.spawnOffset);
            if (point && glm::dot(glm::normalize(*point - gun.muzzle), gun.direction) >=
                std::cos(gun.fieldOfAim)) break;
            point.reset();
        }
        if (!point) continue;
        const auto lead = projectContactMarker(camera.viewProjection() * glm::vec4(*point, 1.0f));
        if (!lead.onScreen) continue;
        const glm::vec3 direction = glm::normalize(*point - origin);
        const float leadDistance = 1.0f - glm::dot(direction, crosshairRay);
        if (leadDistance < nearestLead) {
            nearestLead = leadDistance;
            cueScreen = lead.position;
            m_remoteLeadCue = RemoteLeadCue{actor->id, contact.id, *point};
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
    Camera visualCamera(camera.viewport().x,camera.viewport().y);
    visualCamera.setFovy(camera.fovy());
    visualCamera.setZNear(camera.zNear());
    visualCamera.setZFar(camera.zFar());
    visualCamera.setProjectionOffset(camera.projectionOffset());
    float kick = 0;
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (!m_gamePlay.freecamActive() && !m_outfitter->visible())
        kick = World::instance()->combatEffects().cameraKick();
#endif
    visualCamera.setEye(camera.eye()+camera.orientation()*glm::vec3(0,0.06f,0.18f)*kick);
    visualCamera.setOrientation(camera.orientation());
    {
        BenchmarkCpuScope cpuScope(BenchmarkMetric::SkyboxDraw);
        BenchmarkGpuScope gpuScope(BenchmarkMetric::SkyboxDraw);
        World::instance()->skybox().draw(visualCamera);
    }

    m_voxelRenderer->program()->setUniform("lightdir", m_defaultLightDir.get());

    {
        BenchmarkCpuScope cpuScope(BenchmarkMetric::WorldObjectDraw);
        BenchmarkGpuScope gpuScope(BenchmarkMetric::WorldObjectDraw);
        m_voxelRenderer->prepareDraw(visualCamera);
        UiFont::instance()->setCamera(camera);
        if (!m_gamePlay.running().remotePreviewActive()) {
            for (WorldObject* worldObject : World::instance()->worldObjects()) {
                if (worldObject->objectType() == WorldObjectType::Bullet &&
                    CombatEffectSettings::gun(worldObject->info().definitionKey())) continue;
                glm::vec3 offset(0);
#ifndef VOXELLANCER_HEADLESS_SERVER
                if (const auto id = World::instance()->entityId(worldObject))
                    offset = World::instance()->combatEffects().recoil(*id);
#endif
                VoxelRenderer::instance()->draw(*worldObject,offset);
            }
        }
#ifndef VOXELLANCER_HEADLESS_SERVER
        for (const auto& proxy : m_remoteVoxelProxies) {
            if (CombatEffectSettings::gun(proxy->definitionKey)) continue;
            if (proxy->cluster && proxy->cluster->voxelCount() > 0) {
                VoxelRenderer::instance()->draw(*proxy->cluster,
                    World::instance()->combatEffects().recoil(proxy->id));
            }
        }
#endif
    }
    m_voxelRenderer->afterDraw();

#ifndef VOXELLANCER_HEADLESS_SERVER
    m_effectRenderer->draw(World::instance()->combatEffects().boxes(),visualCamera.viewProjection());
#endif
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
            drawRemoteContacts(visualCamera);
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
        if (const auto* range = m_gamePlay.weaponRange(); range && !m_outfitter->developerVisible()) {
            UiFont::instance()->drawScreen(
                "WEAPON RANGE / 1-6 CONTRACT / 7-9 LEGACY / 9 TOGGLE / 0 RESET",
                {0.5f, 0.08f}, 0.017f, glm::vec4(1), FontAlign::CENTER, 0.9f);
            if (range->showSelection()) {
                UiFont::instance()->drawScreen(
                    range->selectedName(), {0.5f, 0.13f}, 0.025f,
                    glm::vec4(0.25f, 0.85f, 0.95f, 1), FontAlign::CENTER, 0.8f);
            }
        }
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
                    visualCamera.viewProjection()*glm::vec4(ship->position(),1));
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

    if (!m_gamePlay.running().contractStatus() ||
        m_gamePlay.running().contractStatus()->phase == voxellancer::protocol::ContractPhase::Mission) {
        BenchmarkCpuScope cpuScope(BenchmarkMetric::ParticleDraw);
        BenchmarkGpuScope gpuScope(BenchmarkMetric::ParticleDraw);
        World::instance()->particleEngine().draw(visualCamera);
    }

    if (m_worldTreeRendererEnabled) {
        BenchmarkCpuScope cpuScope(BenchmarkMetric::WorldTreeDraw);
        BenchmarkGpuScope gpuScope(BenchmarkMetric::WorldTreeDraw);
        m_worldTreeRenderer->draw(visualCamera);
    }
#ifndef VOXELLANCER_HEADLESS_SERVER
    drawEffectPreview();
#endif
}
