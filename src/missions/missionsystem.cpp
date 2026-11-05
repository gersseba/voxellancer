#include "missionsystem.h"

#include <stdexcept>

#include "scripting/scriptengine.h"

#include "world/world.h"

#include "mission.h"


MissionSystem::MissionSystem(World& world):
    m_world(world)
{
}

MissionSystem::~MissionSystem() = default;

void MissionSystem::update(float deltaSec) {
    for (auto iter = m_missions.begin(); iter != m_missions.end(); ) {
        Mission* mission = iter->second.get();

        mission->update(deltaSec);

        if (mission->state() != MissionState::Running) {
            iter = m_missions.erase(iter);
        } else {
            ++iter;
        }
    }
}

void MissionSystem::setStateEventReplicationEnabled(bool enabled) {
    m_stateEventReplicationEnabled = enabled;
    if (!enabled) {
        m_stateEvents.clear();
    }
}

void MissionSystem::addMission(std::shared_ptr<Mission> mission) {
    m_missions[mission.get()] = mission;
    m_world.scriptEngine().registerScriptable(mission.get());
    mission->start();
}

void MissionSystem::removeMission(Mission* mission) {
    m_missions.erase(mission);
    m_world.scriptEngine().unregisterScriptable(mission);
}

void MissionSystem::recordStateChange(
    const std::string& missionKey,
    voxellancer::protocol::MissionEventState state
) {
    if (!m_stateEventReplicationEnabled) {
        return;
    }
    if (missionKey.empty() ||
        missionKey.size() > voxellancer::protocol::MaxMissionKeyBytes ||
        missionKey.find('\0') != std::string::npos) {
        throw std::invalid_argument("Mission key is invalid for replication.");
    }
    if (m_stateEvents.size() >= MaxQueuedStateEvents) {
        throw std::overflow_error("Mission-state event queue exceeded its limit.");
    }
    m_stateEvents.push_back({
        m_world.simulationTickIndex(),
        state,
        missionKey
    });
}

std::vector<voxellancer::protocol::MissionStateEvent>
MissionSystem::drainStateEvents() {
    std::vector<voxellancer::protocol::MissionStateEvent> events;
    events.swap(m_stateEvents);
    return events;
}
