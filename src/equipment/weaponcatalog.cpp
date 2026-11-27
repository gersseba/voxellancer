#include "weaponcatalog.h"

#include <array>
#include <cmath>
#include <stdexcept>

#include "property/property.h"
#include "utils/safenormalize.h"

namespace {
    const std::array<std::string, WeaponCatalogSize> Keys{{
        "pulselaser", "pulselasermk2", "heavyautocannon",
        "kineticrailgun", "plasmamortar", "scattercannon"
    }};
}

WeaponProfile WeaponProfile::load(const std::string& projectile) {
    WeaponProfile p;
    const std::string root = projectile + ".impact.";
    p.enabled = Property<bool>(root + "enabled", false);
    if (!p.enabled) return p;
    p.speed = Property<float>(projectile + ".general.speed");
    p.range = Property<float>(root + "range");
    p.shield = Property<float>(root + "shield");
    p.armor = Property<float>(root + "armor");
    p.normal = Property<float>(root + "normal");
    p.radius = Property<float>(root + "radius", 0);
    p.isBlast = Property<bool>(root + "isBlast", false);
    p.propagationAngle = Property<float>(root + "propagationAngle", 360);
    p.propagationFraction = Property<float>(root + "propagationFraction", 1);
    p.mass = Property<float>(root + "mass", 0);
    p.physical = p.mass > 0;
    p.pellets = Property<int>(root + "pellets", 1);
    p.scatter = Property<float>(root + "scatter", 0);
    for (float value : {p.speed, p.range, p.shield, p.armor, p.normal}) {
        if (!std::isfinite(value) || value <= 0) {
            throw std::invalid_argument("Invalid weapon impact profile: " + projectile);
        }
    }
    if (!std::isfinite(p.radius) || p.radius < 0 || p.radius > 10 ||
        (p.isBlast && p.radius <= 0) ||
        !std::isfinite(p.propagationAngle) || p.propagationAngle < 0 ||
        p.propagationAngle > 360 ||
        !std::isfinite(p.propagationFraction) || p.propagationFraction < 0 ||
        p.propagationFraction > 1 ||
        !std::isfinite(p.mass) || p.mass < 0 ||
        p.pellets < 1 || p.pellets > 8 ||
        !std::isfinite(p.scatter) || p.scatter < 0 || p.scatter > 5) {
        throw std::invalid_argument("Invalid weapon traversal limits: " + projectile);
    }
    return p;
}

namespace {
    std::uint32_t mixBits(std::uint32_t x) {
        x ^= x >> 16; x *= 0x7feb352dU;
        x ^= x >> 15; x *= 0x846ca68bU;
        return x ^ (x >> 16);
    }

    float unitFloat(std::uint32_t bits) {
        return static_cast<float>(bits >> 8) / static_cast<float>(1u << 24);
    }
}

glm::vec3 scatterDirection(const glm::vec3& forward, float halfAngleDegrees,
    std::uint32_t seed) {
    const glm::vec3 axis = safeNormalize(forward, glm::vec3(0, 0, -1));
    if (halfAngleDegrees <= 0) return axis;
    const glm::vec3 helper = std::abs(axis.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
    const glm::vec3 right = glm::normalize(glm::cross(axis, helper));
    const glm::vec3 up = glm::cross(right, axis);
    const std::uint32_t a = mixBits(seed * 2u + 1u);
    const std::uint32_t b = mixBits(a ^ 0x9e3779b9U);
    // sqrt keeps hits uniformly distributed over the cone's cross-section
    const float offset = std::tan(glm::radians(halfAngleDegrees)) * std::sqrt(unitFloat(a));
    const float angle = 6.28318530718f * unitFloat(b);
    return glm::normalize(axis + (right * std::cos(angle) + up * std::sin(angle)) * offset);
}

CatalogWeapon catalogWeapon(std::uint8_t id) {
    if (id == 0 || id > Keys.size()) {
        throw std::invalid_argument("Unknown weapon catalog ID.");
    }
    const auto& key = Keys[id - 1];
    const int price = Property<int>(key + ".shop.price");
    const float cost = Property<float>(key + ".general.energyCost");
    if (!std::isfinite(cost) || cost <= 0)
        throw std::invalid_argument("Invalid catalog weapon energy cost: " + key);
    if (price <= 0) throw std::invalid_argument("Invalid weapon shop price: " + key);
    return {id, key, Property<std::string>(key + ".shop.name"),
        Property<std::string>(key + ".shop.role"), static_cast<std::uint32_t>(price),
        WeaponProfile::load(Property<std::string>(key + ".general.bullet")), cost};
}

std::uint8_t catalogWeaponId(const std::string& key) {
    for (std::uint8_t i = 0; i < Keys.size(); ++i) {
        if (Keys[i] == key) return i + 1;
    }
    return 0;
}
