#include "benchmarkrunner.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include <GL/glew.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "display/viewer.h"
#include "benchmark/benchmarkprofiler.h"
#include "etc/contextprovider.h"
#include "gamestate/game.h"
#include "gamestate/gameplay/gameplay.h"
#include "gamestate/gameplay/input/gameplayfreecaminput.h"
#include "scenarios/performancebenchmarkscenario.h"
#include "voxeleffect/voxelparticleengine.h"
#include "world/world.h"
#include "world/worldlogic.h"
#include "worldobject/ship.h"
#include "worldobject/worldobject.h"

namespace {
    using Clock = std::chrono::steady_clock;

    constexpr std::size_t WarmupFrames = 120;
    constexpr std::size_t StandardMeasuredFrames = 600;
    constexpr std::size_t LargeCombatMeasuredFrames = 1800;
    constexpr double LargeCombatTargetFps = 55.0;
    constexpr double MinimumMetricChangeMs = 0.25;
    constexpr double MinimumMetricChangePercent = 10.0;

    struct FrameProfile {
        double gameUpdateMs = 0.0;
        double drawCpuMs = 0.0;
        double swapWaitMs = 0.0;
        double eventPollMs = 0.0;
        double otherFrameMs = 0.0;
        double gpuDrawMs = 0.0;
        bool gpuDrawAvailable = false;
        std::size_t voxelCount = 0;
        std::size_t worldObjectCount = 0;
        std::size_t shipCount = 0;
        std::size_t bulletCount = 0;
        std::size_t rocketCount = 0;
        std::size_t voxelParticleCount = 0;
        std::size_t voxelParticleBufferCapacity = 0;
        WorldUpdateTimings worldUpdate;
        WorldLogicTimings worldLogic;
        BenchmarkMetricSample taskTimings;
    };

    struct MetricInfo {
        BenchmarkMetric metric;
        const char* id;
        const char* label;
        const char* parent;
        const char* device;
        bool gpu;
    };

    constexpr std::array<MetricInfo, BenchmarkMetricCount> MetricInfos{{
        { BenchmarkMetric::GameUpdate, "game_update", "Game update", nullptr, "CPU", false },
        { BenchmarkMetric::WorldUpdate, "world_update", "World update", "game_update", "CPU", false },
        { BenchmarkMetric::PlayerUpdate, "player_update", "Player update", "world_update", "CPU", false },
        { BenchmarkMetric::WorldLogic, "world_logic", "World logic", "world_update", "CPU", false },
        { BenchmarkMetric::MovementCollisionDetection, "movement_collision_detection",
            "Movement and voxel collision detection", "world_logic", "CPU", false },
        { BenchmarkMetric::CollisionProcessingDamage, "collision_processing_damage",
            "Collision processing and damage", "world_logic", "CPU", false },
        { BenchmarkMetric::SpawnSplitCleanup, "spawn_split_cleanup",
            "Spawn, split, and cleanup", "world_logic", "CPU", false },
        { BenchmarkMetric::BulletSpawnCollision, "bullet_spawn_collision",
            "Bullet spawn collision checks", "spawn_split_cleanup", "CPU", false },
        { BenchmarkMetric::ScriptUpdate, "script_update", "Script update", "world_update", "CPU", false },
        { BenchmarkMetric::EventPollerUpdate, "event_poller_update",
            "World event-poller update", "world_update", "CPU", false },
        { BenchmarkMetric::ParticleEngineUpdate, "particle_engine_update",
            "Particle-engine update", "world_update", "CPU", false },
        { BenchmarkMetric::MissionSystemUpdate, "mission_system_update",
            "Mission-system update", "world_update", "CPU", false },
        { BenchmarkMetric::WorldObjectUpdate, "world_object_update",
            "World-object updates and AI", "world_update", "CPU", false },
        { BenchmarkMetric::ShootingTargetSelection, "shooting_target_selection",
            "AI target scan and firing", "world_object_update", "CPU", false },
        { BenchmarkMetric::BulletLineOfFireQuery, "bullet_line_of_fire_query",
            "Bullet line-of-fire queries", "shooting_target_selection", "CPU", false },
        { BenchmarkMetric::ProjectileMovement, "projectile_movement",
            "Projectile movement and collision", "movement_collision_detection", "CPU", false },
        { BenchmarkMetric::GameDraw, "game_draw", "Game draw", nullptr, "CPU + GPU", true },
        { BenchmarkMetric::SceneContentDraw, "scene_content_draw",
            "Scene content draw", "game_draw", "CPU + GPU", true },
        { BenchmarkMetric::SkyboxDraw, "skybox_draw", "Skybox draw", "scene_content_draw", "CPU + GPU", true },
        { BenchmarkMetric::WorldObjectDraw, "world_object_draw",
            "World-object voxel draw", "scene_content_draw", "CPU + GPU", true },
        { BenchmarkMetric::HudDraw, "hud_draw", "HUD and FPS overlay", "scene_content_draw", "CPU + GPU", true },
        { BenchmarkMetric::ParticleDraw, "particle_draw",
            "Voxel-particle draw", "scene_content_draw", "CPU + GPU", true },
        { BenchmarkMetric::WorldTreeDraw, "world_tree_draw",
            "World-tree debug draw", "scene_content_draw", "CPU + GPU", true },
        { BenchmarkMetric::StarfieldDraw, "starfield_draw", "Starfield draw", "render_pipeline", "CPU + GPU", true },
        { BenchmarkMetric::RenderPipeline, "render_pipeline",
            "Render pipeline", "game_draw", "CPU + GPU", true },
        { BenchmarkMetric::OutputBlit, "output_blit", "Output blit", "game_draw", "CPU + GPU", true },
        { BenchmarkMetric::BufferSwapWait, "buffer_swap_wait",
            "Buffer swap and VSync wait", nullptr, "CPU wait", false },
        { BenchmarkMetric::EventPolling, "event_polling", "Window event polling", nullptr, "CPU", false },
        { BenchmarkMetric::OtherFrameWork, "other_frame_work",
            "Other measured frame work", nullptr, "CPU", false }
    }};

    struct ProfileBucket {
        std::size_t frames = 0;
        std::size_t gpuTimingFrames = 0;
        double frameTimeMs = 0.0;
        std::array<double, BenchmarkMetricCount> cpuMs{};
        std::array<double, BenchmarkMetricCount> gpuMs{};
    };

    struct ProfileWindow {
        std::size_t startSecond = 0;
        std::size_t seconds = 0;
        std::size_t frames = 0;
        std::size_t gpuTimingFrames = 0;
        double frameTimeMs = 0.0;
        double averageFps = 0.0;
        std::array<double, BenchmarkMetricCount> cpuMs{};
        std::array<double, BenchmarkMetricCount> gpuMs{};
    };

    struct TimingSummary {
        double averageMs = 0.0;
        double p95Ms = 0.0;
        double maxMs = 0.0;
        double totalMs = 0.0;
    };

    double percentile(const std::vector<double>& sortedFrames, double fraction);

    TimingSummary summarizeTimings(std::vector<double> samples) {
        if (samples.empty()) {
            return {};
        }

        std::sort(samples.begin(), samples.end());
        TimingSummary summary;
        summary.totalMs = std::accumulate(samples.begin(), samples.end(), 0.0);
        summary.averageMs = summary.totalMs / static_cast<double>(samples.size());
        summary.p95Ms = percentile(samples, 0.95);
        summary.maxMs = samples.back();
        return summary;
    }

    void writeTimingSummary(
        std::ostream& output,
        const char* name,
        const std::vector<double>& samples,
        double denominatorMs,
        bool includeShare
    ) {
        const TimingSummary summary = summarizeTimings(samples);
        output << "      \"" << name << "\": {"
            << "\"avg_ms\": " << summary.averageMs
            << ", \"p95_ms\": " << summary.p95Ms
            << ", \"max_ms\": " << summary.maxMs
            << ", \"total_ms\": " << summary.totalMs;
        if (includeShare) {
            output << ", \"share_percent\": "
                << (denominatorMs > 0.0 ? 100.0 * summary.totalMs / denominatorMs : 0.0);
        }
        output << '}';
    }

    template <typename Sample>
    void writeTimeSeries(
        std::ostream& output,
        const char* name,
        const std::vector<Sample>& samples,
        bool isLast
    ) {
        output << "    \"" << name << "\": [";
        for (std::size_t i = 0; i < samples.size(); ++i) {
            output << (i == 0 ? "" : ", ") << samples[i];
        }
        output << ']' << (isLast ? "\n" : ",\n");
    }

    void writeGpuDrawTimeSeries(
        std::ostream& output,
        const std::vector<FrameProfile>& profiles
    ) {
        output << "    \"gpu_draw_ms\": [";
        for (std::size_t i = 0; i < profiles.size(); ++i) {
            if (i != 0) {
                output << ", ";
            }
            if (profiles[i].gpuDrawAvailable) {
                output << profiles[i].gpuDrawMs;
            } else {
                output << "null";
            }
        }
        output << "]\n";
    }

    std::vector<ProfileBucket> buildProfileBuckets(
        const std::vector<double>& frameTimesMs,
        const std::vector<FrameProfile>& profiles
    ) {
        std::vector<ProfileBucket> buckets;
        double elapsedMs = 0.0;
        for (std::size_t frame = 0; frame < frameTimesMs.size(); ++frame) {
            const std::size_t second = static_cast<std::size_t>(elapsedMs / 1000.0);
            if (buckets.size() <= second) {
                buckets.resize(second + 1);
            }

            ProfileBucket& bucket = buckets[second];
            ++bucket.frames;
            bucket.frameTimeMs += frameTimesMs[frame];
            const BenchmarkMetricSample& sample = profiles[frame].taskTimings;
            if (sample.gpuTimingAvailable) {
                ++bucket.gpuTimingFrames;
            }
            for (std::size_t metric = 0; metric < BenchmarkMetricCount; ++metric) {
                bucket.cpuMs[metric] += sample.cpuMs[metric];
                if (sample.gpuTimingAvailable) {
                    bucket.gpuMs[metric] += sample.gpuMs[metric];
                }
            }
            elapsedMs += frameTimesMs[frame];
        }
        return buckets;
    }

    ProfileWindow aggregateWindow(
        const std::vector<ProfileBucket>& buckets,
        std::size_t startSecond,
        std::size_t seconds
    ) {
        ProfileWindow window;
        window.startSecond = startSecond;
        window.seconds = seconds;
        for (std::size_t second = startSecond; second < startSecond + seconds; ++second) {
            const ProfileBucket& bucket = buckets[second];
            window.frames += bucket.frames;
            window.gpuTimingFrames += bucket.gpuTimingFrames;
            window.frameTimeMs += bucket.frameTimeMs;
            for (std::size_t metric = 0; metric < BenchmarkMetricCount; ++metric) {
                window.cpuMs[metric] += bucket.cpuMs[metric];
                window.gpuMs[metric] += bucket.gpuMs[metric];
            }
        }
        if (window.frameTimeMs > 0.0) {
            window.averageFps = 1000.0 * static_cast<double>(window.frames) /
                window.frameTimeMs;
        }
        return window;
    }

    void writeNullableNumber(std::ostream& output, bool available, double value) {
        if (available) {
            output << value;
        } else {
            output << "null";
        }
    }

    bool isSignificantChange(double baseline, double comparison) {
        const double absoluteChange = std::abs(comparison - baseline);
        if (absoluteChange < MinimumMetricChangeMs) {
            return false;
        }
        return baseline == 0.0 ||
            100.0 * absoluteChange / std::abs(baseline) >= MinimumMetricChangePercent;
    }

    void writeProfileWindow(
        std::ostream& output,
        const ProfileWindow& window,
        const char* indent
    ) {
        output << indent << "{\n"
            << indent << "  \"first_second\": " << window.startSecond << ",\n"
            << indent << "  \"seconds\": " << window.seconds << ",\n"
            << indent << "  \"frames\": " << window.frames << ",\n"
            << indent << "  \"gpu_timing_frames\": " << window.gpuTimingFrames << ",\n"
            << indent << "  \"average_fps\": " << window.averageFps << ",\n"
            << indent << "  \"average_frame_ms\": "
            << (window.frames > 0 ? window.frameTimeMs / window.frames : 0.0) << '\n'
            << indent << '}';
    }

    void writeTaskProfile(
        std::ostream& output,
        const std::vector<ProfileBucket>& buckets,
        std::size_t completeSeconds
    ) {
        output << "    \"task_profile\": {\n"
            << "      \"method\": \"Task timings are averaged per measured frame within each elapsed one-second bucket. Parent timings include nested children; do not sum them.\",\n"
            << "      \"hierarchy\": [\n";
        for (std::size_t metric = 0; metric < BenchmarkMetricCount; ++metric) {
            const MetricInfo& info = MetricInfos[metric];
            output << "        {\"id\": \"" << info.id << "\", \"label\": \"" << info.label
                << "\", \"parent\": ";
            if (info.parent) {
                output << '"' << info.parent << '"';
            } else {
                output << "null";
            }
            output << ", \"runs_on\": \"" << info.device << "\"}"
                << (metric + 1 == BenchmarkMetricCount ? "\n" : ",\n");
        }
        output << "      ],\n"
            << "      \"per_second\": [\n";
        for (std::size_t second = 0; second < buckets.size(); ++second) {
            const ProfileBucket& bucket = buckets[second];
            output << "        {\"second\": " << second
                << ", \"complete\": " << (second < completeSeconds ? "true" : "false")
                << ", \"frames\": " << bucket.frames
                << ", \"gpu_timing_frames\": " << bucket.gpuTimingFrames
                << ", \"average_fps\": ";
            writeNullableNumber(output, bucket.frames > 0 && bucket.frameTimeMs > 0.0,
                bucket.frameTimeMs > 0.0
                    ? 1000.0 * static_cast<double>(bucket.frames) / bucket.frameTimeMs
                    : 0.0);
            output << ", \"average_frame_ms\": ";
            writeNullableNumber(output, bucket.frames > 0,
                bucket.frames > 0
                    ? bucket.frameTimeMs / bucket.frames
                    : 0.0);
            output << ", \"tasks\": {\n";
            for (std::size_t metric = 0; metric < BenchmarkMetricCount; ++metric) {
                const MetricInfo& info = MetricInfos[metric];
                output << "          \"" << info.id << "\": {\"cpu_avg_ms_per_frame\": "
                    << (bucket.frames > 0 ? bucket.cpuMs[metric] / bucket.frames : 0.0)
                    << ", \"gpu_avg_ms_per_frame\": ";
                writeNullableNumber(output, info.gpu && bucket.gpuTimingFrames > 0,
                    bucket.gpuTimingFrames > 0
                        ? bucket.gpuMs[metric] / bucket.gpuTimingFrames
                        : 0.0);
                output << '}' << (metric + 1 == BenchmarkMetricCount ? "\n" : ",\n");
            }
            output << "        }}" << (second + 1 == buckets.size() ? "\n" : ",\n");
        }
        output << "      ],\n"
            << "      \"three_second_comparison\": {";
        if (completeSeconds < 3) {
            output << "\"available\": false}\n";
        } else {
            const ProfileWindow baseline = aggregateWindow(buckets, 0, 3);
            ProfileWindow worst = baseline;
            for (std::size_t start = 1; start + 3 <= completeSeconds; ++start) {
                const ProfileWindow candidate = aggregateWindow(buckets, start, 3);
                if (candidate.frames > 0 &&
                    (worst.frames == 0 || candidate.averageFps < worst.averageFps)) {
                    worst = candidate;
                }
            }
            std::array<bool, BenchmarkMetricCount> visibleMetrics{};
            std::size_t visibleMetricCount = 0;
            for (std::size_t metric = 0; metric < BenchmarkMetricCount; ++metric) {
                const MetricInfo& info = MetricInfos[metric];
                const double baselineCpu =
                    baseline.cpuMs[metric] / static_cast<double>(baseline.frames);
                const double worstCpu =
                    worst.cpuMs[metric] / static_cast<double>(worst.frames);
                const bool gpuAvailable = info.gpu &&
                    baseline.gpuTimingFrames > 0 && worst.gpuTimingFrames > 0;
                const double baselineGpu = gpuAvailable
                    ? baseline.gpuMs[metric] / static_cast<double>(baseline.gpuTimingFrames)
                    : 0.0;
                const double worstGpu = gpuAvailable
                    ? worst.gpuMs[metric] / static_cast<double>(worst.gpuTimingFrames)
                    : 0.0;
                visibleMetrics[metric] = isSignificantChange(baselineCpu, worstCpu) ||
                    (gpuAvailable && isSignificantChange(baselineGpu, worstGpu));
                if (visibleMetrics[metric]) {
                    ++visibleMetricCount;
                }
            }
            const double fpsDelta = worst.averageFps - baseline.averageFps;
            const double fpsChangePercent = baseline.averageFps != 0.0
                ? 100.0 * fpsDelta / baseline.averageFps
                : 0.0;
            output << "\n        \"available\": true,\n"
                << "        \"baseline\": ";
            writeProfileWindow(output, baseline, "        ");
            output << ",\n        \"worst_fps\": ";
            writeProfileWindow(output, worst, "        ");
            output << ",\n"
                << "        \"fps_delta\": " << fpsDelta << ",\n"
                << "        \"fps_change_percent\": " << fpsChangePercent << ",\n"
                << "        \"metric_change_filter\": {"
                << "\"minimum_absolute_delta_ms_per_frame\": " << MinimumMetricChangeMs
                << ", \"minimum_percent_delta\": " << MinimumMetricChangePercent
                << ", \"zero_baseline_requires_absolute_delta\": true},\n"
                << "        \"metric_comparison\": {";
            bool firstVisibleMetric = true;
            for (std::size_t metric = 0; metric < BenchmarkMetricCount; ++metric) {
                if (!visibleMetrics[metric]) {
                    continue;
                }
                const MetricInfo& info = MetricInfos[metric];
                const double baselineCpu = baseline.frames > 0
                    ? baseline.cpuMs[metric] / baseline.frames : 0.0;
                const double worstCpu = worst.frames > 0
                    ? worst.cpuMs[metric] / worst.frames : 0.0;
                const bool gpuAvailable = info.gpu &&
                    baseline.gpuTimingFrames > 0 && worst.gpuTimingFrames > 0;
                const double baselineGpu = baseline.gpuTimingFrames > 0
                    ? baseline.gpuMs[metric] / baseline.gpuTimingFrames : 0.0;
                const double worstGpu = worst.gpuTimingFrames > 0
                    ? worst.gpuMs[metric] / worst.gpuTimingFrames : 0.0;
                output << (firstVisibleMetric ? "\n" : ",\n")
                    << "          \"" << info.id << "\": {"
                    << "\"cpu_baseline_avg_ms_per_frame\": " << baselineCpu
                    << ", \"cpu_worst_avg_ms_per_frame\": " << worstCpu
                    << ", \"cpu_delta_ms_per_frame\": " << worstCpu - baselineCpu
                    << ", \"cpu_change_percent\": ";
                writeNullableNumber(output, baselineCpu != 0.0,
                    baselineCpu != 0.0 ? 100.0 * (worstCpu - baselineCpu) / baselineCpu : 0.0);
                output << ", \"gpu_baseline_avg_ms_per_frame\": ";
                writeNullableNumber(output, gpuAvailable, baselineGpu);
                output << ", \"gpu_worst_avg_ms_per_frame\": ";
                writeNullableNumber(output, gpuAvailable, worstGpu);
                output << ", \"gpu_delta_ms_per_frame\": ";
                writeNullableNumber(output, gpuAvailable, worstGpu - baselineGpu);
                output << ", \"gpu_change_percent\": ";
                writeNullableNumber(output, gpuAvailable && baselineGpu != 0.0,
                    baselineGpu != 0.0 ? 100.0 * (worstGpu - baselineGpu) / baselineGpu : 0.0);
                output << '}';
                firstVisibleMetric = false;
            }
            output << "\n        },\n"
                << "        \"omitted_metric_count\": "
                << BenchmarkMetricCount - visibleMetricCount << "\n"
                << "      }\n";
        }
        output << "    }\n";
    }

    bool isCombatMode(BenchmarkMode mode) {
        return mode == BenchmarkMode::Combat || mode == BenchmarkMode::LargeCombatVsync;
    }

    bool isLargeCombatMode(BenchmarkMode mode) {
        return mode == BenchmarkMode::LargeCombatVsync;
    }

    const char* modeName(BenchmarkMode mode) {
        switch (mode) {
        case BenchmarkMode::Rendering:
            return "rendering";
        case BenchmarkMode::Combat:
            return "combat";
        case BenchmarkMode::LargeCombatVsync:
            return "large-combat-vsync";
        default:
            return "invalid";
        }
    }

    double percentile(const std::vector<double>& sortedFrames, double fraction) {
        const std::size_t index = static_cast<std::size_t>(
            std::ceil(fraction * static_cast<double>(sortedFrames.size()))) - 1;
        return sortedFrames[std::min(index, sortedFrames.size() - 1)];
    }
}

BenchmarkRunner::BenchmarkRunner(BenchmarkMode mode) :
    m_mode(mode)
{
}

int BenchmarkRunner::run(Game& game) const {
    if (m_mode != BenchmarkMode::Rendering && !isCombatMode(m_mode)) {
        std::cerr << "Cannot run benchmark: invalid benchmark mode.\n";
        return 1;
    }

    game.update(1.0f / 60.0f);

    GamePlay& gamePlay = game.gamePlay();
    gamePlay.setFreecamActive(true);
    GamePlayFreecamInput& freecam = static_cast<GamePlayFreecamInput&>(gamePlay.inputHandler());
    const glm::vec3 cameraStart = m_mode == BenchmarkMode::Rendering
        ? glm::vec3(16.0f, 12.0f, 232.0f)
        : (isLargeCombatMode(m_mode) ? glm::vec3(0.0f, 0.0f, 1800.0f) : glm::vec3(0.0f, 0.0f, 120.0f));
    const glm::vec3 cameraEnd = m_mode == BenchmarkMode::Rendering
        ? glm::vec3(16.0f, 12.0f, 16.0f)
        : (isLargeCombatMode(m_mode) ? cameraStart : glm::vec3(0.0f, 0.0f, -120.0f));
    const glm::quat cameraOrientation(1.0f, 0.0f, 0.0f, 0.0f);

    const std::size_t measuredFrames = isLargeCombatMode(m_mode)
        ? LargeCombatMeasuredFrames
        : StandardMeasuredFrames;
    std::vector<double> frameTimesMs;
    std::vector<FrameProfile> frameProfiles(measuredFrames);
    frameTimesMs.reserve(measuredFrames);
    BenchmarkProfiler& profiler = BenchmarkProfiler::instance();
    profiler.beginRun(measuredFrames);
    double previousTime = glfwGetTime();
    std::uint64_t collisionPairsBeforeMeasurement = 0;
    const std::size_t initialShipCount = World::instance()->ships().size();
    std::size_t belowTargetFrameCount = 0;
    std::size_t currentBelowTargetStreak = 0;
    std::size_t longestBelowTargetStreak = 0;
    double worstFrameMs = 0.0;

    for (std::size_t frame = 0; frame < WarmupFrames + measuredFrames; ++frame) {
        GLFWwindow* window = glfwGetCurrentContext();
        if (glfwWindowShouldClose(window)) {
            profiler.endRun();
            std::cerr << "Benchmark cancelled before all sample frames completed; no result file was written.\n";
            return 2;
        }

        const double currentTime = glfwGetTime();
        const float deltaSec = static_cast<float>(currentTime - previousTime);
        previousTime = currentTime;

        const auto frameStart = Clock::now();
        FrameProfile* profile = frame >= WarmupFrames
            ? &frameProfiles[frame - WarmupFrames]
            : nullptr;
        if (profile) {
            profiler.beginFrame(frame - WarmupFrames);
        }

        if (frame == WarmupFrames) {
            collisionPairsBeforeMeasurement =
                World::instance()->worldLogic().voxelCollisionPairCount();
        }
        const auto updateStart = profile ? Clock::now() : Clock::time_point{};
        if (isCombatMode(m_mode)) {
            BenchmarkCpuScope scope(BenchmarkMetric::GameUpdate);
            game.update(deltaSec);
        }
        if (profile) {
            profile->gameUpdateMs = std::chrono::duration<double, std::milli>(
                Clock::now() - updateStart).count();
            if (isCombatMode(m_mode)) {
                profile->worldUpdate = World::instance()->lastUpdateTimings();
                profile->worldLogic = World::instance()->worldLogic().lastUpdateTimings();
            }
            for (WorldObject* worldObject : World::instance()->worldObjects()) {
                ++profile->worldObjectCount;
                profile->voxelCount += static_cast<std::size_t>(worldObject->voxelCount());
                switch (worldObject->objectType()) {
                case WorldObjectType::Ship:
                    ++profile->shipCount;
                    break;
                case WorldObjectType::Bullet:
                    ++profile->bulletCount;
                    break;
                case WorldObjectType::Rocket:
                    ++profile->rocketCount;
                    break;
                case WorldObjectType::Other:
                    break;
                }
            }
            profile->voxelParticleCount =
                static_cast<std::size_t>(World::instance()->particleEngine().particleCount());
            profile->voxelParticleBufferCapacity =
                static_cast<std::size_t>(World::instance()->particleEngine().particleDataCount());
        }

        float progress = 0.0f;
        if (frame >= WarmupFrames) {
            progress = static_cast<float>(frame - WarmupFrames) /
                static_cast<float>(measuredFrames - 1);
        }
        freecam.setPosition(glm::mix(cameraStart, cameraEnd, progress));
        freecam.setOrientation(cameraOrientation);

        const auto drawStart = profile ? Clock::now() : Clock::time_point{};
        {
            BenchmarkCpuScope cpuScope(BenchmarkMetric::GameDraw);
            BenchmarkGpuScope gpuScope(BenchmarkMetric::GameDraw);
            game.draw();
        }
        if (profile) {
            profile->drawCpuMs = std::chrono::duration<double, std::milli>(
                Clock::now() - drawStart).count();
        }

        const auto swapStart = profile ? Clock::now() : Clock::time_point{};
        {
            BenchmarkCpuScope scope(BenchmarkMetric::BufferSwapWait);
            glfwSwapBuffers(window);
        }
        if (profile) {
            profile->swapWaitMs = std::chrono::duration<double, std::milli>(
                Clock::now() - swapStart).count();
        }

        const auto eventPollStart = profile ? Clock::now() : Clock::time_point{};
        {
            BenchmarkCpuScope scope(BenchmarkMetric::EventPolling);
            glfwPollEvents();
        }
        const auto frameEnd = Clock::now();
        if (frame >= WarmupFrames) {
            const std::chrono::duration<double, std::milli> elapsed = frameEnd - frameStart;
            const double frameTimeMs = elapsed.count();
            profile->eventPollMs = std::chrono::duration<double, std::milli>(
                frameEnd - eventPollStart).count();
            const double measuredPhasesMs = profile->gameUpdateMs + profile->drawCpuMs +
                profile->swapWaitMs + profile->eventPollMs;
            profile->otherFrameMs = std::max(0.0, frameTimeMs - measuredPhasesMs);
            profiler.addCpuTime(
                BenchmarkMetric::OtherFrameWork,
                profile->otherFrameMs);
            frameTimesMs.push_back(frameTimeMs);
            worstFrameMs = std::max(worstFrameMs, frameTimeMs);
            if (isLargeCombatMode(m_mode) && frameTimeMs > 1000.0 / LargeCombatTargetFps) {
                ++belowTargetFrameCount;
                ++currentBelowTargetStreak;
                longestBelowTargetStreak = std::max(longestBelowTargetStreak, currentBelowTargetStreak);
            } else {
                currentBelowTargetStreak = 0;
            }
            profiler.endFrame();
        }
    }

    profiler.endRun();
    const std::vector<BenchmarkMetricSample>& taskSamples = profiler.samples();
    for (std::size_t frame = 0; frame < frameProfiles.size(); ++frame) {
        FrameProfile& profile = frameProfiles[frame];
        profile.taskTimings = taskSamples[frame];
        const std::size_t gameDrawMetric =
            static_cast<std::size_t>(BenchmarkMetric::GameDraw);
        profile.gpuDrawAvailable = profile.taskTimings.gpuValid[gameDrawMetric];
        if (profile.gpuDrawAvailable) {
            profile.gpuDrawMs = profile.taskTimings.gpuMs[gameDrawMetric];
        }
    }

    const double averageFrameMs =
        std::accumulate(frameTimesMs.begin(), frameTimesMs.end(), 0.0) /
        static_cast<double>(frameTimesMs.size());
    std::vector<double> sortedFrameTimesMs(frameTimesMs);
    std::sort(sortedFrameTimesMs.begin(), sortedFrameTimesMs.end());

    const std::size_t middleFrameIndex = sortedFrameTimesMs.size() / 2;
    const double medianFrameMs = sortedFrameTimesMs.size() % 2 == 0
        ? (sortedFrameTimesMs[middleFrameIndex - 1] + sortedFrameTimesMs[middleFrameIndex]) / 2.0
        : sortedFrameTimesMs[middleFrameIndex];
    const double p95FrameMs = percentile(sortedFrameTimesMs, 0.95);
    const double p99FrameMs = percentile(sortedFrameTimesMs, 0.99);
    const double averageFps = 1000.0 / averageFrameMs;
    const std::size_t slowestFrameCount = std::max<std::size_t>(
        1, static_cast<std::size_t>(std::ceil(sortedFrameTimesMs.size() * 0.01)));
    const double slowestFramesAverageMs = std::accumulate(
        sortedFrameTimesMs.end() - slowestFrameCount,
        sortedFrameTimesMs.end(), 0.0) / static_cast<double>(slowestFrameCount);
    const double onePercentLowFps = 1000.0 / slowestFramesAverageMs;

    std::size_t voxelCount = 0;
    for (WorldObject* worldObject : World::instance()->worldObjects()) {
        voxelCount += static_cast<std::size_t>(worldObject->voxelCount());
    }

    const std::uint64_t collisionPairCount =
        World::instance()->worldLogic().voxelCollisionPairCount() -
        collisionPairsBeforeMeasurement;
    std::vector<double> gameUpdateSamples;
    std::vector<double> drawCpuSamples;
    std::vector<double> swapWaitSamples;
    std::vector<double> eventPollSamples;
    std::vector<double> otherFrameSamples;
    std::vector<double> gpuDrawSamples;
    std::vector<double> playerUpdateSamples;
    std::vector<double> worldLogicSamples;
    std::vector<double> scriptsSamples;
    std::vector<double> eventPollerSamples;
    std::vector<double> particleEngineSamples;
    std::vector<double> missionSystemSamples;
    std::vector<double> worldObjectsSamples;
    std::vector<double> worldLogicMovementSamples;
    std::vector<double> worldLogicCollisionDamageSamples;
    std::vector<double> worldLogicLifecycleSamples;
    std::vector<double> frameFpsSamples;
    std::vector<std::size_t> frameVoxelCounts;
    std::vector<std::size_t> frameWorldObjectCounts;
    std::vector<std::size_t> frameShipCounts;
    std::vector<std::size_t> frameBulletCounts;
    std::vector<std::size_t> frameRocketCounts;
    std::vector<std::size_t> frameVoxelParticleCounts;
    std::vector<std::size_t> frameVoxelParticleBufferCapacities;
    std::array<std::vector<std::uint64_t>, BenchmarkCounterCount> counterSamples;
    std::vector<ProfileBucket> profileBuckets =
        buildProfileBuckets(frameTimesMs, frameProfiles);
    const double measuredElapsedMs =
        std::accumulate(frameTimesMs.begin(), frameTimesMs.end(), 0.0);
    const std::size_t completeProfileSeconds =
        static_cast<std::size_t>(measuredElapsedMs / 1000.0);
    for (const FrameProfile& profile : frameProfiles) {
        gameUpdateSamples.push_back(profile.gameUpdateMs);
        drawCpuSamples.push_back(profile.drawCpuMs);
        swapWaitSamples.push_back(profile.swapWaitMs);
        eventPollSamples.push_back(profile.eventPollMs);
        otherFrameSamples.push_back(profile.otherFrameMs);
        if (profile.gpuDrawAvailable) {
            gpuDrawSamples.push_back(profile.gpuDrawMs);
        }
        playerUpdateSamples.push_back(profile.worldUpdate.playerMs);
        worldLogicSamples.push_back(profile.worldUpdate.worldLogicMs);
        scriptsSamples.push_back(profile.worldUpdate.scriptsMs);
        eventPollerSamples.push_back(profile.worldUpdate.eventPollerMs);
        particleEngineSamples.push_back(profile.worldUpdate.particleEngineMs);
        missionSystemSamples.push_back(profile.worldUpdate.missionSystemMs);
        worldObjectsSamples.push_back(profile.worldUpdate.worldObjectsMs);
        worldLogicMovementSamples.push_back(profile.worldLogic.movementMs);
        worldLogicCollisionDamageSamples.push_back(profile.worldLogic.collisionDamageMs);
        worldLogicLifecycleSamples.push_back(profile.worldLogic.lifecycleMs);
        frameVoxelCounts.push_back(profile.voxelCount);
        frameWorldObjectCounts.push_back(profile.worldObjectCount);
        frameShipCounts.push_back(profile.shipCount);
        frameBulletCounts.push_back(profile.bulletCount);
        frameRocketCounts.push_back(profile.rocketCount);
        frameVoxelParticleCounts.push_back(profile.voxelParticleCount);
        frameVoxelParticleBufferCapacities.push_back(profile.voxelParticleBufferCapacity);
        for (std::size_t counter = 0; counter < BenchmarkCounterCount; ++counter) {
            counterSamples[counter].push_back(profile.taskTimings.counters[counter]);
        }
    }
    for (double frameTimeMs : frameTimesMs) {
        frameFpsSamples.push_back(1000.0 / frameTimeMs);
    }
    const double measuredFrameTotalMs =
        std::accumulate(frameTimesMs.begin(), frameTimesMs.end(), 0.0);
    const double worldUpdateTotalMs =
        summarizeTimings(playerUpdateSamples).totalMs +
        summarizeTimings(worldLogicSamples).totalMs +
        summarizeTimings(scriptsSamples).totalMs +
        summarizeTimings(eventPollerSamples).totalMs +
        summarizeTimings(particleEngineSamples).totalMs +
        summarizeTimings(missionSystemSamples).totalMs +
        summarizeTimings(worldObjectsSamples).totalMs;
    const double worldLogicTotalMs =
        summarizeTimings(worldLogicMovementSamples).totalMs +
        summarizeTimings(worldLogicCollisionDamageSamples).totalMs +
        summarizeTimings(worldLogicLifecycleSamples).totalMs;
    const Size<int> resolution = ContextProvider::instance()->resolution();
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::filesystem::path outputDirectory("benchmark-results");
    const std::filesystem::path outputPath = outputDirectory /
        (std::string(modeName(m_mode)) + "-" + std::to_string(timestamp) + ".json");

    std::error_code directoryError;
    std::filesystem::create_directories(outputDirectory, directoryError);
    if (directoryError) {
        std::cerr << "Could not create benchmark result directory: "
                  << directoryError.message() << '\n';
        return 1;
    }

    std::ofstream result(outputPath);
    if (!result) {
        std::cerr << "Could not open benchmark result file: " << outputPath.string() << '\n';
        return 1;
    }

    result << std::fixed << std::setprecision(4)
        << "{\n"
        << "  \"mode\": \"" << modeName(m_mode) << "\",\n"
        << "  \"random_seed\": " << PerformanceBenchmarkScenario::SimulationRandomSeed << ",\n"
        << "  \"warmup_frames\": " << WarmupFrames << ",\n"
        << "  \"measured_frames\": " << measuredFrames << ",\n"
        << "  \"resolution\": \"" << resolution.width() << "x" << resolution.height() << "\",\n"
        << "  \"world_objects\": " << World::instance()->worldObjects().size() << ",\n"
        << "  \"initial_ships\": " << initialShipCount << ",\n"
        << "  \"final_ships\": " << World::instance()->ships().size() << ",\n"
        << "  \"voxels\": " << voxelCount << ",\n"
        << "  \"voxel_collision_pairs\": " << collisionPairCount << ",\n"
        << "  \"average_fps\": " << averageFps << ",\n"
        << "  \"average_frame_ms\": " << averageFrameMs << ",\n"
        << "  \"median_frame_ms\": " << medianFrameMs << ",\n"
        << "  \"p95_frame_ms\": " << p95FrameMs << ",\n"
        << "  \"p99_frame_ms\": " << p99FrameMs << ",\n"
        << "  \"one_percent_low_fps\": " << onePercentLowFps << ",\n"
        << "  \"vsync_enabled\": " << (isLargeCombatMode(m_mode) ? "true" : "false") << ",\n";
    if (isLargeCombatMode(m_mode)) {
        const double belowTargetPercent =
            100.0 * static_cast<double>(belowTargetFrameCount) / static_cast<double>(measuredFrames);
        result
            << "  \"target_fps\": " << LargeCombatTargetFps << ",\n"
            << "  \"below_target_frames\": " << belowTargetFrameCount << ",\n"
            << "  \"below_target_percent\": " << belowTargetPercent << ",\n"
            << "  \"longest_below_target_streak\": " << longestBelowTargetStreak << ",\n"
            << "  \"placement_seed\": " << PerformanceBenchmarkScenario::LargeCombatPlacementSeed << ",\n"
            << "  \"worst_fps\": " << 1000.0 / worstFrameMs << ",\n";
    }
    result << "  \"timing_profile\": {\n"
        << "    \"notes\": \"Frame phases are wall-clock CPU timings; GPU draw time is asynchronous and may overlap CPU work. World update and world logic entries are nested breakdowns.\",\n"
        << "    \"frame_phases\": {\n";
    writeTimingSummary(result, "game_update", gameUpdateSamples, measuredFrameTotalMs, true);
    result << ",\n";
    writeTimingSummary(result, "draw_cpu", drawCpuSamples, measuredFrameTotalMs, true);
    result << ",\n";
    writeTimingSummary(result, "buffer_swap_wait", swapWaitSamples, measuredFrameTotalMs, true);
    result << ",\n";
    writeTimingSummary(result, "event_poll", eventPollSamples, measuredFrameTotalMs, true);
    result << ",\n";
    writeTimingSummary(result, "other_frame_work", otherFrameSamples, measuredFrameTotalMs, true);
    result << "\n    },\n"
        << "    \"gpu_draw\": {\"available\": " << (!gpuDrawSamples.empty() ? "true" : "false")
        << ", \"samples\": " << gpuDrawSamples.size();
    if (!gpuDrawSamples.empty()) {
        const TimingSummary gpuDrawSummary = summarizeTimings(gpuDrawSamples);
        result << ", \"avg_ms\": " << gpuDrawSummary.averageMs
            << ", \"p95_ms\": " << gpuDrawSummary.p95Ms
            << ", \"max_ms\": " << gpuDrawSummary.maxMs
            << ", \"total_ms\": " << gpuDrawSummary.totalMs;
    }
    result << "},\n"
        << "    \"world_update_ms\": {\n";
    writeTimingSummary(result, "player", playerUpdateSamples, worldUpdateTotalMs, true);
    result << ",\n";
    writeTimingSummary(result, "world_logic", worldLogicSamples, worldUpdateTotalMs, true);
    result << ",\n";
    writeTimingSummary(result, "scripts", scriptsSamples, worldUpdateTotalMs, true);
    result << ",\n";
    writeTimingSummary(result, "event_poller", eventPollerSamples, worldUpdateTotalMs, true);
    result << ",\n";
    writeTimingSummary(result, "particle_engine", particleEngineSamples, worldUpdateTotalMs, true);
    result << ",\n";
    writeTimingSummary(result, "mission_system", missionSystemSamples, worldUpdateTotalMs, true);
    result << ",\n";
    writeTimingSummary(result, "world_objects_and_ai", worldObjectsSamples, worldUpdateTotalMs, true);
    result << "\n    },\n"
        << "    \"world_logic_ms\": {\n";
    writeTimingSummary(result, "movement_and_voxel_collision_detection",
        worldLogicMovementSamples, worldLogicTotalMs, true);
    result << ",\n";
    writeTimingSummary(result, "collision_processing_and_damage",
        worldLogicCollisionDamageSamples, worldLogicTotalMs, true);
    result << ",\n";
    writeTimingSummary(result, "spawn_split_and_cleanup",
        worldLogicLifecycleSamples, worldLogicTotalMs, true);
    result << "\n    },\n";
    writeTaskProfile(result, profileBuckets, completeProfileSeconds);
    result << "  },\n"
        << "  \"frame_times_ms\": [";

    for (std::size_t i = 0; i < frameTimesMs.size(); ++i) {
        result << (i == 0 ? "" : ", ") << frameTimesMs[i];
    }
    result << "],\n"
        << "  \"time_series\": {\n";
    writeTimeSeries(result, "fps", frameFpsSamples, false);
    writeTimeSeries(result, "world_voxels", frameVoxelCounts, false);
    writeTimeSeries(result, "world_objects", frameWorldObjectCounts, false);
    writeTimeSeries(result, "ships", frameShipCounts, false);
    writeTimeSeries(result, "bullets", frameBulletCounts, false);
    writeTimeSeries(result, "rockets", frameRocketCounts, false);
    writeTimeSeries(result, "voxel_particles", frameVoxelParticleCounts, false);
    writeTimeSeries(result, "voxel_particle_buffer_capacity",
        frameVoxelParticleBufferCapacities, false);
    writeTimeSeries(result, "game_update_ms", gameUpdateSamples, false);
    writeTimeSeries(result, "world_logic_ms", worldLogicSamples, false);
    writeTimeSeries(result, "movement_and_voxel_collision_ms",
        worldLogicMovementSamples, false);
    writeTimeSeries(result, "collision_processing_and_damage_ms",
        worldLogicCollisionDamageSamples, false);
    writeTimeSeries(result, "spawn_split_cleanup_ms",
        worldLogicLifecycleSamples, false);
    writeTimeSeries(result, "world_objects_and_ai_ms", worldObjectsSamples, false);
    writeTimeSeries(result, "primary_target_candidates_considered",
        counterSamples[static_cast<std::size_t>(
            BenchmarkCounter::PrimaryTargetCandidatesConsidered)], false);
    writeTimeSeries(result, "primary_target_switches",
        counterSamples[static_cast<std::size_t>(
            BenchmarkCounter::PrimaryTargetSwitches)], false);
    writeTimeSeries(result, "gun_target_candidates_considered",
        counterSamples[static_cast<std::size_t>(
            BenchmarkCounter::GunTargetCandidatesConsidered)], false);
    writeTimeSeries(result, "gun_target_refreshes",
        counterSamples[static_cast<std::size_t>(
            BenchmarkCounter::GunTargetRefreshes)], false);
    writeTimeSeries(result, "gun_targets_assigned",
        counterSamples[static_cast<std::size_t>(
            BenchmarkCounter::GunTargetsAssigned)], false);
    writeTimeSeries(result, "weapon_aim_checks",
        counterSamples[static_cast<std::size_t>(
            BenchmarkCounter::WeaponAimChecks)], false);
    writeTimeSeries(result, "bullet_line_of_fire_queries",
        counterSamples[static_cast<std::size_t>(
            BenchmarkCounter::BulletLineOfFireQueries)], false);
    writeTimeSeries(result, "bullets_scheduled",
        counterSamples[static_cast<std::size_t>(
            BenchmarkCounter::BulletsScheduled)], false);
    writeGpuDrawTimeSeries(result, frameProfiles);
    result << "  }\n}\n";
    result.close();
    if (!result) {
        std::cerr << "Failed while writing benchmark result file: " << outputPath.string() << '\n';
        return 1;
    }

    std::cout << std::fixed << std::setprecision(2)
        << "Benchmark complete: " << modeName(m_mode) << '\n'
        << "Frames: " << measuredFrames << " (after " << WarmupFrames << " warm-up frames)\n"
        << "Average FPS: " << averageFps << '\n'
        << "Median / p95 / p99 frame time: " << medianFrameMs << " / "
        << p95FrameMs << " / " << p99FrameMs << " ms\n"
        << "1% low FPS: " << onePercentLowFps << '\n'
        << "World objects / initial ships / final ships / voxels: " << World::instance()->worldObjects().size()
        << " / " << initialShipCount << " / " << World::instance()->ships().size() << " / " << voxelCount << '\n'
        << "Voxel collision pairs: " << collisionPairCount << '\n';
    std::cout << "CPU frame phase avg ms (update / draw / swap / events / other): "
        << summarizeTimings(gameUpdateSamples).averageMs << " / "
        << summarizeTimings(drawCpuSamples).averageMs << " / "
        << summarizeTimings(swapWaitSamples).averageMs << " / "
        << summarizeTimings(eventPollSamples).averageMs << " / "
        << summarizeTimings(otherFrameSamples).averageMs << '\n'
        << "World update avg ms (world logic / world objects and AI): "
        << summarizeTimings(worldLogicSamples).averageMs << " / "
        << summarizeTimings(worldObjectsSamples).averageMs << '\n'
        << "World logic avg ms (movement and collision detection / collision damage / lifecycle): "
        << summarizeTimings(worldLogicMovementSamples).averageMs << " / "
        << summarizeTimings(worldLogicCollisionDamageSamples).averageMs << " / "
        << summarizeTimings(worldLogicLifecycleSamples).averageMs << '\n';
    if (!gpuDrawSamples.empty()) {
        std::cout << "GPU draw time avg / p95 ms: "
            << summarizeTimings(gpuDrawSamples).averageMs << " / "
            << summarizeTimings(gpuDrawSamples).p95Ms << " ("
            << gpuDrawSamples.size() << " samples)\n";
    } else {
        std::cout << "GPU draw timing unavailable (timer-query extension not supported)\n";
    }
    if (isLargeCombatMode(m_mode)) {
        std::cout << "VSync: enabled; target: " << LargeCombatTargetFps << " FPS\n"
            << "Frames below target: " << belowTargetFrameCount << " / " << measuredFrames
            << " (" << 100.0 * static_cast<double>(belowTargetFrameCount) /
                static_cast<double>(measuredFrames) << "%)\n"
            << "Longest below-target streak: " << longestBelowTargetStreak << " frames\n"
            << "Worst FPS: " << 1000.0 / worstFrameMs << '\n';
    }
    std::cout << "Results: " << outputPath.string() << std::endl;

    return 0;
}
