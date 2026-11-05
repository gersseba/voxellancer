#include <omp.h>
#include <cstdlib>
#include <ctime>
#include <iostream>

#ifdef WIN32
#include <windows.h>
#endif

#include <GL/glew.h>

#ifdef WIN32
#include <GL/wglew.h>
#endif

#include <GLFW/glfw3.h>

#include <glow/Version.h>
#include <glow/global.h>
#include <glow/logging.h>
#include <glow/debugmessageoutput.h>
#include <glow/Program.h>
#include <glowutils/global.h>
#include <glowutils/File.h>
#include <glowutils/AutoTimer.h>

#include "etc/contextprovider.h"
#include "etc/cli/commandlineparser.h"
#include "benchmark/benchmarkmode.h"
#include "benchmark/benchmarkrunner.h"
#include "scenarios/performancebenchmarkscenario.h"

#include "display/stereorenderinfo.h"
#include "display/viewer.h"

#include "property/propertydirectory.h"
#include "utils/filesystem.h"

#include "gamestate/gameplay/gameplay.h"
#include "gamestate/gameplay/running/gameplayrunning.h"
#include "gamestate/gameplay/gameplayinput.h"
#include "gamestate/game.h"
#include "gamestate/gameplay/gameplayscene.h"

#include "world/world.h"

#include "display/rendering/texturerenderer.h"

static GLint MajorVersionRequire = 3;
static GLint MinorVersionRequire = 1;

static Game* game;

static void checkGLVersion() {
    glow::info("OpenGL Version Needed %;.%; (%;.%; Found)",
        MajorVersionRequire, MinorVersionRequire,
        glow::version().majorVersion, glow::version().minorVersion);
    glow::info("version %;", glow::versionString());
    glow::info("vendor: %;", glow::vendor());
    glow::info("renderer %;", glow::renderer());
    glow::info("core profile: %;", glow::isCoreProfile() ? "true" : "false");
    glow::info("GLSL version: %;", glow::getString(GL_SHADING_LANGUAGE_VERSION));
    glow::info("GL Versionstring: %;\n", glow::versionString());
}

static void checkOpenMP() {
    //omp_set_num_threads(4);
#pragma omp parallel
    {
#pragma omp master
        glow::debug("OpenMP with %; threads ", omp_get_num_threads());
    }
}

static void errorCallback(int error, const char* description) {
    glow::warning(description);
}

static void resizeCallback(GLFWwindow* window, int width, int height) {
    glow::info("Resizing viewport to %;x%;", width, height);
    if (width > 0 && height > 0) {
        glViewport(0, 0, width, height);
        game->inputHandler().onResizeEvent(width, height);
        game->viewer().setViewport(Viewport(0, 0, width, height));
    }
}

static void toggleFullScreen();

static void onKeyEvent(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        glfwSetWindowShouldClose(window, GL_TRUE);
    }
    if ((key == GLFW_KEY_ENTER && action == GLFW_PRESS) &&
        ((glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS) ||
        (glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS))) {
        toggleFullScreen();
    }

    if (key == GLFW_KEY_F8 && action == GLFW_PRESS) {
        glowutils::File::reloadAll();
        PropertyManager::instance()->load("data/config.ini");
    }
    if (key == GLFW_KEY_F9 && action == GLFW_PRESS) {
        World::instance()->printStatus();
    }

    game->inputHandler().onKeyEvent(key, scancode, action, mods);
}


static void onMouseButtonEvent(GLFWwindow* window, int button, int action, int mods) {
    game->inputHandler().onMouseButtonEvent(button, action, mods);
}

void setGLFWCallbacks(GLFWwindow* window) {
    glfwSetKeyCallback(window, onKeyEvent);
    glfwSetWindowSizeCallback(window, resizeCallback);
    glfwSetMouseButtonCallback(window, onMouseButtonEvent);
}

static bool miscSettings(bool enableVsync) {
    glClearColor(0.0f, 0.0f, 0.3f, 1.0f);

#ifdef WIN32 // TODO: find a way to correctly detect debug extension in linux
    glow::debugmessageoutput::enable();
#endif

#ifdef WIN32
    if (!wglSwapIntervalEXT || !wglSwapIntervalEXT(enableVsync ? 1 : 0)) {
        return false;
    }
#else
    glfwSwapInterval(enableVsync ? 1 : 0);
    const char* errorDescription = nullptr;
    if (glfwGetError(&errorDescription) != GLFW_NO_ERROR) {
        return false;
    }
#endif
    return true;
}

static void mainloop() {
    glow::debug("Entering mainloop");
    double time = glfwGetTime();

    while (!glfwWindowShouldClose(glfwGetCurrentContext())) {
        double delta = glfwGetTime() - time;
        time += delta;

        game->update(static_cast<float>(delta));
        game->draw();

        glfwSwapBuffers(glfwGetCurrentContext());
        glfwPollEvents();
    }
}

void toggleFullScreen() {
    ContextProvider::instance()->toggleFullScreen();

    GLFWwindow* window = glfwGetCurrentContext();
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

    setGLFWCallbacks(window);

    // not checking version etc again

    Size<int> res = ContextProvider::instance()->resolution();
    resizeCallback(window, res.width(), res.height());
}

int main(int argc, char* argv[]) {
    glowutils::AutoTimer* initTimer = new glowutils::AutoTimer("Setup");
    int exitCode = 0;
    CommandLineParser clParser;
    clParser.parse(argc, argv);
    const BenchmarkMode benchmarkMode = clParser.benchmarkMode();
    if (benchmarkMode == BenchmarkMode::Invalid) {
        std::cerr << "Choose only one benchmark mode: -benchmark-rendering, -benchmark-combat, or -benchmark-large-combat.\n";
        delete initTimer;
        return 1;
    }
    const unsigned int randomSeed = benchmarkMode == BenchmarkMode::None
        ? static_cast<unsigned int>(std::time(nullptr))
        : PerformanceBenchmarkScenario::SimulationRandomSeed;
    std::srand(randomSeed);

    PropertyManager::instance()->load("data/config.ini");
    glow::info("Config Directory: %;", FileSystem::userConfigDir());
    std::string controlsConfig = FileSystem::userConfigDir() + "/controls.ini";
    if (!FileSystem::exists(controlsConfig)) {
        FileSystem::copyFile("data/controls.ini.default", controlsConfig);
    }
    PropertyManager::instance()->load(controlsConfig);
    PropertyManager::instance()->load("data/voxels.ini", "voxels");

    if (!glfwInit()) {
        glow::fatal("Could not init GLFW");
        exit(-1);
    }

    glfwSetErrorCallback(errorCallback);

    ContextProvider::instance()->setRequiredGLVersion(MajorVersionRequire, MinorVersionRequire);
    if(clParser.fullScreen()) {
        ContextProvider::instance()->initFullScreen(1);
    } else {
        ContextProvider::instance()->initWindowed();
    }

    GLFWwindow* window = glfwGetCurrentContext();
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);


    setGLFWCallbacks(window);
    checkGLVersion();
    checkOpenMP();

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        glow::fatal("glewInit() failed");
    }
    CheckGLError();

    const bool swapIntervalSet = miscSettings(benchmarkMode == BenchmarkMode::None ||
        benchmarkMode == BenchmarkMode::LargeCombatVsync);
    if (benchmarkMode == BenchmarkMode::LargeCombatVsync && !swapIntervalSet) {
        std::cerr << "Could not enable VSync for the large combat benchmark.\n";
        ContextProvider::instance()->shutdown();
        glfwTerminate();
        delete initTimer;
        return 1;
    }

//#define TRYCATCH

#ifdef TRYCATCH
    try {
#endif
        TextureRenderer* loadingScreen = new TextureRenderer("data/textures/loading.dds");

        loadingScreen->display("Loading... Objects");
        PropertyDirectory("data/worldobjects").read();
        loadingScreen->display("Loading... Engines");
        PropertyDirectory("data/equipment/engines").read();
        loadingScreen->display("Loading... Shields");
        PropertyDirectory("data/equipment/shields").read();
        loadingScreen->display("Loading... Weapons");
        PropertyDirectory("data/equipment/weapons").read();
        loadingScreen->display("Loading... Projectiles");
        PropertyDirectory("data/equipment/projectiles").read();

        loadingScreen->display("Loading... Game");
        game = new Game(benchmarkMode == BenchmarkMode::None &&
            clParser.showIntro() && Property<bool>::get("general.showIntro", true));

        if (benchmarkMode == BenchmarkMode::Rendering) {
            game->gamePlay().setInitialScenario(PerformanceBenchmarkScenario::RenderingScenarioId);
        } else if (benchmarkMode == BenchmarkMode::Combat) {
            game->gamePlay().setInitialScenario(PerformanceBenchmarkScenario::CombatScenarioId);
        } else if (benchmarkMode == BenchmarkMode::LargeCombatVsync) {
            game->gamePlay().setInitialScenario(PerformanceBenchmarkScenario::LargeCombatVsyncScenarioId);
        }

        if(clParser.stereoView()) {
            game->viewer().switchToStereoView(StereoRenderInfo::dummy());
        }
        game->gamePlay().updateView();

        delete loadingScreen;
        delete initTimer;

        if (benchmarkMode == BenchmarkMode::None) {
            mainloop();
        } else {
            exitCode = BenchmarkRunner(benchmarkMode).run(*game);
        }

#ifdef TRYCATCH
    }
    catch (std::exception &e) {
        glfwDestroyWindow(window);
        glfwTerminate();
        glow::fatal("Termination after Exception: %;", e.what());
        std::cout << "Hit enter to quit" << std::endl;
        std::cin.ignore(1, '\n');
    }
#endif

    delete game;
    ContextProvider::instance()->shutdown();
    glfwTerminate();
    return exitCode;
}
