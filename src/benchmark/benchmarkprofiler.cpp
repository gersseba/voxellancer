#include "benchmarkprofiler.h"

#include <algorithm>
#include <cstdint>
#include <GL/glew.h>

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

    m_active = true;
}

void BenchmarkProfiler::beginFrame(std::size_t frameIndex) {
    m_currentFrameIndex = frameIndex;
    m_currentSlot = frameIndex % QueryFramesInFlight;
    m_frameOpen = true;
    m_gpuSlotUsable = m_gpuAvailable;

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

void BenchmarkProfiler::beginGpuTime(BenchmarkMetric metric) {
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
}

void BenchmarkProfiler::endGpuTime(BenchmarkMetric metric) {
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
}

const std::vector<BenchmarkMetricSample>& BenchmarkProfiler::samples() const {
    return m_samples;
}

std::size_t BenchmarkProfiler::queryIndex(
    std::size_t slot,
    std::size_t metric,
    bool end
) const {
    return (slot * BenchmarkMetricCount + metric) * 2 + (end ? 1 : 0);
}

void BenchmarkProfiler::resolveSlot(std::size_t slot) {
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
