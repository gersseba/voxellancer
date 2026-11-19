#include <bandit/bandit.h>

#include "client/inputpredictor.h"

using namespace bandit;
using namespace voxellancer::protocol;

go_bandit([]() {
    describe("client local input prediction", []() {
        it("replays scheduled movement from authoritative state", []() {
            EntitySnapshot authoritative;
            authoritative.id = EntityId{1, 1};
            authoritative.kind = EntitySnapshot::Kind::Ship;
            authoritative.maxMass = 1.0f;
            InputCommand movement;
            movement.sequence = 1;
            movement.tick = 2;
            movement.moveZ = -32768;

            const EnginePower enginePower(
                glm::vec4(0.0f, 0.0f, 60.0f, 60.0f),
                glm::vec3(0.0f));
            const auto predicted = InputPredictor().predict(
                authoritative, 1, 4, {movement}, enginePower, 0.0f, 0.0f);

            AssertThat(predicted.has_value(), IsTrue());
            AssertThat(
                predicted->directionalVelocity.z,
                EqualsWithDelta(-2.0f, 1e-5f));
            AssertThat(
                predicted->position.z,
                EqualsWithDelta(-2.0f / 30.0f, 1e-5f));
        });

        it("reconciles from each authoritative snapshot and bounds replay", []() {
            EntitySnapshot authoritative;
            authoritative.id = EntityId{1, 1};
            authoritative.kind = EntitySnapshot::Kind::Ship;
            authoritative.position = glm::vec3(7.0f, 0.0f, 0.0f);
            authoritative.maxMass = 1.0f;
            InputCommand movement;
            movement.sequence = 1;
            movement.tick = 2;
            movement.moveX = 32767;
            const EnginePower enginePower(
                glm::vec4(60.0f, 60.0f, 60.0f, 60.0f),
                glm::vec3(0.0f));
            InputPredictor predictor;
            const auto eightTicks = predictor.predict(
                authoritative, 1, 9, {movement}, enginePower, 0.0f, 0.0f);
            const auto beyondHorizon = predictor.predict(
                authoritative, 1, 30, {movement}, enginePower, 0.0f, 0.0f);
            AssertThat(eightTicks.has_value(), IsTrue());
            AssertThat(beyondHorizon.has_value(), IsTrue());
            AssertThat(
                beyondHorizon->position,
                Equals(eightTicks->position));

            authoritative.position = glm::vec3(20.0f, 0.0f, 0.0f);
            const auto reconciled = predictor.predict(
                authoritative, 9, 9, {movement}, enginePower, 0.0f, 0.0f);
            AssertThat(reconciled.has_value(), IsTrue());
            AssertThat(reconciled->position.x, Equals(20.0f));
        });

        it("rebases delayed input replay on an authoritative post-impact state", []() {
            EntitySnapshot authoritative;
            authoritative.id = EntityId{2, 1};
            authoritative.kind = EntitySnapshot::Kind::Ship;
            authoritative.position = glm::vec3(5.0f, 0.0f, 0.0f);
            authoritative.directionalVelocity = glm::vec3(-2.0f, 0.0f, 0.0f);
            authoritative.maxMass = 1.0f;
            InputCommand delayedMovement;
            delayedMovement.sequence = 4;
            delayedMovement.tick = 11;
            delayedMovement.moveX = 32767;
            const EnginePower enginePower(
                glm::vec4(60.0f),
                glm::vec3(0.0f));

            const auto reconciled = InputPredictor().predict(
                authoritative,
                10,
                13,
                {delayedMovement},
                enginePower,
                0.0f,
                0.0f);

            AssertThat(reconciled.has_value(), IsTrue());
            AssertThat(
                reconciled->position.x,
                EqualsWithDelta(5.0f - 4.0f / 30.0f, 1e-5f));
            AssertThat(
                reconciled->directionalVelocity.x,
                EqualsWithDelta(0.0f, 1e-5f));
            AssertThat(
                reconciled->engineState.x,
                EqualsWithDelta(1.0f, 1e-5f));
        });
    });
});
