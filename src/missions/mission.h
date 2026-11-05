#pragma once

#include <memory>
#include <string>

#include "utils/handle/handleowner.h"

#include "scripting/scriptable.h"


class MissionScript;
class World;

enum class MissionState {
    Idle,
    Running,
    Failed,
    Succeeded
};

class Mission : public Scriptable, public HandleOwner {
public:
    Mission(World& world, const std::string& path);
    ~Mission();

    void start();

    MissionState state() const;

    /**
     * Mark the mission as successfully played.
     * Calls onSuccess() in the script
     */
    void succeed();

    /**
     * Mark the mission as failed.
     * Calls onFailure() in the script
     */
    void fail();

    void update(float deltaSec);


protected:
    World& m_world;
    std::shared_ptr<MissionScript> m_script;
    std::string m_missionKey;
    MissionState m_state;
};
