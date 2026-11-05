#include <bandit/bandit.h>

#include "utils/staggeredphase.h"

using namespace bandit;

go_bandit([]() {
    describe("staggered target refresh phases", []() {
        it("derives repeatable phases from positions", []() {
            const std::uint32_t key =
                StaggeredPhase::keyForPosition(glm::vec3(12.5f, -8.0f, 3.25f));
            const std::uint32_t first = StaggeredPhase::initialPhaseTicks(key, 30);
            const std::uint32_t second = StaggeredPhase::initialPhaseTicks(key, 30);

            AssertThat(first, Equals(second));
            AssertThat(first < 30U, IsTrue());
        });

        it("varies tick intervals within the configured bounds", []() {
            const std::uint32_t key =
                StaggeredPhase::keyForPosition(glm::vec3(12.5f, -8.0f, 3.25f));
            bool intervalsDiffer = false;
            const std::uint32_t first = StaggeredPhase::periodTicks(key, 30, 0);

            for (std::uint32_t cycle = 0; cycle < 32; ++cycle) {
                const std::uint32_t interval =
                    StaggeredPhase::periodTicks(key, 30, cycle);
                AssertThat(interval >= 26U && interval <= 34U, IsTrue());
                intervalsDiffer = intervalsDiffer || interval != first;
            }

            AssertThat(intervalsDiffer, IsTrue());
            AssertThat(StaggeredPhase::periodTicks(key, 0, 0), Equals(0U));
        });
    });
});
