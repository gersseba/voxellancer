#pragma once

#include <vector>
#include <list>

#include <glm/glm.hpp>
#include <glm/gtx/quaternion.hpp>

#include "equipment/enginestate.h"

#include "utils/handle/handle.h"

class WorldObject;
class Hardpoint;
class Weapon;

/**
 *  The BoardComputer abstracts the Ship's systems to a level usable to AiTask, Character and Player
 */
class BoardComputer {
public:
    BoardComputer(WorldObject* worldObject);

    WorldObject* worldObject();

    const EngineState& engineState() const;

    void moveTo(const glm::vec3& position, bool decelerate = true);
    void rotateTo(const glm::vec3& position, const glm::vec3& up = glm::vec3(0, 0, 0));

    void shootBullet(std::vector<Handle<WorldObject>>& targets, float deltaSec);
    void shootBullet(Handle<WorldObject>& target, float deltaSec);
    void shootRockets(WorldObject* target);

    void update(float deltaSec);


protected:
    struct GunTargetSelection {
        Hardpoint* hardpoint = nullptr;
        Weapon* weapon = nullptr;
        Handle<WorldObject> target;
        float refreshCountdown = 0.0f;
        bool initialized = false;
    };

    WorldObject* m_worldObject;
    EngineState m_engineState;
    bool m_overwriteEngineState;
    std::vector<GunTargetSelection> m_gunTargetSelections;

    /* Return the euler angles needed to adjust 'up' */
    glm::vec3 rotateUpTo(const glm::vec3& up);
    glm::vec3 rotateUpAuto(const glm::quat& rotation);

private:
    void shootBulletsAtTargets(
        std::vector<Handle<WorldObject>>* targets,
        Handle<WorldObject>* focusedTarget,
        float deltaSec
    );
    Handle<WorldObject> selectGunTarget(
        Hardpoint* hardpoint,
        std::vector<Handle<WorldObject>>* targets,
        Handle<WorldObject>* focusedTarget,
        Handle<WorldObject>& currentTarget
    );
};
