#include <bandit/bandit.h>

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "utils/simulationtickrunner.h"

using namespace bandit;

go_bandit([]() {
    describe("simulation tick runner", []() {
        it("advances the same number of ticks at common render rates", []() {
            const std::uint32_t renderRates[] = { 30, 60, 144 };

            for (std::uint32_t renderRate : renderRates) {
                SimulationTickRunner runner;
                std::vector<std::uint64_t> tickIndices;
                const double frameDuration = 1.0 / renderRate;

                for (std::uint32_t frame = 0; frame < renderRate; ++frame) {
                    runner.advance(frameDuration,
                        [&tickIndices](float tickDurationSeconds, std::uint64_t tickIndex) {
                            AssertThat(tickDurationSeconds,
                                EqualsWithDelta(1.0f / 30.0f, 1e-7f));
                            tickIndices.push_back(tickIndex);
                        });
                }

                AssertThat(tickIndices.size(), Equals(static_cast<std::size_t>(30)));
                AssertThat(runner.tickIndex(), Equals(static_cast<std::uint64_t>(30)));
                for (std::uint64_t tick = 0; tick < runner.tickIndex(); ++tick) {
                    AssertThat(tickIndices[static_cast<std::size_t>(tick)], Equals(tick));
                }
            }
        });

        it("bounds catch-up work and retains the remaining simulation time", []() {
            SimulationTickRunner runner(5);
            std::uint32_t callbackCount = 0;

            const std::uint32_t firstAdvance = runner.advance(0.5,
                [&callbackCount](float, std::uint64_t) {
                    ++callbackCount;
                });

            AssertThat(firstAdvance, Equals(static_cast<std::uint32_t>(5)));
            AssertThat(callbackCount, Equals(static_cast<std::uint32_t>(5)));
            AssertThat(runner.backlogSeconds() >= 9.0 * SimulationTickRunner::TickDurationSeconds,
                IsTrue());

            while (runner.backlogSeconds() >= SimulationTickRunner::TickDurationSeconds) {
                const std::uint32_t ticksRun = runner.advance(0.0,
                    [&callbackCount](float, std::uint64_t) {
                        ++callbackCount;
                    });
                AssertThat(ticksRun <= 5, IsTrue());
            }

            AssertThat(runner.tickIndex(), Equals(static_cast<std::uint64_t>(15)));
            AssertThat(callbackCount, Equals(static_cast<std::uint32_t>(15)));
        });

        it("rejects invalid elapsed time and a zero catch-up limit", []() {
            bool rejectedElapsedTime = false;
            bool rejectedTickLimit = false;
            SimulationTickRunner runner;

            try {
                runner.advance(-1.0, [](float, std::uint64_t) {});
            } catch (const std::invalid_argument&) {
                rejectedElapsedTime = true;
            }

            try {
                SimulationTickRunner invalidRunner(0);
            } catch (const std::invalid_argument&) {
                rejectedTickLimit = true;
            }

            AssertThat(rejectedElapsedTime, IsTrue());
            AssertThat(rejectedTickLimit, IsTrue());
        });
    });
});
