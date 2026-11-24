#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "procedural/coverlayout.h"

struct ContractLevel {
    std::string name, description, enemy;
    std::uint32_t reward;
    // formation selects the enemy spawn pattern (1-3).
    int enemiesPerPlayer, formation;
    std::uint32_t rescueBonus = 0;
    std::string objective;
    // Cover layouts that fit this encounter; one is picked at random per sortie.
    std::vector<procedural::CoverLayout> layouts;
};

inline constexpr char DistressWingmanDefinition[] = "contractwingman";

ContractLevel contractLevel(std::uint8_t id);
ContractLevel contractSignal(std::uint8_t id);
// Distance at which dormant contract ships engage: engageRangeFactor x base weapon range.
float contractEngagementRange();
