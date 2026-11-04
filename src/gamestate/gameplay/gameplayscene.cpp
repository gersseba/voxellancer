#include "gameplayscene.h"

#include <sstream>
#include <iomanip>

#include <GL/glew.h>
#include <glow/logging.h>
#include <glow/Program.h>

#include "benchmark/benchmarkprofiler.h"
#include "camera/camera.h"
#include "camera/camerahead.h"

#include "display/rendering/starfield.h"
#include "display/rendering/skybox.h"
#include "display/rendering/worldtreerenderer.h"

#include "gamestate/gameplay/gameplay.h"

#include "geometry/viewport.h"

#include "sound/soundmanager.h"

#include "ui/hud/hud.h"
#include "ui/voxelfont.h"
#include "ui/voxelfontconstants.h"

#include "voxel/voxelrenderer.h"
#include "voxeleffect/voxelparticleengine.h"

#include "world/world.h"
#include "worldobject/worldobject.h"

#include "player.h"


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
}

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
        for (WorldObject* worldObject : World::instance()->worldObjects()) {
            VoxelRenderer::instance()->draw(*worldObject);
        }
    }

    {
        BenchmarkCpuScope cpuScope(BenchmarkMetric::HudDraw);
        BenchmarkGpuScope gpuScope(BenchmarkMetric::HudDraw);
        World::instance()->player().hud().draw();

        std::ostringstream fpsText;
        fpsText << "FPS: " << std::fixed << std::setprecision(1) << m_fps;

        constexpr float overlayDistance = 8.0f;
        const glm::mat4 projection = camera.projection();
        const float halfWidth = overlayDistance / projection[0][0];
        const float halfHeight = overlayDistance / projection[1][1];
        const glm::vec3 overlayPosition = camera.eye() + camera.orientation() *
            glm::vec3(-halfWidth + 0.3f, halfHeight - 0.9f, -overlayDistance);

        const GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
        glDisable(GL_DEPTH_TEST);
        VoxelFont::instance()->drawString(
            fpsText.str(), overlayPosition, camera.orientation(),
            FontSize::SIZE5x7, 0.12f, FontAlign::LEFT);
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
