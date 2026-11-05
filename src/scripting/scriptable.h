#pragma once

class ScriptEngine;

class Scriptable {
public:
    Scriptable();
    virtual ~Scriptable();

    int scriptKey() const;
    void setScriptKey(int key);

    bool isScriptLocal() const;
    void setScriptLocal(bool local);

    static const int INVALID_KEY = -1;


protected:
    friend class ScriptEngine;

    void setScriptEngine(ScriptEngine* scriptEngine);

    int m_key;
    bool m_local;
    ScriptEngine* m_scriptEngine;
};
