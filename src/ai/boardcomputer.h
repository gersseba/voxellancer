#pragma once

#include <cstdint>
#include <random>
#include <vector>
#include <list>

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

#include "equipment/enginestate.h"

#include "utils/handle/handle.h"

class WorldObject;
class World;
class Hardpoint;
class Weapon;

/**
 *  The BoardComputer abstracts the Ship's systems to a level usable to AiTask, Character and Player
 */
class BoardComputer {
    friend class ShootingController;
public:
    explicit BoardComputer(WorldObject* worldObject);
    BoardComputer(WorldObject* worldObject, World& world);

    WorldObject* worldObject();
    void seedRandom(std::uint64_t seed);
    float randomFloat(float from, float to);

    const EngineState& engineState() const;
    void setEngineState(const EngineState& state);

    void moveTo(const glm::vec3& position, bool decelerate = true);
    void rotateTo(const glm::vec3& position, const glm::vec3& up = glm::vec3(0, 0, 0));

    void shootBullet(std::vector<Handle<WorldObject>>& targets);
    void shootBullet(Handle<WorldObject>& target);
    void shootRockets(WorldObject* target);

    void update(float deltaSec);


protected:
    struct GunTargetSelection {
        Hardpoint* hardpoint = nullptr;
        Weapon* weapon = nullptr;
        Handle<WorldObject> target;
        std::uint64_t nextRefreshTick = 0;
        std::uint32_t scheduleKey = 0;
        std::uint32_t refreshCycle = 0;
        bool initialized = false;
    };

    WorldObject* m_worldObject;
    World* m_world;
    EngineState m_engineState;
    bool m_overwriteEngineState;
    std::vector<GunTargetSelection> m_gunTargetSelections;
    std::mt19937 m_randomEngine;

    /* Return the euler angles needed to adjust 'up' */
    glm::vec3 rotateUpTo(const glm::vec3& up);
    glm::vec3 rotateUpAuto(const glm::quat& rotation);

private:
    void shootBulletsAtTargets(
        std::vector<Handle<WorldObject>>* targets,
        Handle<WorldObject>* focusedTarget,
        std::uint64_t tick
    );
    Handle<WorldObject> selectGunTarget(
        Hardpoint* hardpoint,
        std::vector<Handle<WorldObject>>* targets,
        Handle<WorldObject>* focusedTarget,
        Handle<WorldObject>& currentTarget
    );
};
