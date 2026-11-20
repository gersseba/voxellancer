#include "gameplayinput.h"
#include "scenarios/aicomparisonscenario.h"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include "gameplay.h"
#include "gamestate/game.h"
#include "gameplayscene.h"
#include "running/gameplayrunning.h"
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
        if (auto* comparison = m_gamePlay.aiComparison()) {
            if (key == GLFW_KEY_COMMA || key == GLFW_KEY_PERIOD) {
                if (m_gamePlay.freecamActive()) m_gamePlay.setFreecamActive(false);
                const auto count = AiComparisonScenario::PairCount;
                const auto step = key == GLFW_KEY_COMMA ? count-1 : 1;
                comparison->watchPair((comparison->watchedPair()+step)%count);
                return;
            }
            if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9) {
                if (m_gamePlay.freecamActive()) m_gamePlay.setFreecamActive(false);
                comparison->watchPair(key == GLFW_KEY_0 ? 9 : key-GLFW_KEY_1);
                return;
            }
        }
        if (!m_waitingOnly && m_gamePlay.scene().stationKey(key)) return;
        if (key == GLFW_KEY_M) {
            m_gamePlay.setMouseCursorFree(!m_gamePlay.mouseCursorFree());
            return;
        }
        if (m_waitingOnly) {
            return;
        }
        const auto contract = m_gamePlay.running().contractStatus();
        if (contract) {
            using namespace voxellancer::protocol;
            if (key == GLFW_KEY_O || key == GLFW_KEY_TAB) {
                auto& overlay = m_gamePlay.scene().hudOverlay();
                const bool show = key == GLFW_KEY_O ||
                    !overlay.visible() || overlay.minimized();
                overlay.setVisible(show);
                overlay.setMinimized(false);
                m_gamePlay.setMouseCursorFree(show);
                return;
            }
            if (contract->phase == ContractPhase::Station) {
                if (key == GLFW_KEY_ENTER) {
                    m_gamePlay.running().submitContractAction(ContractActionKind::Ready);
                    return;
                }
                if (key == GLFW_KEY_B || key == GLFW_KEY_N) {
                    m_gamePlay.running().submitContractAction(key == GLFW_KEY_B
                        ? ContractActionKind::BuyGun : ContractActionKind::BuyShield);
                    return;
                }
            }
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

        if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F7 &&
            !m_gamePlay.game()->mainMenuEnabled()) {
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
    if (width > 0 && height > 0 &&
        m_gamePlay.scene().stationMouse(button, action,
            glm::vec2(cursorX / width, cursorY / height))) return true;
    auto& overlay = m_gamePlay.scene().hudOverlay();
    const bool overlayCaptured =
        overlay.handleMouseButtonEvent(
            button,
            action,
            cursorX,
            cursorY,
            width,
            height,
            m_gamePlay.mouseCursorFree());
    if (overlayCaptured && (!overlay.visible() || overlay.minimized())) {
        m_gamePlay.setMouseCursorFree(false);
    }
    return overlayCaptured || m_gamePlay.mouseCursorFree();
}

bool GamePlayInput::mouseInputCaptured() const {
    return m_gamePlay.mouseCursorFree() ||
        m_gamePlay.scene().hudOverlay().focused();
}

void GamePlayInput::update(float deltaSec) {

}
