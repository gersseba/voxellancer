#pragma once

#include <cstdint>
#include <string>

#include <glm/glm.hpp>

struct WeaponProfile {
    bool enabled = false;
    bool physical = false;
    bool isBlast = false;
    float speed = 0;
    float range = 0;
    float shield = 0;
    float armor = 0;
    float normal = 0;
    float radius = 0;
    float propagationAngle = 360;
    float propagationFraction = 1;
    float mass = 0;
    int penetration = 1;
    int pellets = 1;
    float scatter = 0; // half-angle of the per-shot aim cone, degrees

    static WeaponProfile load(const std::string& projectile);
};

struct CatalogWeapon {
    std::uint8_t id;
    std::string key;
    std::string name;
    std::string role;
    std::uint32_t price;
    WeaponProfile profile;
    float energyCost = 0;
};

constexpr std::uint8_t WeaponCatalogSize = 6;
constexpr float PayloadRegenBonusPerStack = 0.15f;
constexpr float ShieldedEnergyDiscountPerStack = 0.10f;
constexpr float MaxShieldedEnergyDiscount = 0.40f;
glm::vec3 scatterDirection(const glm::vec3& forward, float halfAngleDegrees,
    std::uint32_t seed);
CatalogWeapon catalogWeapon(std::uint8_t id);
std::uint8_t catalogWeaponId(const std::string& key);
