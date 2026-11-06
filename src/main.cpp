#include <omp.h>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef WIN32
#include <windows.h>
#endif

#include <GL/glew.h>

#ifdef WIN32
#include <GL/wglew.h>
#endif

#include <GLFW/glfw3.h>
#include <SFML/Network/TcpListener.hpp>

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
#include "client/localserverprocess.h"
#include "client/tcpclient.h"
#include "benchmark/benchmarkmode.h"
#include "benchmark/benchmarkrunner.h"
#include "scenarios/battlescenario.h"
#include "scenarios/largecombatscenario.h"

#include "display/stereorenderinfo.h"
#include "display/viewer.h"

#include "property/propertydirectory.h"
#include "utils/filesystem.h"

#include "gamestate/gameplay/gameplay.h"
#include "gamestate/gameplay/running/gameplayrunning.h"
#include "gamestate/gameplay/gameplayinput.h"
#include "gamestate/gameplay/input/gameplayfreecaminput.h"
#include "gamestate/game.h"
#include "gamestate/gameplay/gameplayscene.h"

#include "world/world.h"

#include "display/rendering/texturerenderer.h"

static GLint MajorVersionRequire = 3;
static GLint MinorVersionRequire = 1;

static Game* game;

namespace {
    std::string benchmarkIdentifier(std::string value) {
        for (char& character : value) {
            const bool valid =
                (character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') ||
                character == '-' || character == '_';
            if (!valid) {
                character = '_';
            }
        }
        return value;
    }

    std::string newBenchmarkRunId() {
        return std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }
}

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

static std::uint16_t availableLocalPort() {
    sf::TcpListener listener;
    if (listener.listen(0, sf::IpAddress::LocalHost) !=
        sf::Socket::Status::Done) {
        throw std::runtime_error(
            "Could not reserve a port for the local simulation server.");
    }
    const std::uint16_t port = listener.getLocalPort();
    listener.close();
    return port;
}

static std::unique_ptr<TcpClient> connectToLocalServer(
    LocalServerProcess& server,
    std::uint16_t port)
{
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(10);
    std::string lastConnectionError;
    while (std::chrono::steady_clock::now() < deadline) {
        auto client = std::make_unique<TcpClient>();
        if (client->connectLocal(port)) {
            return client;
        }
        lastConnectionError = client->lastError();
        if (!server.isRunning()) {
            throw std::runtime_error(
                "The local simulation server exited during startup. " +
                server.lastError());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    throw std::runtime_error(
        "Timed out waiting for the local simulation server: " +
        lastConnectionError);
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
        std::cerr << "Invalid benchmark mode.\n";
        delete initTimer;
        return 1;
    }
    if (clParser.localSingleplayer() && clParser.serverPreview()) {
        std::cerr << "-local-singleplayer cannot be combined with "
            "-server-preview.\n";
        delete initTimer;
        return 1;
    }
    if (clParser.localSingleplayer() && clParser.multiplayerClient()) {
        std::cerr << "-local-singleplayer cannot be combined with "
            "-multiplayer-client.\n";
        delete initTimer;
        return 1;
    }
    if (clParser.multiplayerClient() && clParser.serverPreview()) {
        std::cerr << "-multiplayer-client cannot be combined with "
            "-server-preview.\n";
        delete initTimer;
        return 1;
    }
    if (benchmarkMode != BenchmarkMode::None &&
        !clParser.localSingleplayer() && !clParser.multiplayerClient()) {
        std::cerr << "-benchmark-large-combat requires -local-singleplayer "
            "or -multiplayer-client.\n";
        delete initTimer;
        return 1;
    }

    const std::string runId = benchmarkIdentifier(
        clParser.benchmarkRunId().empty()
            ? newBenchmarkRunId()
            : clParser.benchmarkRunId());
    const std::string clientId = benchmarkIdentifier(
        clParser.benchmarkClientId().empty()
            ? (clParser.localSingleplayer() ? "integrated-client" : "client")
            : clParser.benchmarkClientId());

    LocalServerProcess localServer;
    std::unique_ptr<TcpClient> preconnectedClient;
    if (clParser.localSingleplayer()) {
        try {
            const std::uint16_t port = availableLocalPort();
            if (!localServer.start(
                    std::filesystem::absolute(argv[0]),
                    port,
                    benchmarkMode == BenchmarkMode::LargeCombat,
                    runId)) {
                throw std::runtime_error(localServer.lastError());
            }
            preconnectedClient = connectToLocalServer(localServer, port);
        } catch (const std::exception& exception) {
            std::cerr << "Could not start local singleplayer: "
                << exception.what() << '\n';
            delete initTimer;
            return 1;
        }
    }

    const unsigned int randomSeed = benchmarkMode == BenchmarkMode::None
        ? static_cast<unsigned int>(std::time(nullptr))
        : LargeCombatScenario::SimulationRandomSeed;
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
        std::cerr << "Could not init GLFW.\n";
        delete initTimer;
        return 1;
    }

    glfwSetErrorCallback(errorCallback);

    ContextProvider::instance()->setRequiredGLVersion(MajorVersionRequire, MinorVersionRequire);
    if (clParser.fullScreen()) {
        ContextProvider::instance()->initFullScreen(1);
    } else if (clParser.multiplayerClient()) {
        const GLFWvidmode* mode = glfwGetVideoMode(glfwGetPrimaryMonitor());
        if (mode == nullptr) {
            std::cerr << "Could not read the primary monitor size.\n";
            glfwTerminate();
            delete initTimer;
            return 1;
        }
        const int halfWidth = mode->width / 2;
        const int halfHeight = mode->height / 2;
        const bool secondWindow = clientId == "client-2";
        ContextProvider::instance()->initWindowed(
            Size<int>(halfWidth, halfHeight),
            Size<int>(secondWindow ? halfWidth : 0, 0));
    } else {
        ContextProvider::instance()->initWindowed();
    }

    GLFWwindow* window = glfwGetCurrentContext();
    if (clParser.multiplayerClient()) {
        glfwSetWindowTitle(
            window,
            ("Voxellancer - " + clientId).c_str());
    }
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);


    setGLFWCallbacks(window);
    checkGLVersion();
    checkOpenMP();

    glewExperimental = GL_TRUE;
    if (glewInit() != GLEW_OK) {
        glow::fatal("glewInit() failed");
    }
    CheckGLError();

    const bool enableVsync = true;
    const bool swapIntervalSet = miscSettings(enableVsync);
    if (benchmarkMode == BenchmarkMode::LargeCombat && !swapIntervalSet) {
        std::cerr << "Could not enable VSync for the large-combat benchmark.\n";
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
            !clParser.multiplayerClient() &&
            !clParser.localSingleplayer() &&
            clParser.showIntro() && Property<bool>::get("general.showIntro", true));

        if (benchmarkMode == BenchmarkMode::LargeCombat ||
            clParser.multiplayerClient()) {
            game->gamePlay().setInitialScenario(99);
        }

        if (clParser.localSingleplayer()) {
            if (benchmarkMode == BenchmarkMode::None) {
                game->gamePlay().setInitialScenario(BattleScenario::ScenarioId);
            }
            if (!game->gamePlay().running().connectRemotePreview(
                    std::move(preconnectedClient))) {
                std::cerr << "Could not initialize the local simulation client.\n";
                exitCode = 1;
            }
        } else if (clParser.serverPreview()) {
            game->gamePlay().running().connectRemotePreview(27800);
        } else if (clParser.multiplayerClient()) {
            if (!game->gamePlay().running().connectRemotePreview(27801)) {
                std::cerr << "Could not connect to multiplayer server.\n";
                exitCode = 1;
            } else {
                game->gamePlay().setFreecamActive(true);
                game->gamePlay().freecamInput().setPosition(
                        glm::vec3(0.0f, 0.0f, 1800.0f));
            }
        }

        if(clParser.stereoView()) {
            game->viewer().switchToStereoView(StereoRenderInfo::dummy());
        }
        game->gamePlay().updateView();

        delete loadingScreen;
        delete initTimer;

        if (benchmarkMode == BenchmarkMode::None && exitCode == 0) {
            mainloop();
        } else {
            if (benchmarkMode != BenchmarkMode::None && exitCode == 0) {
                exitCode = BenchmarkRunner(benchmarkMode, runId, clientId).run(*game);
            }
        }

        if (clParser.localSingleplayer() &&
            benchmarkMode == BenchmarkMode::LargeCombat &&
            exitCode == 0) {
            if (!localServer.waitForExit(std::chrono::seconds(60))) {
                std::cerr << "Timed out waiting for the integrated benchmark server "
                    "to finish: " << localServer.lastError() << '\n';
                exitCode = 1;
            } else {
                const std::filesystem::path serverReport =
                    std::filesystem::path("benchmark-results") /
                    ("large-combat-" + runId + "-server.json");
                if (!std::filesystem::exists(serverReport)) {
                    std::cerr << "Integrated benchmark server finished without "
                        "writing " << serverReport.string() << ".\n";
                    exitCode = 1;
                }
            }
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
    localServer.stop();
    ContextProvider::instance()->shutdown();
    glfwTerminate();
    return exitCode;
}
