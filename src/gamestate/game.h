#pragma once

#include <chrono>
#include <memory>

#include "gamestate.h"


class GamePlay;
class Intro;
class Viewer;
class MainMenu;
class SettingsStore;

/**
 * Mainstate of the Game, entered once when libraries and context are setup
 * and left just before they are teared down again
 */
class Game: public GameState {
public:
    Game(bool showIntro);
    ~Game();

    GamePlay& gamePlay();

    virtual const Scene& scene() const override;
    virtual const CameraHead& cameraHead() const override;

    virtual InputHandler& inputHandler() override;

    Viewer& viewer();
    SettingsStore& settings();
    void enableMainMenu();
    bool mainMenuEnabled() const;
    bool mainMenuVisible() const;
    void toggleMainMenu();
    void setConnectionError(const std::string& error);

    virtual void update(float deltaSec) override;
    void draw();


protected:
    std::unique_ptr<SettingsStore> m_settings;
    std::unique_ptr<Viewer> m_viewer;

    GamePlay* m_gamePlay;
    Intro* m_intro;
    std::chrono::steady_clock::time_point m_lastDrawTime;
    float m_smoothedFps;
    std::unique_ptr<MainMenu> m_mainMenu;
    std::string m_connectionError;
};
