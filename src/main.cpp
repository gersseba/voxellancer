#include <omp.h>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <ctime>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#ifdef WIN32
#include <windows.h>
#endif

#include <GL/glew.h>

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
#include "scenarios/shieldshowcasescenario.h"
#include "scenarios/aicomparisonscenario.h"

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

#if defined(__APPLE__)
static GLint MajorVersionRequire = 4;
static GLint MinorVersionRequire = 1;
#else
static GLint MajorVersionRequire = 3;
static GLint MinorVersionRequire = 1;
#endif

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
        int framebufferWidth, framebufferHeight;
        glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
        if (framebufferWidth <= 0 || framebufferHeight <= 0) {
            return;
        }
        glViewport(0, 0, framebufferWidth, framebufferHeight);
        game->gamePlay().resizeInput(width, height);
        game->viewer().setViewport(Viewport(0, 0, framebufferWidth, framebufferHeight));
    }
}

static void toggleFullScreen();

static void onKeyEvent(GLFWwindow* window, int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        if (game && game->mainMenuEnabled()) {
            if (!game->mainMenuVisible() && game->gamePlay().scene().closeEquipment()) return;
            game->toggleMainMenu();
            return;
        }
        glfwSetWindowShouldClose(window, GL_TRUE);
    }
    if ((key == GLFW_KEY_ENTER && action == GLFW_PRESS) &&
        ((glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS) ||
        (glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS))) {
        toggleFullScreen();
        return;
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

    glfwSwapInterval(enableVsync ? 1 : 0);
    const char* errorDescription = nullptr;
    if (glfwGetError(&errorDescription) != GLFW_NO_ERROR) {
        return false;
    }
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
    LocalServerProcess* server,
    std::uint16_t port,
    const std::atomic<bool>* cancelled = nullptr)
{
    const auto deadline = std::chrono::steady_clock::now() +
        std::chrono::seconds(10);
    std::string lastConnectionError;
    while (std::chrono::steady_clock::now() < deadline) {
        if (cancelled && cancelled->load()) {
            throw std::runtime_error("Server connection cancelled.");
        }
        auto client = std::make_unique<TcpClient>();
        if (client->connectLocal(port)) {
            return client;
        }
        lastConnectionError = client->lastError();
        if (server && !server->isRunning()) {
            throw std::runtime_error(
                "The local simulation server exited during startup. " +
                server->lastError());
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    throw std::runtime_error(
        "Timed out waiting for the local simulation server: " +
        lastConnectionError);
}

static void mainloop(const std::function<void()>& pollConnection = {}) {
#if defined(__APPLE__)
    glfwPollEvents();
    GLFWwindow* window = glfwGetCurrentContext();
    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED)) {
        glfwRestoreWindow(window);
    }
    glfwFocusWindow(window);
#endif
    glow::debug("Entering mainloop");
    double time = glfwGetTime();

    while (!glfwWindowShouldClose(glfwGetCurrentContext())) {
        double delta = glfwGetTime() - time;
        time += delta;

        if (pollConnection) {
            pollConnection();
        }
        game->update(static_cast<float>(delta));
        game->draw();

        glfwSwapBuffers(glfwGetCurrentContext());
        glfwPollEvents();
    }
}

void toggleFullScreen() {
    ContextProvider::instance()->toggleFullScreen();
    if (!miscSettings(Property<bool>::get("window.vsync", true))) {
        std::cerr << "Could not enable VSync after recreating the window context.\n";
    }

    GLFWwindow* window = glfwGetCurrentContext();
    glfwSetInputMode(window, GLFW_CURSOR,
        game && game->mainMenuVisible() ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);

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
    const std::string scenario = clParser.scenario();
    if (!scenario.empty() && scenario != "shield-showcase" && scenario != "contracts" &&
        scenario != "ai-comparison") {
        std::cerr << "Unknown scenario: " << scenario << ".\n";
        delete initTimer;
        return 1;
    }
    if (scenario == "contracts" &&
        (benchmarkMode != BenchmarkMode::None ||
         clParser.multiplayerClient() || clParser.serverPreview())) {
        std::cerr << "-scenario contracts cannot be combined with benchmarks or remote client options.\n";
        delete initTimer;
        return 1;
    }
    if ((scenario == "shield-showcase" || scenario == "ai-comparison") &&
        (benchmarkMode != BenchmarkMode::None ||
         clParser.localSingleplayer() ||
         clParser.multiplayerClient() ||
         clParser.serverPreview())) {
        std::cerr << "-scenario cannot be combined with benchmark or remote "
            "simulation options.\n";
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

    const bool showMainMenu = benchmarkMode == BenchmarkMode::None &&
        scenario != "shield-showcase" && scenario != "ai-comparison" && !clParser.serverPreview();
    LocalServerProcess localServer;
    std::unique_ptr<TcpClient> preconnectedClient;
    std::atomic<bool> cancelConnection(false);
    std::future<std::unique_ptr<TcpClient>> pendingConnection;
    if (clParser.localSingleplayer() && !showMainMenu) {
        try {
            const std::uint16_t port = availableLocalPort();
            if (!localServer.start(
                    std::filesystem::absolute(argv[0]),
                    port,
                    benchmarkMode == BenchmarkMode::LargeCombat,
                    runId,
                    scenario == "contracts")) {
                throw std::runtime_error(localServer.lastError());
            }
            preconnectedClient = connectToLocalServer(&localServer, port);
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
    const std::string settingsConfig = FileSystem::userConfigDir() + "/settings.ini";
    if (benchmarkMode == BenchmarkMode::None && FileSystem::exists(settingsConfig)) {
        PropertyManager::instance()->load(settingsConfig);
    }
    PropertyManager::instance()->load("data/voxels.ini", "voxels");

    if (!glfwInit()) {
        std::cerr << "Could not init GLFW.\n";
        delete initTimer;
        return 1;
    }

    glfwSetErrorCallback(errorCallback);

    ContextProvider::instance()->setRequiredGLVersion(MajorVersionRequire, MinorVersionRequire);
    if (clParser.fullScreen()) {
        ContextProvider::instance()->initFullScreen(0);
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

    const bool enableVsync = benchmarkMode != BenchmarkMode::None ||
        Property<bool>("window.vsync").get();
    const bool swapIntervalSet = miscSettings(enableVsync);
    if (!swapIntervalSet) {
        std::cerr << "Could not enable VSync.\n";
    }
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
        game = new Game(!showMainMenu && benchmarkMode == BenchmarkMode::None &&
            !clParser.multiplayerClient() &&
            !clParser.localSingleplayer() &&
            clParser.showIntro() && Property<bool>::get("general.showIntro", true));

        if (showMainMenu) {
            game->enableMainMenu();
            try {
                std::uint16_t port = 27801;
                LocalServerProcess* server = nullptr;
                if (!clParser.multiplayerClient()) {
                    port = availableLocalPort();
                    if (!localServer.start(std::filesystem::absolute(argv[0]),
                            port, false, {}, true)) {
                        throw std::runtime_error(localServer.lastError());
                    }
                    server = &localServer;
                }
                pendingConnection = std::async(std::launch::async,
                    [server, port, &cancelConnection]() {
                        return connectToLocalServer(server, port, &cancelConnection);
                    });
            } catch (const std::exception& exception) {
                glow::warning("Could not set up server connection: %;", exception.what());
                game->setConnectionError(exception.what());
            }
        } else if (benchmarkMode == BenchmarkMode::LargeCombat ||
            clParser.multiplayerClient()) {
            game->gamePlay().setInitialScenario(99);
        } else if (scenario == "shield-showcase") {
            game->gamePlay().setInitialScenario(
                ShieldShowcaseScenario::ScenarioId);
        } else if (scenario == "ai-comparison") {
            game->gamePlay().setInitialScenario(AiComparisonScenario::ScenarioId);
        }

        if (!showMainMenu && clParser.localSingleplayer()) {
            if (benchmarkMode == BenchmarkMode::None) {
                game->gamePlay().setInitialScenario(BattleScenario::ScenarioId);
            }
            if (!game->gamePlay().running().connectRemotePreview(
                    std::move(preconnectedClient))) {
                std::cerr << "Could not initialize the local simulation client.\n";
                exitCode = 1;
            }
        } else if (!showMainMenu && clParser.serverPreview()) {
            game->gamePlay().running().connectRemotePreview(27800);
        } else if (!showMainMenu && clParser.multiplayerClient()) {
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
        const auto resolution = ContextProvider::instance()->resolution();
        resizeCallback(window, resolution.width(), resolution.height());

        delete loadingScreen;
        delete initTimer;

        if (benchmarkMode == BenchmarkMode::None && exitCode == 0) {
            mainloop([&pendingConnection]() {
                if (!pendingConnection.valid() ||
                    pendingConnection.wait_for(std::chrono::seconds(0)) !=
                        std::future_status::ready) {
                    return;
                }
                try {
                    if (!game->gamePlay().running().connectRemotePreview(
                            pendingConnection.get())) {
                        throw std::runtime_error("Could not initialize the server connection.");
                    }
                } catch (const std::exception& exception) {
                    glow::warning("Could not connect to server: %;", exception.what());
                    game->setConnectionError(exception.what());
                }
            });
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

    cancelConnection.store(true);
    if (pendingConnection.valid()) {
        pendingConnection.wait();
    }
    delete game;
    localServer.stop();
    ContextProvider::instance()->shutdown();
    glfwTerminate();
    return exitCode;
}
