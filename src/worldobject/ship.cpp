#include "ship.h"

#include "ai/boardcomputer.h"
#include "ai/character.h"
#include "ai/squadlogic.h"

#include "factions/factionmatrix.h"

#include "worldobject/worldobjectinfo.h"

#include "world/world.h"


Ship::Ship(World& world):
    WorldObject(),
    m_character(new Character(*this, world.factionMatrix().unknownFaction())),
    m_boardComputer(new BoardComputer(this, world)),
    m_world(&world),
    m_squadLogic(new SquadLogic(*this)),
    m_aiUpdatePhase(0),
    m_shipType(ShipType::Fighter)
{
    setWorld(world);
    m_info->setShowOnHud(true);
    m_info->setCanLockOn(true);
}

Ship::~Ship() = default;

WorldObjectType Ship::objectType() const {
    return WorldObjectType::Ship;
}

void Ship::update(float deltaSec) {
    WorldObject::update(deltaSec);

    if (m_world->simulationTickIndex() % AiUpdateIntervalTicks ==
        m_aiUpdatePhase) {
        m_character->update(deltaSec * AiUpdateIntervalTicks);
    }
    m_boardComputer->update(deltaSec);
}

void Ship::setTargetObject(WorldObject* target) {
    m_targetObjectHandle = target ? makeHandle(target) : Handle<WorldObject>();
}

WorldObject* Ship::targetObject() {
    return m_targetObjectHandle.get();
}

void Ship::setCharacter(Character* character) {
    m_character.reset(character);
}

Character* Ship::character() {
    return m_character.get();
}

BoardComputer* Ship::boardComputer() {
    return m_boardComputer.get();
}

void Ship::setAiUpdatePhase(std::uint32_t phase) {
    m_aiUpdatePhase = phase % AiUpdateIntervalTicks;
}

SquadLogic* Ship::squadLogic() {
    return m_squadLogic.get();
}

ShipType Ship::shipType() const {
    return m_shipType;
}

void Ship::setShipType(ShipType shipType) {
    m_shipType = shipType;
}
