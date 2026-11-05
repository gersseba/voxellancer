#include "scriptengine.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

#include <glow/logging.h>

#include "scriptable.h"
#include "script.h"


ScriptEngine::ScriptEngine(World& world):
    m_world(world),
    m_nextKey(1),
    m_running(false)
{
}

World& ScriptEngine::world() const {
    return m_world;
}

ScriptEngine::~ScriptEngine() {
    for (auto pair : m_scriptables) {
        pair.second->setScriptKey(Scriptable::INVALID_KEY);
        pair.second->setScriptEngine(nullptr);
    }
    m_scripts.clear(); // delete scripts before scriptengine
}

void ScriptEngine::addScript(std::shared_ptr<Script> script) {
    m_scripts.push_back(script);
    if (m_running) {
        script->start();
    }
}

void ScriptEngine::start() {
    m_running = true;
    for (std::shared_ptr<Script>& script : m_scripts) {
        if (script->state() == ScriptState::Idle) {
            script->start();
        }
    }
}

void ScriptEngine::stop() {
    m_running = false;
}

void ScriptEngine::registerScriptable(Scriptable* scriptable) {
    if (scriptable->m_scriptEngine == this && scriptable->scriptKey() > 0) {
        // This is legit and happens e.g. when an object is created by a script
        return;
    }
    if (scriptable->m_scriptEngine != nullptr ||
        scriptable->scriptKey() != Scriptable::INVALID_KEY) {
        throw std::logic_error("A scriptable cannot be registered with multiple script engines.");
    }

    m_scriptables[m_nextKey] = scriptable;
    scriptable->setScriptEngine(this);
    scriptable->setScriptKey(m_nextKey);

    m_nextKey++;
}

void ScriptEngine::unregisterScriptable(Scriptable* scriptable) {
    if (scriptable->m_scriptEngine != this) {
        if (scriptable->scriptKey() == Scriptable::INVALID_KEY) {
            return;
        }
        throw std::logic_error("A scriptable cannot be unregistered by a different script engine.");
    }
    if (scriptable->scriptKey() > 0) {
        assert(keyValid(scriptable->scriptKey()));

        m_scriptables.erase(scriptable->scriptKey());
        scriptable->setScriptKey(Scriptable::INVALID_KEY);
        scriptable->setScriptEngine(nullptr);
    }
}

bool ScriptEngine::keyValid(int key) const {
    return m_scriptables.find(key) != m_scriptables.end();
}

void ScriptEngine::update(float deltaSec) {
    if (m_running) {
        for (std::list<std::shared_ptr<Script>>::iterator iter = m_scripts.begin(); iter != m_scripts.end();) {
            Script* script = iter->get();

            if (script->state() == ScriptState::Running) {
                script->update(deltaSec);
            }

            if (script->state() == ScriptState::Stopped) {
                iter = m_scripts.erase(iter);
            } else {
                ++iter;
            }
        }
    }
}

Scriptable* ScriptEngine::getScriptable(int key) {
    if (key <= 0 || key >= m_nextKey) {
        glow::warning("ScriptEngine: script-key '%;' is not valid", key);
        return nullptr;
    } else {
        auto pair = m_scriptables.find(key);
        if (pair == m_scriptables.end()) {
            glow::warning("ScriptEngine: the script-key '%;' points to a removed object, next time check with valid()", key);
            return nullptr;
        } else {
            return pair->second;
        }
    }
}
