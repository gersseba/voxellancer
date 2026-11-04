#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <vector>

enum class BenchmarkMetric : std::size_t {
    GameUpdate,
    WorldUpdate,
    PlayerUpdate,
    WorldLogic,
    MovementCollisionDetection,
    CollisionProcessingDamage,
    SpawnSplitCleanup,
    BulletSpawnCollision,
    ScriptUpdate,
    EventPollerUpdate,
    ParticleEngineUpdate,
    MissionSystemUpdate,
    WorldObjectUpdate,
    ShootingTargetSelection,
    BulletLineOfFireQuery,
    ProjectileMovement,
    GameDraw,
    SceneContentDraw,
    SkyboxDraw,
    WorldObjectDraw,
    HudDraw,
    ParticleDraw,
    WorldTreeDraw,
    StarfieldDraw,
    RenderPipeline,
    OutputBlit,
    BufferSwapWait,
    EventPolling,
    OtherFrameWork,
    Count
};

constexpr std::size_t BenchmarkMetricCount =
    static_cast<std::size_t>(BenchmarkMetric::Count);

struct BenchmarkMetricSample {
    std::array<double, BenchmarkMetricCount> cpuMs{};
    std::array<double, BenchmarkMetricCount> gpuMs{};
    std::array<bool, BenchmarkMetricCount> gpuValid{};
    bool gpuTimingAvailable = false;
};

class BenchmarkProfiler {
public:
    static BenchmarkProfiler& instance();

    void beginRun(std::size_t frameCount);
    void beginFrame(std::size_t frameIndex);
    void endFrame();
    void endRun();

    bool recording() const;
    void addCpuTime(BenchmarkMetric metric, double elapsedMs);
    void beginGpuTime(BenchmarkMetric metric);
    void endGpuTime(BenchmarkMetric metric);

    const std::vector<BenchmarkMetricSample>& samples() const;

private:
    static constexpr std::size_t QueryFramesInFlight = 8;

    BenchmarkProfiler();
    std::size_t queryIndex(std::size_t slot, std::size_t metric, bool end) const;
    void resolveSlot(std::size_t slot);

    std::vector<BenchmarkMetricSample> m_samples;
    std::vector<unsigned int> m_gpuQueries;
    std::array<std::array<bool, BenchmarkMetricCount>, QueryFramesInFlight> m_started{};
    std::array<std::array<bool, BenchmarkMetricCount>, QueryFramesInFlight> m_completed{};
    std::array<std::array<bool, BenchmarkMetricCount>, QueryFramesInFlight> m_pendingMetrics{};
    std::array<std::size_t, QueryFramesInFlight> m_pendingFrameIndices{};
    std::array<bool, QueryFramesInFlight> m_pendingSlots{};
    std::size_t m_currentFrameIndex;
    std::size_t m_currentSlot;
    bool m_active;
    bool m_frameOpen;
    bool m_gpuSlotUsable;
    bool m_gpuAvailable;
};

class BenchmarkCpuScope {
public:
    explicit BenchmarkCpuScope(BenchmarkMetric metric);
    ~BenchmarkCpuScope();

    BenchmarkCpuScope(const BenchmarkCpuScope&) = delete;
    BenchmarkCpuScope& operator=(const BenchmarkCpuScope&) = delete;

private:
    BenchmarkMetric m_metric;
    bool m_enabled;
    std::chrono::steady_clock::time_point m_start;
};

class BenchmarkGpuScope {
public:
    explicit BenchmarkGpuScope(BenchmarkMetric metric);
    ~BenchmarkGpuScope();

    BenchmarkGpuScope(const BenchmarkGpuScope&) = delete;
    BenchmarkGpuScope& operator=(const BenchmarkGpuScope&) = delete;

private:
    BenchmarkMetric m_metric;
    bool m_enabled;
};
