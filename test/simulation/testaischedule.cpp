#include <bandit/bandit.h>

#include "ai/aitask.h"
#include "ai/boardcomputer.h"
#include "ai/character.h"
#include "world/world.h"
#include "worldobject/ship.h"

using namespace bandit;

namespace {
    class CountingTask : public AiTask {
    public:
        explicit CountingTask(BoardComputer* boardComputer) : AiTask(boardComputer) {}

        void update(float deltaSec) override {
            ++updateCount;
            accumulatedSeconds += deltaSec;
        }

        bool isFinished() override {
            return false;
        }

        unsigned int updateCount = 0;
        float accumulatedSeconds = 0.0f;
    };

    struct WorldResetGuard {
        ~WorldResetGuard() {
            World::reset(false);
        }
    };
}

go_bandit([]() {
    describe("fixed-tick AI schedule", []() {
        it("updates ship AI at 10 Hz with stable six-tick phases", []() {
            World::reset(false);
            WorldResetGuard resetGuard;
            World* world = World::instance(World::Mode::Server);
            Ship ship(*world);
            ship.setAiUpdatePhase(0);
            auto task = std::make_shared<CountingTask>(ship.boardComputer());
            ship.character()->setTask(task);

            for (int tick = 0; tick < 60; ++tick) {
                world->update(1.0f / 60.0f);
                ship.update(1.0f / 60.0f);
            }

            AssertThat(task->updateCount, Equals(10u));
            AssertThat(task->accumulatedSeconds, EqualsWithDelta(1.0f, 1e-6f));
            AssertThat(world->simulationTickIndex(), Equals(std::uint64_t{60}));
        });
    });
});
