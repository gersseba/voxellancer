#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <emscripten.h>
#include <emscripten/html5.h>

#include <filesystem>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <memory>
#include <stdexcept>

#include "client/localcontractsession.h"
#include "client/tcpclient.h"
#include "display/viewer.h"
#include "etc/contextprovider.h"
#include "gamestate/game.h"
#include "gamestate/gameplay/gameplay.h"
#include "gamestate/gameplay/gameplayscene.h"
#include "gamestate/gameplay/running/gameplayrunning.h"
#include "property/propertydirectory.h"
#include "property/propertymanager.h"
#include "utils/filesystem.h"
#include "web/effectlab.h"

extern "C" {
EM_JS(int, browserGameFocused, (), {
    return document.hasFocus() && document.activeElement === Module.canvas;
});
}

namespace {
std::unique_ptr<Game> game;
std::unique_ptr<LocalContractSession> session;
double previousTime = 0;

void reportError(const std::string& message) {
    std::cerr << message << '\n';
    EM_ASM({ Module.showNotice(UTF8ToString($0)); }, message.c_str());
}

void resize(GLFWwindow* window, int width, int height) {
    if (width <= 0 || height <= 0) return;
    int framebufferWidth = 0;
    int framebufferHeight = 0;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    if (framebufferWidth <= 0 || framebufferHeight <= 0) return;
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    game->gamePlay().resizeInput(width, height);
    game->viewer().setViewport(Viewport(0, 0, framebufferWidth, framebufferHeight));
}

EM_BOOL resizeCanvas(int, const EmscriptenUiEvent*, void*) {
    double width = 0;
    double height = 0;
    if (emscripten_get_element_css_size("#canvas", &width, &height) == EMSCRIPTEN_RESULT_SUCCESS &&
        width > 0 && height > 0) {
        glfwSetWindowSize(glfwGetCurrentContext(), static_cast<int>(width), static_cast<int>(height));
    }
    return EM_TRUE;
}

void onKey(GLFWwindow*, int key, int scan, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        if (!game->mainMenuVisible() && game->gamePlay().scene().closeEquipment()) return;
        game->toggleMainMenu();
        return;
    }
    game->inputHandler().onKeyEvent(key, scan, action, mods);
}

void frame() {
    try {
        GLFWwindow* window = glfwGetCurrentContext();
        if (glfwWindowShouldClose(window)) {
            emscripten_cancel_main_loop();
            game.reset();
            session.reset();
            ContextProvider::instance()->shutdown();
            glfwTerminate();
            reportError("Game exited. Reload the page to play again.");
            return;
        }
        const double now = glfwGetTime();
        const double delta = now - previousTime;
        previousTime = now;
        session->advance(delta);
        game->update(static_cast<float>(delta));
        game->draw();
        glfwSwapBuffers(window);
        glfwPollEvents();
    } catch (const std::exception& error) {
        emscripten_cancel_main_loop();
        reportError(std::string("Game stopped: ") + error.what());
    }
}
}

int main() {
    try {
        std::srand(static_cast<unsigned int>(std::time(nullptr)));
        PropertyManager::instance()->load("data/config.ini");
        const std::string controls = FileSystem::userConfigDir() + "/controls.ini";
        if (!FileSystem::exists(controls) &&
            !FileSystem::copyFile("data/controls.ini.default", controls)) {
            throw std::runtime_error("Could not initialize control settings.");
        }
        PropertyManager::instance()->load(controls);
        const std::string settings = FileSystem::userConfigDir() + "/settings.ini";
        if (FileSystem::exists(settings)) PropertyManager::instance()->load(settings);
        PropertyManager::instance()->load("data/voxels.ini", "voxels");
        if (!glfwInit()) throw std::runtime_error("Could not initialize the browser window.");
        glfwSetErrorCallback([](int, const char* message) { std::cerr << "Window: " << message << '\n'; });
        ContextProvider::instance()->setRequiredGLVersion(3, 0);
        ContextProvider::instance()->initWindowed();
        glewExperimental = GL_TRUE;
        if (glewContextInit() != GLEW_OK) throw std::runtime_error("Could not initialize WebGL 2.");
        glClearColor(0.0f, 0.0f, 0.3f, 1.0f);
        if (startEffectLab()) return 0;

        PropertyDirectory("data/worldobjects").read();
        PropertyDirectory("data/equipment/engines").read();
        PropertyDirectory("data/equipment/shields").read();
        PropertyDirectory("data/equipment/weapons").read();
        PropertyDirectory("data/equipment/projectiles").read();

        game = std::make_unique<Game>(false);
        game->enableMainMenu();
        session = std::make_unique<LocalContractSession>(
            std::filesystem::path(FileSystem::userConfigDir()) / "solo-contract.checkpoint");
        if (!game->gamePlay().running().connectRemotePreview(session->takeClient()))
            throw std::runtime_error("Could not initialize the local contracts session.");
        GLFWwindow* window = glfwGetCurrentContext();
        glfwSetKeyCallback(window, onKey);
        glfwSetMouseButtonCallback(window, [](GLFWwindow*, int button, int action, int mods) {
            game->inputHandler().onMouseButtonEvent(button, action, mods);
        });
        glfwSetWindowSizeCallback(window, resize);
        emscripten_set_resize_callback(EMSCRIPTEN_EVENT_TARGET_WINDOW, nullptr, false, resizeCanvas);
        resizeCanvas(0, nullptr, nullptr);
        int width = 0;
        int height = 0;
        glfwGetWindowSize(window, &width, &height);
        game->gamePlay().updateView();
        resize(window, width, height);
        previousTime = glfwGetTime();
        EM_ASM({ Module.setStatus(""); Module.canvas.focus(); });
        emscripten_set_main_loop(frame, 0, false);
        return 0;
    } catch (const std::exception& error) {
        reportError(std::string("Could not start Voxellancer: ") + error.what());
        return 1;
    }
}
