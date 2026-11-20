#include <bandit/bandit.h>

#include "ui/contactmarker.h"

using namespace bandit;

go_bandit([]() {
    describe("Enemy contact marker projection", []() {
        it("tracks a visible contact without flipping screen coordinates", []() {
            const auto marker = projectContactMarker(glm::vec4(0.4f, 0.2f, 0, 2));
            AssertThat(marker.onScreen, IsTrue());
            AssertThat(marker.behind, IsFalse());
            AssertThat(marker.position.x, EqualsWithDelta(0.6f, 1e-6f));
            AssertThat(marker.position.y, EqualsWithDelta(0.45f, 1e-6f));
        });

        it("keeps offscreen contacts within the readable screen margin", []() {
            for (const auto clip : {glm::vec4(4, 0, 0, 1),
                    glm::vec4(-4, 0, 0, 1), glm::vec4(0, 4, 0, 1),
                    glm::vec4(0, -4, 0, 1)}) {
                const auto marker = projectContactMarker(clip);
                AssertThat(marker.onScreen, IsFalse());
                AssertThat(marker.position.x >= 0.079f &&
                    marker.position.x <= 0.921f, IsTrue());
                AssertThat(marker.position.y >= 0.169f &&
                    marker.position.y <= 0.831f, IsTrue());
            }
        });

        it("shows a stable behind cue even on the rear axis or camera plane", []() {
            const auto rear = projectContactMarker(glm::vec4(0, 0, 0, -1));
            AssertThat(rear.behind, IsTrue());
            AssertThat(rear.onScreen, IsFalse());
            AssertThat(rear.position.y, EqualsWithDelta(0.83f, 1e-6f));
            const auto plane = projectContactMarker(glm::vec4(1, 0, 0, 0));
            AssertThat(plane.onScreen, IsFalse());
            AssertThat(std::isfinite(plane.position.x), IsTrue());
            AssertThat(plane.position.x, EqualsWithDelta(0.92f, 1e-6f));
        });
    });
});
