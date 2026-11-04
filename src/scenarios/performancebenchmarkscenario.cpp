#include "performancebenchmarkscenario.h"

#include <cmath>
#include <memory>
#include <random>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ai/basictasks/fighttask.h"
#include "ai/character.h"
#include "benchmark/benchmarkmode.h"
#include "player.h"
#include "resource/worldobjectbuilder.h"
#include "utils/handle/handle.h"
#include "voxel/voxel.h"
#include "world/god.h"
#include "world/world.h"
#include "worldobject/ship.h"
#include "worldobject/worldobject.h"
#include "worldobject/worldobjectinfo.h"

namespace {
    constexpr int LargeCombatShipsPerTeam = 400;
    constexpr float LargeCombatClusterCenterOffset = 800.0f;
    constexpr float LargeCombatClusterRadius = 300.0f;
    constexpr float LargeCombatMinimumShipSpacing = 32.0f;

    glm::vec3 randomClusterPosition(
        int team,
        std::mt19937& generator,
        std::normal_distribution<float>& directionComponent,
        std::uniform_real_distribution<float>& radiusDistribution,
        const std::vector<glm::vec3>& existingPositions) {
        const glm::vec3 center(
            team == 0 ? -LargeCombatClusterCenterOffset : LargeCombatClusterCenterOffset,
            0.0f,
            0.0f);
        const float minimumSpacingSquared =
            LargeCombatMinimumShipSpacing * LargeCombatMinimumShipSpacing;

        while (true) {
            glm::vec3 direction(
                directionComponent(generator),
                directionComponent(generator),
                directionComponent(generator));
            const float directionLength = glm::length(direction);
            if (directionLength < 0.0001f) {
                continue;
            }
            direction /= directionLength;

            const float radius = LargeCombatClusterRadius *
                std::cbrt(radiusDistribution(generator));
            const glm::vec3 candidate = center + direction * radius;
            bool hasSpacing = true;
            for (const glm::vec3& position : existingPositions) {
                const glm::vec3 delta = candidate - position;
                if (glm::dot(delta, delta) < minimumSpacingSquared) {
                    hasSpacing = false;
                    break;
                }
            }
            if (hasSpacing) {
                return candidate;
            }
        }
    }
}

PerformanceBenchmarkScenario::PerformanceBenchmarkScenario(GamePlay* gamePlay, BenchmarkMode mode) :
    BaseScenario(gamePlay),
    m_mode(mode)
{
}

void PerformanceBenchmarkScenario::populateWorld() {
    if (m_mode == BenchmarkMode::Rendering) {
        populateRenderingBenchmark();
    } else if (m_mode == BenchmarkMode::Combat) {
        populateCombatBenchmark();
    } else {
        populateLargeCombatVsyncBenchmark();
    }
}

void PerformanceBenchmarkScenario::populateRenderingBenchmark() {
    constexpr int tunnelLength = 192;
    constexpr int width = 32;
    constexpr int height = 24;

    WorldObject* tunnel = new WorldObject();
    tunnel->info().setName("rendering_benchmark_tunnel");
    tunnel->info().setShowOnHud(false);
    tunnel->info().setCanLockOn(false);

    for (int z = 0; z < tunnelLength; ++z) {
        const unsigned int color = (z / 4) % 2 == 0 ? 0x4C7899 : 0x7B543A;
        for (int y = 0; y <= height; ++y) {
            for (int x = 0; x <= width; ++x) {
                if (x != 0 && x != width && y != 0 && y != height) {
                    continue;
                }
                tunnel->addVoxel(new Voxel(glm::ivec3(x, y, z), color));
            }
        }
    }

    m_world->god().scheduleSpawn(tunnel);
}

void PerformanceBenchmarkScenario::populateCombatBenchmark() {
    constexpr int shipsPerTeam = 12;
    constexpr int columns = 3;
    constexpr float teamOffset = 38.0f;
    constexpr float laneSpacing = 12.0f;
    constexpr float rowSpacing = 16.0f;
    std::vector<Ship*> teams[2];

    for (int team = 0; team < 2; ++team) {
        for (int i = 0; i < shipsPerTeam; ++i) {
            Ship* ship = WorldObjectBuilder("basicship").buildShip();
            const int lane = i % columns;
            const int row = i / columns;
            const float x = team == 0 ? -teamOffset : teamOffset;
            const float y = static_cast<float>(lane - 1) * laneSpacing;
            const float z = static_cast<float>(row - 1) * rowSpacing;
            const float yaw = team == 0 ? -glm::half_pi<float>() : glm::half_pi<float>();

            ship->transform().setPosition(glm::vec3(x, y, z));
            ship->transform().setOrientation(glm::angleAxis(yaw, glm::vec3(0, 1, 0)));
            ship->info().setName(team == 0 ? "benchmark_blue" : "benchmark_red");
            ship->info().setShowOnHud(false);
            ship->info().setCanLockOn(false);

            teams[team].push_back(ship);
            m_world->god().scheduleSpawn(ship);
        }
    }

    assignCombatTasks(teams);
}

void PerformanceBenchmarkScenario::populateLargeCombatVsyncBenchmark() {
    std::mt19937 generator(LargeCombatPlacementSeed);
    std::normal_distribution<float> directionComponent(0.0f, 1.0f);
    std::uniform_real_distribution<float> radiusDistribution(0.0f, 1.0f);
    std::vector<Ship*> teams[2];

    for (int team = 0; team < 2; ++team) {
        std::vector<glm::vec3> positions;
        positions.reserve(LargeCombatShipsPerTeam);
        for (int i = 0; i < LargeCombatShipsPerTeam; ++i) {
            Ship* ship = WorldObjectBuilder("basicship").buildShip();
            const float yaw = team == 0 ? -glm::half_pi<float>() : glm::half_pi<float>();
            const glm::vec3 position = randomClusterPosition(
                team, generator, directionComponent, radiusDistribution, positions);

            positions.push_back(position);
            ship->transform().setPosition(position);
            ship->transform().setOrientation(glm::angleAxis(yaw, glm::vec3(0, 1, 0)));
            ship->info().setName(team == 0 ? "benchmark_blue" : "benchmark_red");
            ship->info().setShowOnHud(false);
            ship->info().setCanLockOn(false);

            teams[team].push_back(ship);
            m_world->god().scheduleSpawn(ship);
        }
    }

    assignCombatTasks(teams);
}

void PerformanceBenchmarkScenario::assignCombatTasks(std::vector<Ship*> teams[2]) {
    World::instance()->player().setShip(teams[0].front());

    for (int team = 0; team < 2; ++team) {
        std::vector<Handle<WorldObject>> targets;
        for (Ship* target : teams[1 - team]) {
            targets.push_back(target->handle<WorldObject>());
        }
        for (Ship* ship : teams[team]) {
            ship->character()->setTask(std::make_shared<FightTask>(ship->boardComputer(), targets));
        }
    }
}
