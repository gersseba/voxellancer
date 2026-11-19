#pragma once

#include <cmath>
#include <cstdint>
#include <stdexcept>

class SimulationTickRunner {
public:
    static constexpr double TickRate = 30.0;
    static constexpr double TickDurationSeconds = 1.0 / TickRate;
    static constexpr std::uint32_t DefaultMaxTicksPerAdvance = 5;

    explicit SimulationTickRunner(
        std::uint32_t maxTicksPerAdvance = DefaultMaxTicksPerAdvance
    ) :
        m_maxTicksPerAdvance(maxTicksPerAdvance)
    {
        if (m_maxTicksPerAdvance == 0) {
            throw std::invalid_argument("SimulationTickRunner requires a positive tick limit.");
        }
    }

    template <typename TickFunction>
    std::uint32_t advance(double elapsedSeconds, TickFunction&& tickFunction) {
        if (!std::isfinite(elapsedSeconds) || elapsedSeconds < 0.0) {
            throw std::invalid_argument("Simulation elapsed time must be finite and non-negative.");
        }

        m_accumulatorSeconds += elapsedSeconds;
        std::uint32_t ticksRun = 0;
        while (m_accumulatorSeconds + 1e-12 >= TickDurationSeconds &&
            ticksRun < m_maxTicksPerAdvance) {
            tickFunction(static_cast<float>(TickDurationSeconds), m_tickIndex);
            ++m_tickIndex;
            ++ticksRun;
            m_accumulatorSeconds -= TickDurationSeconds;
            if (m_accumulatorSeconds < 0.0 && m_accumulatorSeconds > -1e-12) {
                m_accumulatorSeconds = 0.0;
            }
        }

        return ticksRun;
    }

    std::uint64_t tickIndex() const {
        return m_tickIndex;
    }

    double backlogSeconds() const {
        return m_accumulatorSeconds;
    }

private:
    const std::uint32_t m_maxTicksPerAdvance;
    std::uint64_t m_tickIndex = 0;
    double m_accumulatorSeconds = 0.0;
};
