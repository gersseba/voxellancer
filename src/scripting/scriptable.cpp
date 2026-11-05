#include "scriptable.h"

#include <cassert>

#include "scriptengine.h"


Scriptable::Scriptable():
    m_key(INVALID_KEY),
    m_local(false),
    m_scriptEngine(nullptr)
{

}

Scriptable::~Scriptable() {
    if (m_scriptEngine != nullptr && m_key != INVALID_KEY) {
        m_scriptEngine->unregisterScriptable(this);
    }
}

int Scriptable::scriptKey() const {
    return m_key;
}

void Scriptable::setScriptKey(int key) {
    assert(m_key == INVALID_KEY || key == INVALID_KEY);
    m_key = key;
}

void Scriptable::setScriptEngine(ScriptEngine* scriptEngine) {
    assert(m_scriptEngine == nullptr || scriptEngine == nullptr ||
        m_scriptEngine == scriptEngine);
    m_scriptEngine = scriptEngine;
}

bool Scriptable::isScriptLocal() const {
    return m_local;
}

void Scriptable::setScriptLocal(bool local) {
    m_local = local;
}
