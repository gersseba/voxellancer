#include "basescenario.h"

#include <stdexcept>

#ifndef VOXELLANCER_HEADLESS_SERVER
#include <glowutils/AutoTimer.h>
#endif

#include "scripting/scriptengine.h"
#include "resource/worldobjectbuilder.h"
#include "world/god.h"
#include "world/world.h"


BaseScenario::BaseScenario(World& world) :
    m_world(&world)
{
}

void BaseScenario::load() {
#ifndef VOXELLANCER_HEADLESS_SERVER
    {
        glowutils::AutoTimer timer("Populating World");
        populateWorld();
        m_world->god().spawn();
        m_world->scriptEngine().start();
    }
#else
    populateWorld();
    m_world->god().spawn();
    m_world->scriptEngine().start();
#endif
}

void BaseScenario::clear() {
    if (m_world != nullptr) {
        m_world->clear();
    }
}

void BaseScenario::reset() {
    clear();
    load();
}

void BaseScenario::populateWorld() {

}

WorldObjectBuilder BaseScenario::worldObjectBuilder(const std::string& name) const {
    if (m_world == nullptr) {
        throw std::logic_error("Cannot build a scenario object before its world is set.");
    }
    return WorldObjectBuilder(name, *m_world);
}
