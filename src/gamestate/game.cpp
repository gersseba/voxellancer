#include "game.h"

#include <GL/glew.h>

#include "etc/contextprovider.h"

#include "display/viewer.h"

#include "utils/statemachine/trigger.h"
#include "utils/statemachine/triggeredtransition.h"

#include "gamestate/gameplay/gameplay.h"
#include "gamestate/gameplay/gameplayscene.h"
#include "gamestate/gameplay/running/gameplayrunning.h"
#include "gamestate/intro/intro.h"
#include "ui/mainmenu.h"
#include "settings/settingsstore.h"
#include "utils/filesystem.h"


Game::Game(bool showIntro):
    GameState("Game", nullptr),
    m_settings(std::make_unique<SettingsStore>(FileSystem::userConfigDir() + "/settings.ini")),
    m_viewer(new Viewer(Viewport(0, 0, ContextProvider::instance()->resolution().width(), ContextProvider::instance()->resolution().height()))),
    m_gamePlay(new GamePlay(this)),
    m_intro(new Intro(this)),
    m_lastDrawTime(),
    m_smoothedFps(0.0f)
{
    if (showIntro) {
        setInitialSubState(m_intro);
        m_intro->overTrigger().setTarget(new TriggeredTransition(m_intro, m_gamePlay));
    } else {
        setInitialSubState(m_gamePlay);
    }
}

Game::~Game() = default;

GamePlay& Game::gamePlay() {
    return *m_gamePlay;
}

const Scene& Game::scene() const {
    assert(currentSubState());
    const GameState* subState = dynamic_cast<const GameState*>(currentSubState());
    return subState->scene();
}

const CameraHead& Game::cameraHead() const {
    assert(currentSubState());
    const GameState* subState = dynamic_cast<const GameState*>(currentSubState());
    return subState->cameraHead();
}

InputHandler& Game::inputHandler() {
    if (mainMenuVisible()) {
        return *m_mainMenu;
    }
    assert(currentSubState());
    GameState* subState = dynamic_cast<GameState*>(currentSubState());
    return subState->inputHandler();
}

Viewer& Game::viewer() {
    return *m_viewer;
}

SettingsStore& Game::settings() { return *m_settings; }

void Game::enableMainMenu() {
    m_mainMenu = std::make_unique<MainMenu>(
        *m_settings,
        [this]() { m_gamePlay->returnToContracts(); },
        [this](int scenario) { m_gamePlay->startLocalScenario(scenario); });
    setInitialSubState(m_gamePlay);
    m_gamePlay->setInitialScenario(99);
}

bool Game::mainMenuEnabled() const {
    return m_mainMenu != nullptr;
}

bool Game::mainMenuVisible() const {
    return m_mainMenu && m_mainMenu->visible();
}

void Game::toggleMainMenu() {
    if (m_mainMenu) {
        m_mainMenu->toggle();
        m_gamePlay->refreshMouseCursorMode();
    }
}

void Game::setConnectionError(const std::string& error) {
    m_connectionError = error;
}

void Game::update(float deltaSec) {
    GameState::update(deltaSec);

    if (m_mainMenu) {
        const auto& running = m_gamePlay->running();
        const auto lobby = running.remoteLobbyStatus();
        const bool ready = running.connectedContractStatus().has_value() &&
            running.remoteWorldReady() &&
            lobby && lobby->worldStarted && !running.contractSessionEnded();
        std::string note = "Connecting to Contracts server. Scenarios are available offline.";
        if (!m_connectionError.empty()) {
            note = m_connectionError + " Restart the game to retry.";
        } else if (!running.remoteConnectionError().empty()) {
            note = running.remoteConnectionError() + " Restart to reconnect.";
        } else if (ready) {
            note = "Server connected. Contracts ready.";
        } else if (running.remoteWorldReady() && !running.connectedContractStatus()) {
            note = "Server connected, but contracts are not ready. Use a contracts server.";
        } else if (lobby && !lobby->worldStarted) {
            note = "Waiting for pilots: " + std::to_string(lobby->connectedPlayers) +
                "/" + std::to_string(lobby->targetPlayers) +
                ". Contracts unlock when the server is ready.";
        }
        m_mainMenu->setConnection(ready, note);
        if (mainMenuVisible()) {
            m_mainMenu->update(deltaSec);
        }
        m_gamePlay->refreshMouseCursorMode();
    }
    m_viewer->update(deltaSec);
}

void Game::draw() {
    const auto now = std::chrono::steady_clock::now();
    if (m_lastDrawTime.time_since_epoch().count() != 0) {
        const std::chrono::duration<float> frameDuration = now - m_lastDrawTime;
        if (frameDuration.count() > 0.0f) {
            const float instantaneousFps = 1.0f / frameDuration.count();
            m_smoothedFps = m_smoothedFps == 0.0f
                ? instantaneousFps
                : glm::mix(m_smoothedFps, instantaneousFps, 0.15f);
        }
    }
    m_lastDrawTime = now;
    m_gamePlay->scene().setFps(m_smoothedFps);

    if (mainMenuVisible()) {
        m_mainMenu->draw();
        return;
    }
    m_viewer->draw(scene(), cameraHead());
}
