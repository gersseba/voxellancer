#pragma once

#include "input/inputhandler.h"


class GamePlay;

class GamePlayInput : public InputHandler {
public:
    GamePlayInput(GamePlay& gamePlay, bool waitingOnly = false);

    void onResizeEvent(const unsigned int width, const unsigned int height);
    void onKeyEvent(int key, int scancode, int action, int mods) override;
    void onMouseButtonEvent(int button, int action, int mods);

    virtual void update(float deltaSec);


protected:
    GamePlay& m_gamePlay;

    bool handleHudOverlayMouseButtonEvent(int button, int action, int mods);
    bool mouseInputCaptured() const;

    bool m_waitingOnly;
};
