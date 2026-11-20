#pragma once

#include <string>

class World;
class GamePlay;
class WorldObjectBuilder;

class BaseScenario {
public:
    explicit BaseScenario(World& world);
    virtual ~BaseScenario() = default;

    void load();
    void clear();
    void reset();


protected:
    World* m_world;

    virtual void populateWorld();
    WorldObjectBuilder worldObjectBuilder(const std::string& name) const;
};
