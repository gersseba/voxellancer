#include "gameplay.h"

#include <stdexcept>

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "gamestate/game.h"
#include "gamestate/gameplay/running/gameplayrunning.h"
#include "gamestate/gameplay/paused/gameplaypaused.h"
#include "gamestate/gameplay/input/gameplaynormalinput.h"
#include "gamestate/gameplay/input/gameplayfreecaminput.h"
#include "gameplayscene.h"
#include "benchmark/benchmarkmode.h"

#include "utils/statemachine/trigger.h"
#include "utils/statemachine/triggeredtransition.h"

#include "scenarios/basescenario.h"
#include "scenarios/battlescenario.h"
#include "scenarios/gamescenario.h"
#include "scenarios/frozengamescenario.h"
#include "scenarios/friendlyfirescenario.h"
#include "scenarios/missionscenario.h"
#include "scenarios/scriptedscenario.h"
#include "scenarios/piratescenario.h"
#include "scenarios/largecombatscenario.h"
#include "scenarios/shieldshowcasescenario.h"
#include "scenarios/aicomparisonscenario.h"
#include "scenarios/weaponrangescenario.h"

#include "sound/soundmanager.h"

#include "voxeleffect/voxelparticleengine.h"

#include "world/world.h"
#include "world/god.h"
#include "worldobject/ship.h"
#include "resource/worldobjectbuilder.h"

#include "player.h"
#include "camera/camerahead.h"
#include "camera/cameradolly.h"
#include "ui/hud/hud.h"
#include "ui/hud/hudoverlay.h"
#include "display/viewer.h"

#include "display/rendering/texturerenderer.h"


GamePlay::GamePlay(Game* game) :
    GameState("In Game", game),
    m_game(game),
    m_scene(new GamePlayScene(*this)),
    m_soundManager(new SoundManager()),
    m_runningState(new GamePlayRunning(this, *World::instance())),
    m_pausedState(new GamePlayPaused(this)),
#ifndef VOXELLANCER_HEADLESS_SERVER
    m_waitingInput(new GamePlayInput(*this, true)),
#endif
    m_normalInput(new GamePlayNormalInput(*this)),
    m_freecamInput(new GamePlayFreecamInput(*this)),
    m_freecamActive(false),
#ifndef VOXELLANCER_HEADLESS_SERVER
    m_mouseCursorFree(false),
#endif
    m_initialScenario(0)
{
    updateView();
    setInitialSubState(m_runningState);

    m_runningState->pauseTrigger().setTarget(new TriggeredTransition(m_runningState, m_pausedState));
    m_pausedState->continueTrigger().setTarget(new TriggeredTransition(m_pausedState, m_runningState));
}

GamePlay::~GamePlay() = default;

Game* GamePlay::game() {
    return m_game;
}

GamePlayScene& GamePlay::scene() {
    return *m_scene;
}

GamePlayRunning& GamePlay::running() {
    return *m_runningState;
}

GamePlayPaused& GamePlay::paused() {
    return *m_pausedState;
}

bool GamePlay::freecamActive() const {
    return m_freecamActive;
}

void GamePlay::setFreecamActive(bool active) {
    m_freecamActive = active;

    if (m_freecamActive) {
        m_freecamInput->setPosition(World::instance()->player().cameraHead().cameraDolly()->position());
        m_freecamInput->setOrientation(World::instance()->player().cameraHead().cameraDolly()->orientation());

        World::instance()->player().move(glm::vec3(0));
        World::instance()->player().rotate(glm::vec3(0));

        Property<bool>("vfx.drawhud").set(false);
    } else {
        Property<bool>("vfx.drawhud").set(true);
    }
}

#ifndef VOXELLANCER_HEADLESS_SERVER
bool GamePlay::waitingForRemoteWorld() const {
    if (!m_runningState->remotePreviewActive()) {
        return false;
    }

    return !m_runningState->remoteWorldReady();
}

GamePlayFreecamInput& GamePlay::freecamInput() {
    return *m_freecamInput;
}

bool GamePlay::mouseCursorFree() const {
    return m_mouseCursorFree;
}

void GamePlay::resizeInput(unsigned int width, unsigned int height) {
    m_normalInput->onResizeEvent(width, height);
    m_freecamInput->onResizeEvent(width, height);
}

void GamePlay::setMouseCursorFree(bool free) {
    m_mouseCursorFree = free;
    refreshMouseCursorMode();
}

void GamePlay::refreshMouseCursorMode() {
    GLFWwindow* window = glfwGetCurrentContext();
    if (window == nullptr) {
        return;
    }

    const bool cursorVisible =
        m_game->mainMenuVisible() ||
        m_mouseCursorFree || m_scene->hudOverlay().focused();
    const int desiredMode = cursorVisible
        ? GLFW_CURSOR_NORMAL
        : GLFW_CURSOR_HIDDEN;
    if (glfwGetInputMode(window, GLFW_CURSOR) == desiredMode) {
        return;
    }

    if (!cursorVisible && glfwGetWindowAttrib(window, GLFW_FOCUSED)) {
        int width = 0;
        int height = 0;
        glfwGetWindowSize(window, &width, &height);
        glfwSetCursorPos(window, width / 2.0, height / 2.0);
    }
    glfwSetInputMode(window, GLFW_CURSOR, desiredMode);
}
#endif

const Scene& GamePlay::scene() const {
    return *m_scene;
}

const CameraHead& GamePlay::cameraHead() const {
    return m_freecamActive ? m_freecamInput->cameraHead() : World::instance()->player().cameraHead();
}

InputHandler& GamePlay::inputHandler() {
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (waitingForRemoteWorld()) {
        return *m_waitingInput;
    }
#endif
    if (m_freecamActive) {
        return *m_freecamInput.get();
    } else {
        return *m_normalInput.get();
    }
}

SoundManager& GamePlay::soundManager() {
    return *m_soundManager;
}

void GamePlay::setInitialScenario(int scenario) {
    m_initialScenario = scenario;
}

AiComparisonScenario* GamePlay::aiComparison() const {
    return dynamic_cast<AiComparisonScenario*>(m_scenario.get());
}

WeaponRangeScenario* GamePlay::weaponRange() const {
    return dynamic_cast<WeaponRangeScenario*>(m_scenario.get());
}

void GamePlay::loadScenario(int i) {
    TextureRenderer loadRenderer("data/textures/loading.dds");
    loadRenderer.display("Loading Scenario...");

    m_soundManager->stopAll();
#ifndef VOXELLANCER_HEADLESS_SERVER
    World& presentationWorld = *World::instance();
    if (presentationWorld.player().ship()) presentationWorld.player().setTarget(nullptr);
    presentationWorld.particleEngine().clear();
#endif
    if (m_scenario) {
        m_scenario->clear();
    }
    updateView();
    World& world = *World::instance();

    switch (i) {
    case 0:
        m_scenario.reset(new ScriptedScenario(world, "data/scripts/scenarios/demo.lua"));
        break;
    case 1:
        m_scenario.reset(new GameScenario(world));
        break;
    case BattleScenario::ScenarioId:
        m_scenario.reset(new BattleScenario(world));
        break;
    case 3:
        m_scenario.reset(new FrozenGameScenario(world));
        break;
    case 4:
        m_scenario.reset(new PirateScenario(world));
        break;
    case 5:
        m_scenario.reset(new FriendlyFireScenario(world));
        break;
    case ShieldShowcaseScenario::ScenarioId:
        m_scenario.reset(new ShieldShowcaseScenario(world));
        break;
    case LargeCombatScenario::ScenarioId:
        m_scenario.reset(new LargeCombatScenario(world));
        break;
    case AiComparisonScenario::ScenarioId:
        m_scenario.reset(new AiComparisonScenario(world));
        break;
    case WeaponRangeScenario::ScenarioId:
        m_scenario.reset(new WeaponRangeScenario(world));
        break;
    default:
        m_scenario.reset(new BaseScenario(world));
    }

    m_scenario->load();
}

#ifndef VOXELLANCER_HEADLESS_SERVER
void GamePlay::startLocalScenario(int scenario) {
    m_runningState->setLocalScenarioActive(true);
    if (m_freecamActive) setFreecamActive(false);
    m_scene->hudOverlay().setFocused(false);
    setMouseCursorFree(false);
    World::instance()->player().move(glm::vec3(0));
    World::instance()->player().rotate(glm::vec3(0));
    loadScenario(scenario);
}

void GamePlay::returnToContracts() {
    if (!m_runningState->localScenarioActive()) return;
    if (m_freecamActive) setFreecamActive(false);
    m_scene->hudOverlay().setFocused(false);
    setMouseCursorFree(false);
    World::instance()->player().move(glm::vec3(0));
    World::instance()->player().rotate(glm::vec3(0));
    loadScenario(99);
    createContractPresentation();
    m_runningState->setLocalScenarioActive(false);
}

void GamePlay::createContractPresentation() {
    World& world = *World::instance();
    Ship* ship = WorldObjectBuilder("contractship", world).buildShip();
    world.god().scheduleSpawn(ship);
    world.god().spawn();
    if (!world.entityId(ship)) {
        throw std::runtime_error("Could not create the contract camera presentation ship.");
    }
    world.player().setShip(ship);
}
#endif

void GamePlay::update(float deltaSec) {
    if (!m_game->mainMenuVisible()) {
        if (auto* range = weaponRange()) range->update(deltaSec);
    }
#ifndef VOXELLANCER_HEADLESS_SERVER
    refreshMouseCursorMode();
    if (m_scene->hudOverlay().focused()) {
        GLFWwindow* window = glfwGetCurrentContext();
        if (window != nullptr) {
            double cursorX = 0.0;
            double cursorY = 0.0;
            int width = 0;
            int height = 0;
            glfwGetCursorPos(window, &cursorX, &cursorY);
            glfwGetWindowSize(window, &width, &height);
            if (width > 0 && height > 0) {
                m_scene->hudOverlay().updatePointer(
                    static_cast<float>(cursorX / width),
                    static_cast<float>(cursorY / height));
            }
        }
    }
#endif
    if (m_game->mainMenuVisible()) {
        m_runningState->submitRemoteInput(glm::vec3(0), glm::vec3(0), false);
    } else {
        inputHandler().update(deltaSec);
    }

    if (m_freecamActive) {
        m_soundManager->setListener(m_freecamInput->cameraHead().position(), m_freecamInput->cameraHead().orientation());
    } else {
        m_soundManager->setListener(World::instance()->player().cameraHead().position(), World::instance()->player().cameraHead().orientation());
    }

    GameState::update(deltaSec);
    m_scene->update(deltaSec);
}

void GamePlay::onEntered() {
    GameState::onEntered();
    m_soundManager->activate();
    loadScenario(m_initialScenario);
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (m_game->mainMenuEnabled()) {
        createContractPresentation();
    }
    refreshMouseCursorMode();
#endif
}

void GamePlay::onLeft() {
    m_soundManager->deactivate();
#ifndef VOXELLANCER_HEADLESS_SERVER
    m_mouseCursorFree = false;
    m_scene->hudOverlay().setFocused(false);
    refreshMouseCursorMode();
#endif
    GameState::onLeft();
}

void GamePlay::updateView() {
    World::instance()->player().hud().setView(&m_game->viewer().view());
}
