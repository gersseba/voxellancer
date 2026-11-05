#include "mission.h"

#include "scripting/missionscript.h"
#include "scripting/scriptengine.h"
#include "scripting/elematelua/luawrapper.h"

#include "missions/missionsystem.h"

#include "world/world.h"

#ifndef VOXELLANCER_HEADLESS_SERVER
#include "ui/hud/hud.h"
#include "player.h"
#endif


Mission::Mission(World& world, const std::string& path):
    m_world(world),
    m_script(new MissionScript(*this, world.scriptEngine())),
    m_missionKey(path),
    m_state(MissionState::Idle)
{
    m_script->load(path);
    world.scriptEngine().addScript(m_script);
}

Mission::~Mission() = default;

void Mission::start() {
    m_state = MissionState::Running;
    m_world.missionSystem().recordStateChange(
        m_missionKey,
        voxellancer::protocol::MissionEventState::Started);
#ifndef VOXELLANCER_HEADLESS_SERVER
    if (m_world.hasPresentation()) {
        m_world.player().hud().showMissionInfo(
            m_script->luaWrapper().call<std::string>("missionTitle"),
            m_script->luaWrapper().call<std::string>("missionCaption")
        );
    }
#endif
}

MissionState Mission::state() const {
    return m_state;
}

void Mission::succeed() {
    assert(m_state == MissionState::Running);

    m_state = MissionState::Succeeded;
    m_world.missionSystem().recordStateChange(
        m_missionKey,
        voxellancer::protocol::MissionEventState::Succeeded);
    m_script->onSuccess();
    m_script->stop();
}

void Mission::fail() {
    assert(m_state == MissionState::Running);

    m_state = MissionState::Failed;
    m_world.missionSystem().recordStateChange(
        m_missionKey,
        voxellancer::protocol::MissionEventState::Failed);
    m_script->onFailure();
    m_script->stop();
}

void Mission::update(float deltaSec) {

}
