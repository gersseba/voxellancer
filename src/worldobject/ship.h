#pragma once

#include <cstdint>
#include <list>
#include <memory>

#include "worldobject.h"
#include "property/property.h"


class World;
class God;
class Hardpoint;
class Engine;
class Character;
class Sound;
class SquadLogic;
class BoardComputer;

/**
 * A Ship is a WorldObject with a Pilot (Character), thus can have an AiTask and a Squad membership
 */
class Ship: public WorldObject {
public:
    static constexpr std::uint32_t AiUpdateIntervalTicks = 6;

    explicit Ship(World& world);
    virtual ~Ship();

    virtual WorldObjectType objectType() const override;

    virtual void update(float deltaSec) override;

    void setTargetObject(WorldObject* target);
    WorldObject* targetObject();

    BoardComputer* boardComputer();
    void setAiUpdatePhase(std::uint32_t phase);
    SquadLogic* squadLogic();

    void setCharacter(Character* character);
    Character* character();


protected:
    std::unique_ptr<Character> m_character;
    std::unique_ptr<BoardComputer> m_boardComputer;
    World* m_world;
    std::uint32_t m_aiUpdatePhase;

    std::unique_ptr<SquadLogic> m_squadLogic;

    Handle<WorldObject> m_targetObjectHandle;


    void updateEnginePosition();
};
