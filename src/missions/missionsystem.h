#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "protocol/serverprotocol.h"

class Mission;
class World;

class MissionSystem {
public:
    explicit MissionSystem(World& world);
    ~MissionSystem();

    void update(float deltaSec);

    void setStateEventReplicationEnabled(bool enabled);
    void addMission(std::shared_ptr<Mission> mission);
    void removeMission(Mission* mission);
    std::vector<voxellancer::protocol::MissionStateEvent>
    drainStateEvents();


protected:
    World& m_world;
    std::unordered_map<Mission*, std::shared_ptr<Mission>> m_missions;
    std::vector<voxellancer::protocol::MissionStateEvent> m_stateEvents;
    bool m_stateEventReplicationEnabled = false;

private:
    friend class Mission;
    void recordStateChange(
        const std::string& missionKey,
        voxellancer::protocol::MissionEventState state
    );

    static constexpr std::size_t MaxQueuedStateEvents = 256;
};
