#include "contractlevel.h"

#include <cmath>
#include <sstream>
#include <stdexcept>
#include "property/property.h"
#include "protocol/serverprotocol.h"

static std::vector<procedural::CoverLayout> loadLayouts(const std::string& key) {
    std::vector<procedural::CoverLayout> layouts;
    std::istringstream stream(Property<std::string>::get(key));
    for (std::string name; std::getline(stream, name, ',');) {
        name.erase(0, name.find_first_not_of(" \t"));
        name.erase(name.find_last_not_of(" \t") + 1);
        layouts.push_back(procedural::coverLayoutFromName(name));
    }
    return layouts;
}

static ContractLevel loadLevel(const std::string& key, bool peaceful) {
    const int reward = Property<int>(key + "reward");
    ContractLevel level{
        Property<std::string>(key + "name"), Property<std::string>(key + "description"),
        Property<std::string>(key + "enemy"), 0,
        Property<int>(key + "enemiesPerPlayer"), Property<int>(key + "formation")};
    level.objective = Property<std::string>(key + "objective");
    level.layouts = loadLayouts(key + "layouts");
    if (reward <= 0 || level.name.empty() || level.description.empty() || level.enemy.empty() ||
        level.objective.empty() || level.layouts.empty() ||
        level.enemiesPerPlayer < (peaceful ? 0 : 1) || level.enemiesPerPlayer > 4 ||
        level.formation < 1 || level.formation > 3)
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
    const auto key = "contractsignal" + std::to_string(id) + ".";
    auto level = loadLevel(key,
        id == static_cast<std::uint8_t>(voxellancer::protocol::SignalKind::Derelict));
    if (id == static_cast<std::uint8_t>(voxellancer::protocol::SignalKind::Distress)) {
        const int bonus = Property<int>(key + "rescueBonus");
        if (bonus <= 0) throw std::invalid_argument("Invalid distress rescue bonus: " + key);
        level.rescueBonus = static_cast<std::uint32_t>(bonus);
    }
    return level;
}

float contractEngagementRange() {
    const auto weapon = Property<std::string>::get("contracts.baseWeapon");
    const auto bullet = Property<std::string>::get(weapon + ".general.bullet");
    const float range = Property<float>::get(bullet + ".general.speed") *
        Property<float>::get(bullet + ".general.lifetime") *
        Property<float>::get("contracts.engageRangeFactor");
    if (!std::isfinite(range) || range <= 0.0f)
        throw std::invalid_argument("Invalid contract engagement range.");
    return range;
}
