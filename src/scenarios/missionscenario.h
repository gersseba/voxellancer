#pragma once

#include <string>
#include <memory>

#include "basescenario.h"


class GamePlay;
class Mission;

class MissionScenario : public BaseScenario {
public:
    MissionScenario(World& world, const std::string& path);


protected:
    std::string m_path;
    std::shared_ptr<Mission> m_mission;

    virtual void populateWorld() override;
};
