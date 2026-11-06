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
class Starfield;
class VoxelCluster;
class WorldTreeRenderer;

class GamePlayScene: public Scene {
public:
    GamePlayScene(GamePlay& gamePlay);
    virtual ~GamePlayScene();

   virtual void update(float deltaSec) override;

    bool worldTreeRendererEnabled() const;
    void setWorldTreeRendererEnabled(bool enabled);
    void setFps(float fps);



protected:
    GamePlay& m_gamePlay;

    std::shared_ptr<VoxelRenderer> m_voxelRenderer;
    std::shared_ptr<Starfield> m_starField;

    bool m_worldTreeRendererEnabled;
    std::unique_ptr<WorldTreeRenderer> m_worldTreeRenderer;

    Property<glm::vec3> m_defaultLightDir;
    float m_fps;
#ifndef VOXELLANCER_HEADLESS_SERVER
    struct RemoteVoxelProxy;
    std::vector<std::unique_ptr<RemoteVoxelProxy>> m_remoteVoxelProxies;
    std::unordered_map<std::uint64_t, RemoteVoxelProxy*> m_remoteVoxelProxyIndex;
    std::unordered_set<std::uint64_t> m_pendingRemoteContentKeys;
    bool m_remotePreviewWasActive = false;
    bool m_remoteContentSyncNeeded = true;
    void updateRemoteVoxelProxies();
#endif


    void drawImpl(const Camera& camera) const override;
};
