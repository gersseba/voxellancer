#include "game.h"

#include <GL/glew.h>

#include "etc/contextprovider.h"

#include "display/viewer.h"

#include "utils/statemachine/trigger.h"
#include "utils/statemachine/triggeredtransition.h"

#include "gamestate/gameplay/gameplay.h"
#include "gamestate/gameplay/gameplayscene.h"
#include "gamestate/intro/intro.h"


Game::Game(bool showIntro):
    GameState("Game", nullptr),
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
    assert(currentSubState());
    GameState* subState = dynamic_cast<GameState*>(currentSubState());
    return subState->inputHandler();
}

Viewer& Game::viewer() {
    return *m_viewer;
}

void Game::update(float deltaSec) {
    GameState::update(deltaSec);

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

    m_viewer->draw(scene(), cameraHead());
}
