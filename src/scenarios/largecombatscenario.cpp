#include "largecombatscenario.h"

#include <cmath>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include "ai/basictasks/fighttask.h"
#include "ai/character.h"
#ifndef VOXELLANCER_HEADLESS_SERVER
#include "player.h"
#endif
#include "resource/worldobjectbuilder.h"
#include "utils/handle/handle.h"
#include "voxel/voxel.h"
#include "world/god.h"
#include "world/world.h"
#include "worldobject/ship.h"
#include "worldobject/worldobject.h"
#include "worldobject/worldobjectinfo.h"

namespace {
    void validateShipsPerTeam(int shipsPerTeam) {
        if (shipsPerTeam < 1 || shipsPerTeam > 400) {
            throw std::invalid_argument("Benchmark ships per team must be between 1 and 400.");
        }
    }

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

LargeCombatScenario::LargeCombatScenario(
    World& world,
    int shipsPerTeam
) :
    BaseScenario(world),
    m_shipsPerTeam(shipsPerTeam)
{
    validateShipsPerTeam(m_shipsPerTeam);
}

void LargeCombatScenario::populateWorld() {
    populateLargeCombat();
}

void LargeCombatScenario::populateLargeCombat() {
    std::mt19937 generator(PlacementSeed);
    std::normal_distribution<float> directionComponent(0.0f, 1.0f);
    std::uniform_real_distribution<float> radiusDistribution(0.0f, 1.0f);
    std::vector<Ship*> teams[2];

    for (int team = 0; team < 2; ++team) {
        std::vector<glm::vec3> positions;
        positions.reserve(m_shipsPerTeam);
        for (int i = 0; i < m_shipsPerTeam; ++i) {
            Ship* ship = WorldObjectBuilder("basicship", *m_world).buildShip();
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

    for (int team = 0; team < 2; ++team) {
        std::vector<Handle<WorldObject>> targets;
        for (Ship* target : teams[1 - team]) {
            targets.push_back(target->handle<WorldObject>());
        }
        for (Ship* ship : teams[team]) {
            ship->character()->setTask(std::make_shared<FightTask>(
                ship->boardComputer(), targets));
        }
    }

#ifndef VOXELLANCER_HEADLESS_SERVER
    if (m_world->hasPresentation()) {
        m_world->player().setShip(teams[0].front());
    }
#endif
}
