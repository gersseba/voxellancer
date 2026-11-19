#pragma once

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <glm/glm.hpp>

#include "display/scene.h"

#include "property/property.h"


class GamePlay;
class VoxelRenderer;
class SoundManager;
class CameraHead;
class FrameBuffer;
class Blitter;
class RenderPipeline;
class Player;
class Ship;
class Starfield;
class VoxelCluster;
class WorldTreeRenderer;
#ifndef VOXELLANCER_HEADLESS_SERVER
class HudOverlay;
#endif

class GamePlayScene: public Scene {
public:
    GamePlayScene(GamePlay& gamePlay);
    virtual ~GamePlayScene();

   virtual void update(float deltaSec) override;

    bool worldTreeRendererEnabled() const;
    void setWorldTreeRendererEnabled(bool enabled);
    void setFps(float fps);
#ifndef VOXELLANCER_HEADLESS_SERVER
    HudOverlay& hudOverlay();
#endif


protected:
    GamePlay& m_gamePlay;

    std::shared_ptr<VoxelRenderer> m_voxelRenderer;
    std::shared_ptr<Starfield> m_starField;

    bool m_worldTreeRendererEnabled;
    std::unique_ptr<WorldTreeRenderer> m_worldTreeRenderer;

    Property<glm::vec3> m_defaultLightDir;
    float m_fps;
#ifndef VOXELLANCER_HEADLESS_SERVER
    mutable std::unique_ptr<HudOverlay> m_hudOverlay;
    struct RemoteVoxelProxy;
    std::vector<std::unique_ptr<RemoteVoxelProxy>> m_remoteVoxelProxies;
    std::unordered_map<std::uint64_t, RemoteVoxelProxy*> m_remoteVoxelProxyIndex;
    std::unordered_set<std::uint64_t> m_pendingRemoteContentKeys;
    bool m_remotePreviewWasActive = false;
    bool m_remoteContentSyncNeeded = true;
    float m_shieldShimmerAccumulator = 0.0f;
    float m_shieldPulseTime = 0.0f;
    std::uint32_t m_shieldShimmerRandomState = 0xA341316Cu;
    struct ShieldSurfacePoint {
        glm::ivec3 cell;
        glm::ivec3 normal;
    };
    struct ShieldSurfaceCache {
        std::uint64_t voxelRevision = 0;
        int minimumX = 0;
        int maximumX = 0;
        bool initialized = false;
        std::vector<ShieldSurfacePoint> points;
    };
    std::unordered_map<const Ship*, ShieldSurfaceCache>
        m_shieldSurfaceCache;
    void updateRemoteVoxelProxies();
    void updateRemoteInfoOverlay();
    void updateShieldShimmer(float deltaSec);
    float nextShieldShimmerRandom();
#endif


    void drawImpl(const Camera& camera) const override;
};
