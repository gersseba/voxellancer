#include "internalmissionbindings.h"

#include <glow/logging.h>

#include "missions/mission.h"

#include "scripting/elematelua/luawrapper.h"
#include "scripting/scriptengine.h"
#include "scripting/missionscript.h"

#ifndef VOXELLANCER_HEADLESS_SERVER
#include "ui/hud/hud.h"
#endif

#include "world/world.h"

#ifndef VOXELLANCER_HEADLESS_SERVER
#include "player.h"
#endif


InternalMissionBindings::InternalMissionBindings(MissionScript& script):
    Bindings(script),
    m_missionScript(script)
{

}

void InternalMissionBindings::bind() {
    m_lua.Register("missionSucceed", this, &InternalMissionBindings::apiMissionSucceed);
    m_lua.Register("missionFail", this, &InternalMissionBindings::apiMissionFail);
    m_lua.Register("missionMessage", this, &InternalMissionBindings::apiMissionMessage);
    m_lua.Register("missionFailureMessage", this, &InternalMissionBindings::apiMissionFailureMessage);
    m_lua.Register("missionSuccessMessage", this, &InternalMissionBindings::apiMissionSuccessMessage);
}

int InternalMissionBindings::apiMissionSucceed() {
    if (m_missionScript.mission().state() == MissionState::Running) {
        m_missionScript.mission().succeed();
        return 0;
    } else {
        glow::warning("InternalMissionBindings: Mission not running, can't succeed");
        return -1;
    }
}

int InternalMissionBindings::apiMissionFail() {
    if (m_missionScript.mission().state() == MissionState::Running) {
        m_missionScript.mission().fail();
        return 0;
    } else {
        glow::warning("InternalMissionBindings: Mission not running, can't fail");
        return -1;
    }
}

int InternalMissionBindings::apiMissionMessage(const std::string& message) {
#ifdef VOXELLANCER_HEADLESS_SERVER
    (void)message;
#else
    if (m_scriptEngine.world().hasPresentation()) {
        m_scriptEngine.world().player().hud().showMissionMessage(message);
    }
#endif
    return 0;
}

int InternalMissionBindings::apiMissionFailureMessage(const std::string& message) {
#ifdef VOXELLANCER_HEADLESS_SERVER
    (void)message;
#else
    if (m_scriptEngine.world().hasPresentation()) {
        m_scriptEngine.world().player().hud().showMissionMessage(message);
    }
#endif
    return 0;
}

int InternalMissionBindings::apiMissionSuccessMessage(const std::string& message) {
#ifdef VOXELLANCER_HEADLESS_SERVER
    (void)message;
#else
    if (m_scriptEngine.world().hasPresentation()) {
        m_scriptEngine.world().player().hud().showMissionMessage(message);
    }
#endif
    return 0;
}
