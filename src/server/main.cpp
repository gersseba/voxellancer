#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "benchmark/benchmarkmode.h"
#include "property/propertydirectory.h"
#include "property/propertymanager.h"
#include "scenarios/battlescenario.h"
#include "scenarios/basescenario.h"
#include "scenarios/performancebenchmarkscenario.h"
#include "server/tcpserver.h"
#include "simulation/simulationsession.h"
#include "utils/simulationtickrunner.h"
#include "world/world.h"
#include "worldobject/ship.h"

namespace {
    using Clock = std::chrono::steady_clock;

    struct ServerOptions {
        std::uint64_t tickCount = 60;
        int shipsPerTeam = 200;
        bool listen = false;
        std::uint16_t port = 27800;
        std::string scenario = "benchmark";
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
        for (int i = 1; i < argc; ++i) {
            const std::string argument(argv[i]);
            if (argument == "--help") {
                std::cout << "Usage: voxellancer-server [--ticks count] "
                    "[--ships-per-team count] [--scenario benchmark|battle] "
                    "[--listen] [--listen-port port]\n";
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
            if (argument == "--ticks") {
                options.tickCount = parseUnsigned(value, "--ticks");
            } else if (argument == "--listen-port") {
                const std::uint64_t parsed = parseUnsigned(value, "--listen-port");
                if (parsed == 0 || parsed > 65535) {
                    throw std::invalid_argument("--listen-port must be between 1 and 65535.");
                }
                options.port = static_cast<std::uint16_t>(parsed);
                options.listen = true;
            } else if (argument == "--ships-per-team") {
                const std::uint64_t parsed = parseUnsigned(value, "--ships-per-team");
                if (parsed > 400) {
                    throw std::invalid_argument("--ships-per-team must be between 1 and 400.");
                }
                options.shipsPerTeam = static_cast<int>(parsed);
            } else if (argument == "--scenario") {
                if (value != "benchmark" && value != "battle") {
                    throw std::invalid_argument(
                        "--scenario must be benchmark or battle.");
                }
                options.scenario = value;
            } else {
                throw std::invalid_argument("Unknown option: " + argument);
            }
        }

        if (options.tickCount == 0 || options.tickCount > 10000000) {
            throw std::invalid_argument("--ticks must be between 1 and 10000000.");
        }
        if (options.shipsPerTeam == 0) {
            throw std::invalid_argument("--ships-per-team must be between 1 and 400.");
        }
        return options;
    }

    double percentile(const std::vector<double>& sortedSamples, double fraction) {
        if (sortedSamples.empty()) {
            return 0.0;
        }

        const std::size_t index = static_cast<std::size_t>(
            std::ceil(fraction * static_cast<double>(sortedSamples.size()))) - 1;
        return sortedSamples[std::min(index, sortedSamples.size() - 1)];
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
}

int main(int argc, char* argv[]) {
    try {
        const ServerOptions options = parseOptions(argc, argv);
        loadSimulationData();
        std::srand(PerformanceBenchmarkScenario::SimulationRandomSeed);

        World* world = World::instance(World::Mode::Server);
        SimulationSession session(*world);
        std::unique_ptr<BaseScenario> scenario;
        std::optional<voxellancer::protocol::EntityId> preferredActor;
        if (options.scenario == "battle") {
            auto battle = std::make_unique<BattleScenario>(*world);
            battle->load();
            preferredActor = world->entityId(battle->playerShip());
            scenario = std::move(battle);
        } else {
            scenario = std::make_unique<PerformanceBenchmarkScenario>(
                *world,
                BenchmarkMode::LargeCombatVsync,
                options.shipsPerTeam);
            scenario->load();
        }

        if (options.listen) {
            TcpServer server(
                *world,
                session,
                options.port,
                650.0f,
                preferredActor);
            std::cout << "Listening on 127.0.0.1:" << server.port() << '\n';
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

        std::vector<double> tickDurationsMs;
        tickDurationsMs.reserve(static_cast<std::size_t>(options.tickCount));
        std::uint64_t missedDeadlines = 0;
        for (std::uint64_t tick = 0; tick < options.tickCount; ++tick) {
            const auto start = Clock::now();
            const std::uint32_t ticksRun =
                session.advance(SimulationTickRunner::TickDurationSeconds);
            const double durationMs =
                std::chrono::duration<double, std::milli>(Clock::now() - start).count();

            if (ticksRun != 1 || session.tickIndex() != tick + 1) {
                throw std::runtime_error("Simulation session did not advance exactly one tick.");
            }
            if (durationMs > 1000.0 / SimulationTickRunner::TickRate) {
                ++missedDeadlines;
            }
            tickDurationsMs.push_back(durationMs);
        }

        std::sort(tickDurationsMs.begin(), tickDurationsMs.end());
        std::cout << std::fixed << std::setprecision(3)
            << "{\n"
            << "  \"tick_count\": " << session.tickIndex() << ",\n"
            << "  \"entity_count\": " << world->worldObjects().size() << ",\n"
            << "  \"ship_count\": " << world->ships().size() << ",\n"
            << "  \"tick_time_ms\": {\"p50\": " << percentile(tickDurationsMs, 0.50)
            << ", \"p95\": " << percentile(tickDurationsMs, 0.95)
            << ", \"p99\": " << percentile(tickDurationsMs, 0.99)
            << ", \"max\": " << tickDurationsMs.back() << "},\n"
            << "  \"missed_deadlines\": " << missedDeadlines << ",\n"
            << "  \"backlog_ms\": " << session.backlogSeconds() * 1000.0 << "\n"
            << "}\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "voxellancer-server: " << exception.what() << '\n';
        return 1;
    }
}
