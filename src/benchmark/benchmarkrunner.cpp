#include "benchmarkrunner.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <utility>
#include <vector>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include "benchmark/benchmarkprofiler.h"
#include "display/viewer.h"
#include "gamestate/game.h"
#include "gamestate/gameplay/gameplay.h"
#include "gamestate/gameplay/input/gameplayfreecaminput.h"
#include "gamestate/gameplay/running/gameplayrunning.h"

namespace {
    using Clock = std::chrono::steady_clock;

    constexpr auto StartupTimeout = std::chrono::seconds(60);
    constexpr double FrameBudgetMs = 1000.0 / 60.0;

    std::string sanitizeIdentifier(const std::string& value) {
        std::string result;
        result.reserve(value.size());
        for (const unsigned char character : value) {
            if ((character >= 'a' && character <= 'z') ||
                (character >= 'A' && character <= 'Z') ||
                (character >= '0' && character <= '9') ||
                character == '-' || character == '_') {
                result.push_back(static_cast<char>(character));
            } else {
                result.push_back('_');
            }
        }
        return result.empty() ? "run" : result;
    }

    std::string timestampIdentifier() {
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

    void writeSeries(
        std::ostream& output,
        const char* name,
        const std::vector<double>& samples,
        bool& first)
    {
        output << (first ? "" : ",\n") << "    \"" << name << "\": [";
        first = false;
        for (std::size_t index = 0; index < samples.size(); ++index) {
            output << (index == 0 ? "" : ", ") << samples[index];
        }
        output << ']';
    }

    std::vector<double> metricSeries(
        const std::vector<BenchmarkMetricSample>& samples,
        BenchmarkMetric metric)
    {
        std::vector<double> values;
        values.reserve(samples.size());
        const std::size_t index = static_cast<std::size_t>(metric);
        for (const BenchmarkMetricSample& sample : samples) {
            values.push_back(sample.cpuMs[index]);
        }
        return values;
    }

    int runLargeCombatClientBenchmark(
        Game& game,
        const std::string& runId,
        const std::string& clientId)
    {
        GamePlay& gamePlay = game.gamePlay();
        GamePlayRunning& running = gamePlay.running();
        if (!running.remotePreviewActive()) {
            std::cerr << "Large-combat client benchmark requires a server connection.\n";
            return 1;
        }

        gamePlay.setFreecamActive(true);
        gamePlay.freecamInput().setPosition(glm::vec3(0.0f, 0.0f, 1800.0f));

        std::optional<std::uint64_t> startTick;
        std::uint64_t endTick = 0;
        const auto startupDeadline = Clock::now() + StartupTimeout;
        const auto benchmarkDeadline = startupDeadline + std::chrono::minutes(4);
        auto previous = Clock::now();
        std::vector<double> frameTimesMs;
        std::uint64_t missedFrameDeadlines = 0;
        BenchmarkProfiler& profiler = BenchmarkProfiler::instance();
        profiler.beginRun(BenchmarkSimulationIntervals * 2);

        while (true) {
            GLFWwindow* window = glfwGetCurrentContext();
            if (glfwWindowShouldClose(window)) {
                profiler.endRun();
                std::cerr << "Large-combat benchmark cancelled.\n";
                return 2;
            }

            const auto frameStart = Clock::now();
            const bool measuring = startTick.has_value();
            if (measuring) {
                profiler.beginFrame(frameTimesMs.size());
            }
            const float delta = std::chrono::duration<float>(
                frameStart - previous).count();
            previous = frameStart;
            {
                BenchmarkCpuScope scope(BenchmarkMetric::GameUpdate);
                game.update(delta);
            }
            if (!running.remotePreviewActive()) {
                profiler.endRun();
                std::cerr << "Server disconnected before the large-combat benchmark completed.\n";
                return 1;
            }
            {
                BenchmarkCpuScope cpuScope(BenchmarkMetric::GameDraw);
                BenchmarkGpuScope gpuScope(BenchmarkMetric::GameDraw);
                game.draw();
            }
            {
                BenchmarkCpuScope scope(BenchmarkMetric::BufferSwapWait);
                glfwSwapBuffers(window);
            }
            {
                BenchmarkCpuScope scope(BenchmarkMetric::EventPolling);
                glfwPollEvents();
            }

            const auto lobby = running.remoteLobbyStatus();
            const bool worldReady = lobby && lobby->worldStarted &&
                running.remotePreviewSnapshot().has_value();
            if (!startTick && worldReady) {
                startTick = running.remotePreviewTick();
            } else if (measuring) {
                const double frameMs = std::chrono::duration<double, std::milli>(
                    Clock::now() - frameStart).count();
                const double phaseMs =
                    profiler.samples()[frameTimesMs.size()].cpuMs[
                        static_cast<std::size_t>(BenchmarkMetric::GameUpdate)] +
                    profiler.samples()[frameTimesMs.size()].cpuMs[
                        static_cast<std::size_t>(BenchmarkMetric::GameDraw)] +
                    profiler.samples()[frameTimesMs.size()].cpuMs[
                        static_cast<std::size_t>(BenchmarkMetric::BufferSwapWait)] +
                    profiler.samples()[frameTimesMs.size()].cpuMs[
                        static_cast<std::size_t>(BenchmarkMetric::EventPolling)];
                profiler.addCpuTime(
                    BenchmarkMetric::OtherFrameWork,
                    std::max(0.0, frameMs - phaseMs));
                profiler.endFrame();
                frameTimesMs.push_back(frameMs);
                if (frameMs > FrameBudgetMs) {
                    ++missedFrameDeadlines;
                }
                endTick = running.remotePreviewTick();
                if (endTick >= BenchmarkSimulationIntervals) {
                    break;
                }
            }

            if (!startTick && Clock::now() >= startupDeadline) {
                profiler.endRun();
                std::cerr << "Timed out waiting for the server to start the benchmark world.\n";
                return 1;
            }
            if (startTick && Clock::now() >= benchmarkDeadline) {
                profiler.endRun();
                std::cerr << "Timed out waiting for the server to complete the large-combat benchmark.\n";
                return 1;
            }
        }

        profiler.endRun();
        if (!startTick || frameTimesMs.empty()) {
            std::cerr << "Large-combat client benchmark completed without measured frames.\n";
            return 1;
        }

        const auto& allSamples = profiler.samples();
        const std::vector<BenchmarkMetricSample> samples(
            allSamples.begin(),
            allSamples.begin() + static_cast<std::ptrdiff_t>(frameTimesMs.size()));
        const double totalFrameMs = std::accumulate(
            frameTimesMs.begin(), frameTimesMs.end(), 0.0);
        const double averageFrameMs = totalFrameMs /
            static_cast<double>(frameTimesMs.size());
        const std::filesystem::path outputDirectory("benchmark-results");
        const std::filesystem::path outputPath = outputDirectory /
            ("large-combat-" + sanitizeIdentifier(runId) + "-" +
                sanitizeIdentifier(clientId) + ".json");
        std::error_code directoryError;
        std::filesystem::create_directories(outputDirectory, directoryError);
        if (directoryError) {
            std::cerr << "Could not create benchmark result directory: "
                << directoryError.message() << '\n';
            return 1;
        }
        std::ofstream result(outputPath);
        if (!result) {
            std::cerr << "Could not open benchmark result file: "
                << outputPath.string() << '\n';
            return 1;
        }

        result << std::fixed << std::setprecision(4)
            << "{\n"
            << "  \"run_id\": \"" << sanitizeIdentifier(runId) << "\",\n"
            << "  \"role\": \"client\",\n"
            << "  \"client_id\": \"" << sanitizeIdentifier(clientId) << "\",\n"
            << "  \"scenario\": \"large-combat\",\n"
            << "  \"target_simulation_intervals\": "
            << BenchmarkSimulationIntervals << ",\n"
            << "  \"measured_simulation_intervals\": "
            << (endTick - *startTick) << ",\n"
            << "  \"start_tick\": " << *startTick << ",\n"
            << "  \"end_tick\": " << endTick << ",\n"
            << "  \"measured_frames\": " << frameTimesMs.size() << ",\n"
            << "  \"average_frame_ms\": " << averageFrameMs << ",\n"
            << "  \"average_fps\": " << 1000.0 / averageFrameMs << ",\n"
            << "  \"p50_frame_ms\": " << percentile(frameTimesMs, 0.50) << ",\n"
            << "  \"p95_frame_ms\": " << percentile(frameTimesMs, 0.95) << ",\n"
            << "  \"p99_frame_ms\": " << percentile(frameTimesMs, 0.99) << ",\n"
            << "  \"missed_frame_deadlines\": " << missedFrameDeadlines << ",\n"
            << "  \"timing_profile\": ";
        writeBenchmarkProfile(result, samples);
        result << ",\n  \"time_series_ms\": {\n";
        bool firstSeries = true;
        writeSeries(result, "frame_total", frameTimesMs, firstSeries);
        writeSeries(result, "game_update", metricSeries(samples, BenchmarkMetric::GameUpdate), firstSeries);
        writeSeries(result, "game_draw_cpu", metricSeries(samples, BenchmarkMetric::GameDraw), firstSeries);
        writeSeries(result, "network_poll", metricSeries(samples, BenchmarkMetric::ClientNetworkPoll), firstSeries);
        writeSeries(result, "snapshot_sampling", metricSeries(samples, BenchmarkMetric::ClientSnapshotSampling), firstSeries);
        writeSeries(result, "input_prediction", metricSeries(samples, BenchmarkMetric::ClientInputPrediction), firstSeries);
        writeSeries(result, "replica_update", metricSeries(samples, BenchmarkMetric::ClientReplicaUpdate), firstSeries);
        writeSeries(result, "buffer_swap_wait", metricSeries(samples, BenchmarkMetric::BufferSwapWait), firstSeries);
        writeSeries(result, "event_polling", metricSeries(samples, BenchmarkMetric::EventPolling), firstSeries);
        result << "\n  }\n}\n";
        result.close();
        if (!result) {
            std::cerr << "Failed while writing benchmark results to "
                << outputPath.string() << '\n';
            return 1;
        }

        std::cout << "Large-combat client benchmark complete: "
            << clientId << " measured " << frameTimesMs.size()
            << " frames over " << (endTick - *startTick)
            << " simulation intervals.\n"
            << "Results: " << outputPath.string() << '\n';
        return 0;
    }
}

BenchmarkRunner::BenchmarkRunner(
    BenchmarkMode mode,
    std::string runId,
    std::string clientId
) :
    m_mode(mode),
    m_runId(std::move(runId)),
    m_clientId(std::move(clientId))
{
}

int BenchmarkRunner::run(Game& game) const {
    if (m_mode != BenchmarkMode::LargeCombat) {
        std::cerr << "Cannot run benchmark: invalid benchmark mode.\n";
        return 1;
    }
    return runLargeCombatClientBenchmark(
        game,
        m_runId.empty() ? timestampIdentifier() : m_runId,
        m_clientId.empty() ? "client" : m_clientId);
}
