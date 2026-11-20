#include "contractlevel.h"

#include <stdexcept>
#include "property/property.h"
#include "protocol/serverprotocol.h"

static ContractLevel loadLevel(const std::string& key, bool peaceful) {
    const int reward = Property<int>(key + "reward");
    ContractLevel level{
        Property<std::string>(key + "name"), Property<std::string>(key + "description"),
        Property<std::string>(key + "enemy"), 0,
        Property<int>(key + "enemiesPerPlayer"), Property<int>(key + "layout"),
        Property<int>(key + "cover")};
    if (reward <= 0 || level.name.empty() || level.description.empty() || level.enemy.empty() ||
        level.enemiesPerPlayer < (peaceful ? 0 : 1) || level.enemiesPerPlayer > 4 ||
        level.layout < 1 || level.layout > 3 || level.cover < 0 || level.cover > 8)
        throw std::invalid_argument("Invalid contract level: " + key);
    level.reward = static_cast<std::uint32_t>(reward);
    return level;
}

ContractLevel contractLevel(std::uint8_t id) {
    if (id < 1 || id > voxellancer::protocol::FlagshipLevel) throw std::invalid_argument("Unknown contract level.");
    return loadLevel("contractlevel" + std::to_string(id) + ".", false);
}

ContractLevel contractSignal(std::uint8_t id) {
    if (id < 1 || id > 3) throw std::invalid_argument("Unknown contract signal.");
    return loadLevel("contractsignal" + std::to_string(id) + ".",
        id == static_cast<std::uint8_t>(voxellancer::protocol::SignalKind::Derelict));
}
