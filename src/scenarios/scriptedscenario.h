#pragma once

#include <memory>
#include <string>

#include "scripting/script.h"

#include "basescenario.h"


class World;
class GamePlay;
class GamePlayScript;

class ScriptedScenario: public BaseScenario {
public:
    ScriptedScenario(World& world, const std::string& path);
    virtual ~ScriptedScenario();


protected:
    std::string m_path;
    std::shared_ptr<GamePlayScript> m_script;

    virtual void populateWorld() override;
};
