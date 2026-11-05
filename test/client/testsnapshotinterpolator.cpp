#include <bandit/bandit.h>

#include <cmath>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "client/snapshotinterpolator.h"

using namespace bandit;
using namespace voxellancer::protocol;

go_bandit([]() {
    describe("client snapshot interpolation", []() {
        it("interpolates remote state and holds entities until lifecycle ticks", []() {
            EntitySnapshot first;
            first.id = EntityId{2, 1};
            first.position = glm::vec3(0.0f);

            EntitySnapshot second = first;
            second.position = glm::vec3(10.0f, 4.0f, -2.0f);
            second.scale = 3.0f;
            second.orientation = glm::angleAxis(
                glm::half_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
            second.directionalVelocity = glm::vec3(10.0f, 0.0f, 0.0f);
            second.shieldFraction = 1.0f;
            second.engineState = glm::vec3(1.0f, -1.0f, 0.5f);
            second.hullFraction = 0.25f;

            SnapshotInterpolator interpolator;
            AssertThat(interpolator.push(10, {first}), IsTrue());
            AssertThat(interpolator.push(20, {second}), IsTrue());

            const auto middle = interpolator.sample(first.id, 15.0);
            AssertThat(middle.has_value(), IsTrue());
            AssertThat(middle->position.x, EqualsWithDelta(5.0f, 1e-6f));
            AssertThat(middle->position.y, EqualsWithDelta(2.0f, 1e-6f));
            AssertThat(
                middle->directionalVelocity.x,
                EqualsWithDelta(5.0f, 1e-6f)
            );
            AssertThat(middle->shieldFraction, EqualsWithDelta(0.5f, 1e-6f));
            AssertThat(
                middle->engineState,
                Equals(glm::vec3(0.5f, -0.5f, 0.25f)));
            AssertThat(middle->hullFraction, EqualsWithDelta(0.125f, 1e-6f));
            AssertThat(middle->scale, EqualsWithDelta(2.0f, 1e-6f));
            const glm::quat expectedOrientation = glm::angleAxis(
                glm::quarter_pi<float>(), glm::vec3(0.0f, 1.0f, 0.0f));
            AssertThat(
                std::abs(glm::dot(middle->orientation, expectedOrientation)),
                EqualsWithDelta(1.0f, 1e-6f)
            );
            const auto predicted = interpolator.sample(first.id, 23.0);
            AssertThat(predicted.has_value(), IsTrue());
            AssertThat(predicted->position.x, EqualsWithDelta(10.5f, 1e-6f));
            const auto boundedPrediction = interpolator.sample(first.id, 40.0);
            AssertThat(boundedPrediction.has_value(), IsTrue());
            AssertThat(
                boundedPrediction->position.x,
                EqualsWithDelta(10.5f, 1e-6f));

            AssertThat(interpolator.push(30, {}), IsTrue());
            AssertThat(interpolator.sample(first.id, 25.0).has_value(), IsTrue());
            AssertThat(interpolator.sample(first.id, 30.0).has_value(), IsFalse());
            AssertThat(interpolator.push(29, {second}), IsFalse());
        });

        it("rejects unordered or duplicate entity IDs", []() {
            EntitySnapshot first;
            first.id = EntityId{2, 1};
            EntitySnapshot second;
            second.id = EntityId{1, 1};

            SnapshotInterpolator interpolator;
            AssertThat(interpolator.push(1, {first, second}), IsFalse());
            AssertThat(interpolator.push(1, {first, first}), IsFalse());
            AssertThat(interpolator.push(1, {first}), IsTrue());
        });

        it("does not present new entities before their first snapshot tick", []() {
            EntitySnapshot existing;
            existing.id = EntityId{2, 1};
            EntitySnapshot spawned;
            spawned.id = EntityId{3, 1};

            SnapshotInterpolator interpolator;
            AssertThat(interpolator.push(10, {existing}), IsTrue());
            AssertThat(interpolator.push(20, {existing, spawned}), IsTrue());
            AssertThat(interpolator.sample(spawned.id, 15.0).has_value(), IsFalse());
            AssertThat(interpolator.sample(spawned.id, 20.0).has_value(), IsTrue());
        });
    });
});
