#pragma once

#include <cstdint>
#include <string>

struct ContractLevel {
    std::string name, description, enemy;
    std::uint32_t reward;
    int enemiesPerPlayer, layout, cover;
};

ContractLevel contractLevel(std::uint8_t id);
ContractLevel contractSignal(std::uint8_t id);
