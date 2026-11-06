#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <glm/glm.hpp>

#include "benchmark/benchmarkmode.h"
#include "benchmark/benchmarkprofiler.h"
#include "property/propertydirectory.h"
#include "property/propertymanager.h"
#include "resource/worldobjectbuilder.h"
#include "scenarios/basescenario.h"
#include "scenarios/battlescenario.h"
#include "scenarios/largecombatscenario.h"
#include "server/tcpserver.h"
#include "simulation/simulationsession.h"
#include "utils/simulationtickrunner.h"
#include "world/god.h"
#include "world/world.h"
#include "worldobject/ship.h"
#include "worldobject/worldobjectinfo.h"

namespace {
    using Clock = std::chrono::steady_clock;

    struct ServerOptions {
        int shipsPerTeam = BenchmarkShipsPerTeam;
        bool shipsPerTeamSpecified = false;
        int squadSize = 0;
        bool squadSizeSpecified = false;
        bool listen = false;
        std::size_t targetPlayers = 0;
        std::size_t benchmarkTicks = 0;
        std::uint16_t port = 27800;
        std::string scenario = "battle";
        std::string runId;
        std::string clientId;
    };

    std::uint64_t parseUnsigned(const std::string& value, const char* option) {
        std::size_t parsedLength = 0;
        const unsigned long long parsed = std::stoull(value, &parsedLength);
        if (parsedLength != value.size()) {
            throw std::invalid_argument(std::string("Invalid value for ") + option + ".");
        }
        return static_cast<std::uint64_t>(parsed);
    }

    ServerOptions parseOptions(int argc, char* argv[]) {
        ServerOptions options;
        bool scenarioSpecified = false;
        for (int i = 1; i < argc; ++i) {
            const std::string argument(argv[i]);
            if (argument == "--help") {
                std::cout << "Usage: voxellancer-server [--scenario battle|benchmark] "
                    << "[--listen] [--listen-port port] [--target-players count] "
                    << "[--ships-per-team count] [--benchmark-ticks count] "
                    << "[--squad-size count] "
                    << "[--benchmark-run-id id]\n";
                std::exit(0);
            }
            if (argument == "--listen") {
                options.listen = true;
                continue;
            }
            if (i + 1 >= argc) {
                throw std::invalid_argument("Missing value for " + argument + ".");
            }

            const std::string value(argv[++i]);
            if (argument == "--listen-port") {
                const std::uint64_t parsed = parseUnsigned(value, "--listen-port");
                if (parsed == 0 || parsed > 65535) {
                    throw std::invalid_argument(
                        "--listen-port must be between 1 and 65535.");
                }
                options.port = static_cast<std::uint16_t>(parsed);
                options.listen = true;
            } else if (argument == "--ships-per-team") {
                const std::uint64_t parsed = parseUnsigned(value, "--ships-per-team");
                if (parsed == 0 || parsed > BenchmarkShipsPerTeam) {
                    throw std::invalid_argument(
                        "--ships-per-team must be between 1 and 400.");
                }
                options.shipsPerTeam = static_cast<int>(parsed);
                options.shipsPerTeamSpecified = true;
            } else if (argument == "--squad-size") {
                const std::uint64_t parsed = parseUnsigned(value, "--squad-size");
                if (parsed == 0 || parsed > BenchmarkShipsPerTeam) {
                    throw std::invalid_argument(
                        "--squad-size must be between 1 and 400.");
                }
                options.squadSize = static_cast<int>(parsed);
                options.squadSizeSpecified = true;
            } else if (argument == "--scenario") {
                if (value != "benchmark" && value != "battle") {
                    throw std::invalid_argument(
                        "--scenario must be benchmark or battle.");
                }
                options.scenario = value;
                scenarioSpecified = true;
            } else if (argument == "--target-players") {
                const std::uint64_t parsed =
                    parseUnsigned(value, "--target-players");
                if (parsed < 1 || parsed > 16) {
                    throw std::invalid_argument(
                        "--target-players must be between 1 and 16.");
                }
                options.targetPlayers = static_cast<std::size_t>(parsed);
                options.listen = true;
            } else if (argument == "--benchmark-ticks") {
                const std::uint64_t parsed =
                    parseUnsigned(value, "--benchmark-ticks");
                if (parsed == 0 || parsed > 10000000) {
                    throw std::invalid_argument(
                        "--benchmark-ticks must be between 1 and 10000000.");
                }
                options.benchmarkTicks = static_cast<std::size_t>(parsed);
            } else if (argument == "--benchmark-run-id") {
                options.runId = value;
            } else if (argument == "--benchmark-client-id") {
                options.clientId = value;
            } else {
                throw std::invalid_argument("Unknown option: " + argument);
            }
        }

        if (options.targetPlayers != 0 && !scenarioSpecified) {
            options.scenario = "battle";
        }
        if (options.scenario == "benchmark" &&
            options.targetPlayers != 0 &&
            !options.shipsPerTeamSpecified) {
            options.shipsPerTeam = BenchmarkShipsPerTeam;
        }
        if (options.benchmarkTicks != 0 &&
            (options.scenario != "benchmark" || options.targetPlayers == 0)) {
            throw std::invalid_argument(
                "--benchmark-ticks requires --scenario benchmark and --target-players.");
        }
        if (options.squadSizeSpecified && options.scenario != "benchmark") {
            throw std::invalid_argument(
                "--squad-size requires --scenario benchmark.");
        }
        if (options.squadSize > options.shipsPerTeam) {
            throw std::invalid_argument(
                "--squad-size cannot exceed --ships-per-team.");
        }
        if (!options.listen) {
            throw std::invalid_argument(
                "The server requires --listen, --listen-port, or --target-players.");
        }
        return options;
    }

    std::string safeRunId(std::string value) {
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
        return value.empty() ? std::string("run") : value;
    }

    std::string newRunId() {
        return std::to_string(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
    }

    double percentile(std::vector<double> samples, double fraction) {
        if (samples.empty()) {
            return 0.0;
        }
        std::sort(samples.begin(), samples.end());
        const std::size_t index = static_cast<std::size_t>(
            std::ceil(fraction * static_cast<double>(samples.size()))) - 1;
        return samples[std::min(index, samples.size() - 1)];
    }

    void writeSummary(
        std::ostream& output,
        const std::vector<double>& samples)
    {
        const double total = std::accumulate(samples.begin(), samples.end(), 0.0);
        output << "{\"samples\":" << samples.size()
            << ",\"avg_ms\":"
            << (samples.empty() ? 0.0 : total / static_cast<double>(samples.size()))
            << ",\"p50_ms\":" << percentile(samples, 0.50)
            << ",\"p95_ms\":" << percentile(samples, 0.95)
            << ",\"p99_ms\":" << percentile(samples, 0.99)
            << ",\"max_ms\":"
            << (samples.empty() ? 0.0
                : *std::max_element(samples.begin(), samples.end()))
            << '}';
    }

    void loadSimulationData() {
        PropertyManager::instance()->load("data/config.ini");
        PropertyManager::instance()->load("data/voxels.ini", "voxels");
        PropertyDirectory("data/worldobjects").read();
        PropertyDirectory("data/equipment/engines").read();
        PropertyDirectory("data/equipment/shields").read();
        PropertyDirectory("data/equipment/weapons").read();
        PropertyDirectory("data/equipment/projectiles").read();
    }

    std::vector<voxellancer::protocol::EntityId> addPlayerShips(
        World& world,
        Ship* firstPlayer,
        std::size_t playerCount,
        float playerY)
    {
        std::vector<Ship*> ships{firstPlayer};
        for (std::size_t index = 1; index < playerCount; ++index) {
            Ship* ship = WorldObjectBuilder("mox", world).buildShip();
            ship->transform().setPosition(
                glm::vec3(static_cast<float>(index) * 80.0f, playerY, 10.0f));
            ship->info().setName("player" + std::to_string(index + 1));
            world.god().scheduleSpawn(ship);
            ships.push_back(ship);
        }
        world.god().spawn();

        std::vector<voxellancer::protocol::EntityId> actors;
        actors.reserve(ships.size());
        for (Ship* ship : ships) {
            const auto actor = world.entityId(ship);
            if (!actor) {
                throw std::runtime_error(
                    "Could not create an entity ID for a multiplayer player.");
            }
            ship->transform().setPosition(glm::vec3(
                static_cast<float>(actors.size()) * 80.0f, playerY, 10.0f));
            actors.push_back(*actor);
        }
        return actors;
    }

    void runLargeCombatBenchmark(
        TcpServer& server,
        SimulationSession& session,
        World& world,
        std::size_t tickCount,
        const std::string& runId,
        int squadSize,
        const std::string& firstClientId)
    {
        server.poll();
        if (server.clientCount() == 0 || !server.worldStarted()) {
            throw std::runtime_error(
                "Large-combat benchmark requires all target clients before measurement.");
        }

        const std::vector<TcpServer::ClientStatistics> clientsAtStart =
            server.clientStatistics();
        BenchmarkProfiler& profiler = BenchmarkProfiler::instance();
        profiler.beginRun(tickCount);
        std::vector<double> simulationMs;
        std::vector<double> networkPollMs;
        std::vector<double> loopMs;
        simulationMs.reserve(tickCount);
        networkPollMs.reserve(tickCount);
        loopMs.reserve(tickCount);
        std::uint64_t missedDeadlines = 0;
        const auto tickDuration = std::chrono::duration_cast<Clock::duration>(
            std::chrono::duration<double>(1.0 / SimulationTickRunner::TickRate));
        const double tickBudgetMs =
            1000.0 / static_cast<double>(SimulationTickRunner::TickRate);

        for (std::size_t tick = 0; tick < tickCount; ++tick) {
            const auto tickStart = Clock::now();
            profiler.beginFrame(tick);
            const auto simulationStart = Clock::now();
            const std::uint32_t ticksRun =
                session.advance(SimulationTickRunner::TickDurationSeconds);
            const double simulationDuration = std::chrono::duration<double, std::milli>(
                Clock::now() - simulationStart).count();
            profiler.addCpuTime(BenchmarkMetric::ServerSimulation, simulationDuration);
            if (ticksRun != 1) {
                profiler.endFrame();
                profiler.endRun();
                throw std::runtime_error(
                    "Large-combat server did not advance exactly one simulation interval.");
            }

            const auto pollStart = Clock::now();
            server.poll();
            const double pollDuration = std::chrono::duration<double, std::milli>(
                Clock::now() - pollStart).count();
            const double loopDuration = std::chrono::duration<double, std::milli>(
                Clock::now() - tickStart).count();
            profiler.endFrame();
            simulationMs.push_back(simulationDuration);
            networkPollMs.push_back(pollDuration);
            loopMs.push_back(loopDuration);
            if (loopDuration > tickBudgetMs) {
                ++missedDeadlines;
            }
            if (server.clientCount() != clientsAtStart.size()) {
                profiler.endRun();
                throw std::runtime_error(
                    "A client disconnected during the large-combat benchmark.");
            }
            std::this_thread::sleep_until(tickStart + tickDuration);
        }
        profiler.endRun();

        const std::vector<TcpServer::ClientStatistics> clientsAtEnd =
            server.clientStatistics();
        if (clientsAtEnd.size() != clientsAtStart.size()) {
            throw std::runtime_error(
                "A client disconnected before the large-combat report was written.");
        }
        const auto clientDrainDeadline = Clock::now() + std::chrono::seconds(2);
        while (server.clientCount() == clientsAtEnd.size() &&
            Clock::now() < clientDrainDeadline) {
            server.poll();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        const std::filesystem::path outputDirectory("benchmark-results");
        const std::filesystem::path outputPath = outputDirectory /
            ("large-combat-" + safeRunId(runId) + "-server.json");
        std::error_code directoryError;
        std::filesystem::create_directories(outputDirectory, directoryError);
        if (directoryError) {
            throw std::runtime_error(
                "Could not create benchmark result directory: " +
                directoryError.message());
        }
        std::ofstream result(outputPath);
        if (!result) {
            throw std::runtime_error(
                "Could not open benchmark result file: " + outputPath.string());
        }

        result << std::fixed << std::setprecision(4)
            << "{\n"
            << "  \"run_id\": \"" << safeRunId(runId) << "\",\n"
            << "  \"role\": \"server\",\n"
            << "  \"scenario\": \"large-combat\",\n"
            << "  \"target_players\": " << clientsAtStart.size() << ",\n"
            << "  \"target_simulation_intervals\": " << tickCount << ",\n"
            << "  \"measured_simulation_intervals\": " << session.tickIndex() << ",\n"
            << "  \"world_entities\": " << world.worldObjects().size() << ",\n"
            << "  \"ships\": " << world.ships().size() << ",\n"
            << "  \"squad_size\": " << squadSize << ",\n"
            << "  \"missed_simulation_deadlines\": " << missedDeadlines << ",\n"
            << "  \"simulation_tick_ms\": ";
        writeSummary(result, simulationMs);
        result << ",\n  \"network_poll_ms\": ";
        writeSummary(result, networkPollMs);
        result << ",\n  \"server_loop_ms\": ";
        writeSummary(result, loopMs);
        result << ",\n  \"timing_profile\": ";
        writeBenchmarkProfile(result, profiler.samples());
        result << ",\n  \"clients\": [";
        for (std::size_t index = 0; index < clientsAtEnd.size(); ++index) {
            const auto& start = clientsAtStart[index];
            const auto& end = clientsAtEnd[index];
            const std::string clientId = index == 0 && !firstClientId.empty()
                ? safeRunId(firstClientId)
                : "client-" + std::to_string(index + 1);
            result << (index == 0 ? "" : ",")
                << "{\"client_id\":\"" << clientId
                << "\",\"actor_index\":" << end.actor.index
                << ",\"actor_generation\":" << end.actor.generation
                << ",\"visible_entities\":" << end.visibleEntityCount
                << ",\"bytes_sent_at_start\":" << start.bytesSent
                << ",\"bytes_sent_at_end\":" << end.bytesSent
                << ",\"bytes_sent_during_measurement\":"
                << (end.bytesSent - start.bytesSent) << '}';
        }
        result << "],\n  \"time_series_ms\": {\n"
            << "    \"simulation_tick\": [";
        for (std::size_t i = 0; i < simulationMs.size(); ++i) {
            result << (i == 0 ? "" : ", ") << simulationMs[i];
        }
        result << "],\n    \"network_poll\": [";
        for (std::size_t i = 0; i < networkPollMs.size(); ++i) {
            result << (i == 0 ? "" : ", ") << networkPollMs[i];
        }
        result << "],\n    \"server_loop\": [";
        for (std::size_t i = 0; i < loopMs.size(); ++i) {
            result << (i == 0 ? "" : ", ") << loopMs[i];
        }
        result << "]\n  }\n}\n";
        result.close();
        if (!result) {
            throw std::runtime_error(
                "Could not finish writing benchmark result file: " +
                outputPath.string());
        }

        std::cout << "Large-combat server benchmark complete: "
            << tickCount << " simulation intervals, "
            << missedDeadlines << " missed deadlines.\n"
            << "Results: " << outputPath.string() << '\n';
    }

    void runServer(TcpServer& server, SimulationSession& session) {
        auto previous = Clock::now();
        auto nextNetworkStatistics = previous + std::chrono::seconds(1);
        while (true) {
            const auto now = Clock::now();
            const double elapsed =
                std::chrono::duration<double>(now - previous).count();
            previous = now;
            session.advance(elapsed);
            server.poll();
            if (now >= nextNetworkStatistics) {
                std::cout << "{\"network_clients\":[";
                const auto statistics = server.clientStatistics();
                for (std::size_t index = 0; index < statistics.size(); ++index) {
                    if (index != 0) {
                        std::cout << ',';
                    }
                    const auto& client = statistics[index];
                    std::cout << "{\"actor_index\":" << client.actor.index
                        << ",\"actor_generation\":" << client.actor.generation
                        << ",\"cumulative_bytes_sent\":" << client.bytesSent
                        << ",\"visible_entities\":" << client.visibleEntityCount
                        << '}';
                }
                std::cout << "]}" << std::endl;
                nextNetworkStatistics = now + std::chrono::seconds(1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
}

int main(int argc, char* argv[]) {
    try {
        const ServerOptions options = parseOptions(argc, argv);
        const std::string runId = safeRunId(
            options.runId.empty() ? newRunId() : options.runId);
        loadSimulationData();
        std::srand(LargeCombatScenario::SimulationRandomSeed);

        World* world = World::instance(World::Mode::Server);
        SimulationSession session(*world);
        std::unique_ptr<BaseScenario> scenario;
        std::optional<voxellancer::protocol::EntityId> preferredActor;
        std::unique_ptr<TcpServer> server;

        if (options.targetPlayers != 0) {
            server = std::make_unique<TcpServer>(
                *world,
                session,
                options.port,
                options.scenario == "benchmark" ? 1500.0f : 650.0f,
                std::nullopt,
                options.targetPlayers);
            std::cout << "Listening on 127.0.0.1:" << server->port() << '\n';
            const auto lobbyDeadline = Clock::now() + std::chrono::seconds(90);
            while (server->clientCount() < options.targetPlayers) {
                server->poll();
                if (options.benchmarkTicks != 0 && Clock::now() >= lobbyDeadline) {
                    throw std::runtime_error(
                        "Timed out waiting for all large-combat benchmark clients.");
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        }

        if (options.scenario == "battle") {
            auto battle = std::make_unique<BattleScenario>(*world);
            battle->load();
            preferredActor = world->entityId(battle->playerShip());
            if (!preferredActor) {
                throw std::runtime_error(
                    "Could not create the battle's player actor.");
            }
            scenario = std::move(battle);
            if (options.targetPlayers != 0) {
                const auto actors = addPlayerShips(
                    *world,
                    static_cast<BattleScenario*>(scenario.get())->playerShip(),
                    options.targetPlayers,
                    0.0f);
                server->startWorldForPlayers(actors);
            }
        } else {
            scenario = std::make_unique<LargeCombatScenario>(
                *world, options.shipsPerTeam, options.squadSize);
            scenario->load();
            if (options.targetPlayers != 0) {
                Ship* firstPlayer = WorldObjectBuilder("mox", *world).buildShip();
                firstPlayer->info().setName("player1");
                world->god().scheduleSpawn(firstPlayer);
                const auto actors = addPlayerShips(
                    *world,
                    firstPlayer,
                    options.targetPlayers,
                    700.0f);
                server->startWorldForPlayers(actors);
            }
        }

        if (server == nullptr) {
            server = std::make_unique<TcpServer>(
                *world,
                session,
                options.port,
                options.scenario == "benchmark" ? 1500.0f : 650.0f,
                preferredActor);
            std::cout << "Listening on 127.0.0.1:" << server->port() << '\n';
        }

        if (options.benchmarkTicks != 0) {
            runLargeCombatBenchmark(
                *server,
                session,
                *world,
                options.benchmarkTicks,
                runId,
                options.squadSize,
                options.clientId);
            return 0;
        }
        runServer(*server, session);
    } catch (const std::exception& exception) {
        std::cerr << "voxellancer-server: " << exception.what() << '\n';
        return 1;
    }
}
