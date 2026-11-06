#include "gameplay.h"

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

#include "sound/soundmanager.h"

#include "voxeleffect/voxelparticleengine.h"

#include "world/world.h"

#include "player.h"
#include "camera/camerahead.h"
#include "camera/cameradolly.h"
#include "ui/hud/hud.h"
#include "display/viewer.h"

#include "display/rendering/texturerenderer.h"


GamePlay::GamePlay(Game* game) :
    GameState("In Game", game),
    m_game(game),
    m_scene(new GamePlayScene(*this)),
    m_soundManager(new SoundManager()),
    m_runningState(new GamePlayRunning(this, *World::instance())),
    m_pausedState(new GamePlayPaused(this)),
    m_normalInput(new GamePlayNormalInput(*this)),
    m_freecamInput(new GamePlayFreecamInput(*this)),
    m_freecamActive(false),
    m_initialScenario(0)
{
    updateView();
    setInitialSubState(m_runningState);

    m_runningState->pauseTrigger().setTarget(new TriggeredTransition(m_runningState, m_pausedState));
    m_pausedState->continueTrigger().setTarget(new TriggeredTransition(m_pausedState, m_runningState));
}

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

    const auto lobby = m_runningState->remoteLobbyStatus();
    return !lobby || !lobby->worldStarted ||
        !m_runningState->remotePreviewSnapshot().has_value();
}

GamePlayFreecamInput& GamePlay::freecamInput() {
    return *m_freecamInput;
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
        return m_waitingInput;
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

void GamePlay::loadScenario(int i) {
    TextureRenderer loadRenderer("data/textures/loading.dds");
    loadRenderer.display("Loading Scenario...");

    m_soundManager->stopAll();
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
    case LargeCombatScenario::ScenarioId:
        m_scenario.reset(new LargeCombatScenario(world));
        break;
    default:
        m_scenario.reset(new BaseScenario(world));
    }

    m_scenario->load();
}

void GamePlay::update(float deltaSec) {
    inputHandler().update(deltaSec);

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
}

void GamePlay::onLeft() {
    m_soundManager->deactivate();
    GameState::onLeft();
}

void GamePlay::updateView() {
    World::instance()->player().hud().setView(&m_game->viewer().view());
}
