#pragma once

#include <cstdint>
#include <string>

struct CatalogShield {
    std::uint8_t id;
    std::string key, name, role;
    std::uint32_t price;
    float capacity, regeneration, rechargeDelay;
};

constexpr std::uint8_t ShieldCatalogSize = 3;
CatalogShield catalogShield(std::uint8_t id);
