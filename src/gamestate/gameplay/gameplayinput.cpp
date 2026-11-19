#include "gameplayinput.h"

#include "GLFW/glfw3.h"

#include "gameplay.h"
#include "gameplayscene.h"
#include "ui/hud/hudoverlay.h"


GamePlayInput::GamePlayInput(GamePlay& gamePlay, bool waitingOnly):
    m_gamePlay(gamePlay),
    m_waitingOnly(waitingOnly)
{

}

void GamePlayInput::onResizeEvent(const unsigned int width, const unsigned int height) {

}

void GamePlayInput::onKeyEvent(int key, int scancode, int action, int mods) {
    if (action == GLFW_PRESS) {
        if (key == GLFW_KEY_M) {
            m_gamePlay.setMouseCursorFree(!m_gamePlay.mouseCursorFree());
            return;
        }
        if (m_waitingOnly) {
            return;
        }

        if (key == GLFW_KEY_F) {
            m_gamePlay.setFreecamActive(!m_gamePlay.freecamActive());
        }

        if (key >= GLFW_KEY_1 && key <= GLFW_KEY_9) {
            m_gamePlay.scene().setOutputBuffer(key - GLFW_KEY_1);
        }

        if (key == GLFW_KEY_T) {
            m_gamePlay.scene().setWorldTreeRendererEnabled(!m_gamePlay.scene().worldTreeRendererEnabled());
        }

        if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F7) {
            m_gamePlay.loadScenario(key - GLFW_KEY_F1);
        }
    }
}

void GamePlayInput::onMouseButtonEvent(int button, int action, int mods) {
    handleHudOverlayMouseButtonEvent(button, action, mods);
}

bool GamePlayInput::handleHudOverlayMouseButtonEvent(
    int button,
    int action,
    int mods)
{
    (void)mods;
    GLFWwindow* window = glfwGetCurrentContext();
    if (window == nullptr) {
        return m_gamePlay.mouseCursorFree() ||
            m_gamePlay.scene().hudOverlay().focused();
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    int width = 0;
    int height = 0;
    glfwGetCursorPos(window, &cursorX, &cursorY);
    glfwGetWindowSize(window, &width, &height);
    const bool overlayCaptured =
        m_gamePlay.scene().hudOverlay().handleMouseButtonEvent(
            button,
            action,
            cursorX,
            cursorY,
            width,
            height,
            m_gamePlay.mouseCursorFree());
    return overlayCaptured || m_gamePlay.mouseCursorFree();
}

bool GamePlayInput::mouseInputCaptured() const {
    return m_gamePlay.mouseCursorFree() ||
        m_gamePlay.scene().hudOverlay().focused();
}

void GamePlayInput::update(float deltaSec) {

}
