#include "benchmarkprofiler.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <numeric>
#ifndef VOXELLANCER_HEADLESS_SERVER
#include <GL/glew.h>
#endif

namespace {
    constexpr std::array<const char*, BenchmarkMetricCount> MetricNames{{
        "game_update",
        "world_update",
        "player_update",
        "world_logic",
        "movement_collision_detection",
        "collision_processing_damage",
        "spawn_split_cleanup",
        "bullet_spawn_collision",
        "script_update",
        "event_poller_update",
        "particle_engine_update",
        "mission_system_update",
        "world_object_update",
        "shooting_target_selection",
        "bullet_line_of_fire_query",
        "projectile_movement",
        "game_draw",
        "scene_content_draw",
        "skybox_draw",
        "world_object_draw",
        "hud_draw",
        "particle_draw",
        "world_tree_draw",
        "starfield_draw",
        "render_pipeline",
        "output_blit",
        "buffer_swap_wait",
        "event_polling",
        "other_frame_work",
        "client_network_poll",
        "client_snapshot_sampling",
        "client_input_prediction",
        "client_replica_update",
        "server_simulation",
        "server_poll",
        "server_snapshot_build",
        "server_interest_filter",
        "server_snapshot_delta",
        "server_snapshot_encoding",
        "server_packet_queue"
    }};

    constexpr std::array<const char*, BenchmarkCounterCount> CounterNames{{
        "primary_target_candidates_considered",
        "primary_target_switches",
        "gun_target_candidates_considered",
        "gun_target_refreshes",
        "gun_targets_assigned",
        "weapon_aim_checks",
        "bullet_line_of_fire_queries",
        "bullets_scheduled",
        "snapshot_entities_considered",
        "snapshot_entities_visible",
        "packets_queued",
        "packet_bytes_queued",
        "remote_snapshots_received",
        "remote_voxel_proxies_updated"
    }};

    double percentile(std::vector<double> values, double fraction) {
        if (values.empty()) {
            return 0.0;
        }
        std::sort(values.begin(), values.end());
        const std::size_t index = static_cast<std::size_t>(
            std::ceil(fraction * static_cast<double>(values.size()))) - 1;
        return values[std::min(index, values.size() - 1)];
    }
}

BenchmarkProfiler::BenchmarkProfiler() :
    m_currentFrameIndex(0),
    m_currentSlot(0),
    m_active(false),
    m_frameOpen(false),
    m_gpuSlotUsable(false),
    m_gpuAvailable(false)
{
}

BenchmarkProfiler& BenchmarkProfiler::instance() {
    static BenchmarkProfiler profiler;
    return profiler;
}

void BenchmarkProfiler::beginRun(std::size_t frameCount) {
    if (m_active) {
        endRun();
    }

    m_samples.assign(frameCount, BenchmarkMetricSample{});
    for (auto& slot : m_started) {
        slot.fill(false);
    }
    for (auto& slot : m_completed) {
        slot.fill(false);
    }
    for (auto& slot : m_pendingMetrics) {
        slot.fill(false);
    }
    m_pendingSlots.fill(false);
#ifndef VOXELLANCER_HEADLESS_SERVER
    m_gpuAvailable = GLEW_VERSION_3_3 || GLEW_ARB_timer_query;
    if (m_gpuAvailable) {
        constexpr std::size_t queryCount =
            QueryFramesInFlight * BenchmarkMetricCount * 2;
        m_gpuQueries.resize(queryCount);
        glGenQueries(static_cast<GLsizei>(queryCount), m_gpuQueries.data());
        if (std::any_of(m_gpuQueries.begin(), m_gpuQueries.end(),
                [](unsigned int query) { return query == 0; })) {
            glDeleteQueries(static_cast<GLsizei>(m_gpuQueries.size()), m_gpuQueries.data());
            m_gpuQueries.clear();
            m_gpuAvailable = false;
        }
    }
#else
    m_gpuAvailable = false;
#endif

    m_active = true;
}

void BenchmarkProfiler::beginFrame(std::size_t frameIndex) {
    if (frameIndex >= m_samples.size()) {
        m_samples.resize(frameIndex + 1);
    }
    m_currentFrameIndex = frameIndex;
    m_currentSlot = frameIndex % QueryFramesInFlight;
    m_frameOpen = true;
    m_gpuSlotUsable = m_gpuAvailable;

#ifndef VOXELLANCER_HEADLESS_SERVER
    if (m_gpuAvailable && m_pendingSlots[m_currentSlot]) {
        bool ready = true;
        for (std::size_t metric = 0; metric < BenchmarkMetricCount; ++metric) {
            if (!m_pendingMetrics[m_currentSlot][metric]) {
                continue;
            }

            GLint available = GL_FALSE;
            glGetQueryObjectiv(
                m_gpuQueries[queryIndex(m_currentSlot, metric, true)],
                GL_QUERY_RESULT_AVAILABLE,
                &available);
            ready = ready && available == GL_TRUE;
        }

        if (ready) {
            resolveSlot(m_currentSlot);
        } else {
            m_gpuSlotUsable = false;
        }
    }
#endif

    if (frameIndex < m_samples.size()) {
        m_samples[frameIndex].gpuTimingAvailable = m_gpuSlotUsable;
    }

    if (m_gpuSlotUsable) {
        m_started[m_currentSlot].fill(false);
        m_completed[m_currentSlot].fill(false);
    }
}

void BenchmarkProfiler::endFrame() {
    if (m_gpuSlotUsable) {
        bool hasQueries = false;
        for (std::size_t metric = 0; metric < BenchmarkMetricCount; ++metric) {
            m_pendingMetrics[m_currentSlot][metric] = m_completed[m_currentSlot][metric];
            hasQueries = hasQueries || m_completed[m_currentSlot][metric];
        }
        m_pendingSlots[m_currentSlot] = hasQueries;
        m_pendingFrameIndices[m_currentSlot] = m_currentFrameIndex;
    }

    m_frameOpen = false;
}

void BenchmarkProfiler::endRun() {
    if (!m_active) {
        return;
    }

#ifndef VOXELLANCER_HEADLESS_SERVER
    if (m_gpuAvailable) {
        glFinish();
        for (std::size_t slot = 0; slot < QueryFramesInFlight; ++slot) {
            if (m_pendingSlots[slot]) {
                resolveSlot(slot);
            }
        }
        glDeleteQueries(
            static_cast<GLsizei>(m_gpuQueries.size()),
            m_gpuQueries.data());
        m_gpuQueries.clear();
    }
#endif

    m_frameOpen = false;
    m_active = false;
    m_gpuAvailable = false;
}

bool BenchmarkProfiler::recording() const {
    return m_active && m_frameOpen;
}

void BenchmarkProfiler::addCpuTime(BenchmarkMetric metric, double elapsedMs) {
    if (!recording()) {
        return;
    }
    m_samples[m_currentFrameIndex].cpuMs[static_cast<std::size_t>(metric)] += elapsedMs;
}

void BenchmarkProfiler::addCounter(BenchmarkCounter counter, std::uint64_t count) {
    if (!recording()) {
        return;
    }
    m_samples[m_currentFrameIndex].counters[static_cast<std::size_t>(counter)] += count;
}

void BenchmarkProfiler::beginGpuTime(BenchmarkMetric metric) {
#ifdef VOXELLANCER_HEADLESS_SERVER
    (void)metric;
#else
    if (!recording() || !m_gpuSlotUsable) {
        return;
    }

    const std::size_t metricIndex = static_cast<std::size_t>(metric);
    if (m_started[m_currentSlot][metricIndex]) {
        return;
    }

    m_started[m_currentSlot][metricIndex] = true;
    glQueryCounter(
        m_gpuQueries[queryIndex(m_currentSlot, metricIndex, false)],
        GL_TIMESTAMP);
#endif
}

void BenchmarkProfiler::endGpuTime(BenchmarkMetric metric) {
#ifdef VOXELLANCER_HEADLESS_SERVER
    (void)metric;
#else
    if (!recording() || !m_gpuSlotUsable) {
        return;
    }

    const std::size_t metricIndex = static_cast<std::size_t>(metric);
    if (!m_started[m_currentSlot][metricIndex]) {
        return;
    }

    glQueryCounter(
        m_gpuQueries[queryIndex(m_currentSlot, metricIndex, true)],
        GL_TIMESTAMP);
    m_completed[m_currentSlot][metricIndex] = true;
#endif
}

const std::vector<BenchmarkMetricSample>& BenchmarkProfiler::samples() const {
    return m_samples;
}

void writeBenchmarkProfile(
    std::ostream& output,
    const std::vector<BenchmarkMetricSample>& samples)
{
    output << "{\n"
        << "\"notes\":\"CPU scopes may be nested and overlap; do not sum parent and child timings. GPU timings are asynchronous.\",\n"
        << "\"cpu_ms\":{";
    bool first = true;
    for (std::size_t metric = 0; metric < BenchmarkMetricCount; ++metric) {
        std::vector<double> values;
        values.reserve(samples.size());
        for (const BenchmarkMetricSample& sample : samples) {
            if (sample.cpuMs[metric] > 0.0) {
                values.push_back(sample.cpuMs[metric]);
            }
        }
        if (values.empty()) {
            continue;
        }
        const double total = std::accumulate(values.begin(), values.end(), 0.0);
        output << (first ? "" : ",") << '"' << MetricNames[metric] << "\":{"
            << "\"samples\":" << values.size()
            << ",\"avg_ms\":" << total / static_cast<double>(values.size())
            << ",\"p95_ms\":" << percentile(values, 0.95)
            << ",\"p99_ms\":" << percentile(values, 0.99)
            << ",\"max_ms\":" << *std::max_element(values.begin(), values.end())
            << '}';
        first = false;
    }
    output << "},\n\"gpu_ms\":{";
    first = true;
    for (std::size_t metric = 0; metric < BenchmarkMetricCount; ++metric) {
        std::vector<double> values;
        values.reserve(samples.size());
        for (const BenchmarkMetricSample& sample : samples) {
            if (sample.gpuValid[metric]) {
                values.push_back(sample.gpuMs[metric]);
            }
        }
        if (values.empty()) {
            continue;
        }
        const double total = std::accumulate(values.begin(), values.end(), 0.0);
        output << (first ? "" : ",") << '"' << MetricNames[metric] << "\":{"
            << "\"samples\":" << values.size()
            << ",\"avg_ms\":" << total / static_cast<double>(values.size())
            << ",\"p95_ms\":" << percentile(values, 0.95)
            << ",\"p99_ms\":" << percentile(values, 0.99)
            << ",\"max_ms\":" << *std::max_element(values.begin(), values.end())
            << '}';
        first = false;
    }
    output << "},\n\"counters\":{";
    first = true;
    for (std::size_t counter = 0; counter < BenchmarkCounterCount; ++counter) {
        std::uint64_t total = 0;
        std::uint64_t maximum = 0;
        for (const BenchmarkMetricSample& sample : samples) {
            total += sample.counters[counter];
            maximum = std::max(maximum, sample.counters[counter]);
        }
        if (total == 0) {
            continue;
        }
        output << (first ? "" : ",") << '"' << CounterNames[counter] << "\":{"
            << "\"total\":" << total
            << ",\"avg_per_frame\":"
            << static_cast<double>(total) / static_cast<double>(samples.size())
            << ",\"max_per_frame\":" << maximum << '}';
        first = false;
    }
    output << "}}";
}

std::size_t BenchmarkProfiler::queryIndex(
    std::size_t slot,
    std::size_t metric,
    bool end
) const {
    return (slot * BenchmarkMetricCount + metric) * 2 + (end ? 1 : 0);
}

void BenchmarkProfiler::resolveSlot(std::size_t slot) {
#ifdef VOXELLANCER_HEADLESS_SERVER
    (void)slot;
#else
    const std::size_t frameIndex = m_pendingFrameIndices[slot];
    BenchmarkMetricSample& sample = m_samples[frameIndex];

    for (std::size_t metric = 0; metric < BenchmarkMetricCount; ++metric) {
        if (!m_pendingMetrics[slot][metric]) {
            continue;
        }

        GLuint64 startNanoseconds = 0;
        GLuint64 endNanoseconds = 0;
        glGetQueryObjectui64v(
            m_gpuQueries[queryIndex(slot, metric, false)],
            GL_QUERY_RESULT,
            &startNanoseconds);
        glGetQueryObjectui64v(
            m_gpuQueries[queryIndex(slot, metric, true)],
            GL_QUERY_RESULT,
            &endNanoseconds);
        if (endNanoseconds >= startNanoseconds) {
            sample.gpuMs[metric] =
                static_cast<double>(endNanoseconds - startNanoseconds) / 1000000.0;
            sample.gpuValid[metric] = true;
        }
        m_pendingMetrics[slot][metric] = false;
    }

    m_pendingSlots[slot] = false;
#endif
}

BenchmarkCpuScope::BenchmarkCpuScope(BenchmarkMetric metric) :
    m_metric(metric),
    m_enabled(BenchmarkProfiler::instance().recording()),
    m_start(m_enabled
        ? std::chrono::steady_clock::now()
        : std::chrono::steady_clock::time_point{})
{
}

BenchmarkCpuScope::~BenchmarkCpuScope() {
    if (m_enabled) {
        const double elapsedMs = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - m_start).count();
        BenchmarkProfiler::instance().addCpuTime(m_metric, elapsedMs);
    }
}

BenchmarkGpuScope::BenchmarkGpuScope(BenchmarkMetric metric) :
    m_metric(metric),
    m_enabled(BenchmarkProfiler::instance().recording())
{
    if (m_enabled) {
        BenchmarkProfiler::instance().beginGpuTime(m_metric);
    }
}

BenchmarkGpuScope::~BenchmarkGpuScope() {
    if (m_enabled) {
        BenchmarkProfiler::instance().endGpuTime(m_metric);
    }
}
