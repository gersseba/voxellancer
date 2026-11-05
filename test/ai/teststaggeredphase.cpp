#include <bandit/bandit.h>

#include "utils/staggeredphase.h"

using namespace bandit;

go_bandit([]() {
    describe("staggered target refresh phases", []() {
        it("derives repeatable phases from positions", []() {
            const std::uint32_t key =
                StaggeredPhase::keyForPosition(glm::vec3(12.5f, -8.0f, 3.25f));
            const float first = StaggeredPhase::initialPhase(key, 0.5f);
            const float second = StaggeredPhase::initialPhase(key, 0.5f);

            AssertThat(first, Equals(second));
        });

        it("varies refresh intervals within the configured bounds", []() {
            const std::uint32_t key =
                StaggeredPhase::keyForPosition(glm::vec3(12.5f, -8.0f, 3.25f));
            bool intervalsDiffer = false;
            const float first = StaggeredPhase::period(key, 0.5f, 0);

            for (std::uint32_t cycle = 0; cycle < 32; ++cycle) {
                const float interval = StaggeredPhase::period(key, 0.5f, cycle);
                AssertThat(interval >= 0.425f && interval <= 0.575f, IsTrue());
                intervalsDiffer = intervalsDiffer || interval != first;
            }

            AssertThat(intervalsDiffer, IsTrue());
        });
    });
});
