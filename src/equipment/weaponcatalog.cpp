#include "weaponcatalog.h"

#include <array>
#include <cmath>
#include <stdexcept>

#include "property/property.h"

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
    p.mass = Property<float>(root + "mass", 0);
    p.physical = p.mass > 0;
    p.penetration = Property<int>(root + "penetration", 1);
    p.pellets = Property<int>(root + "pellets", 1);
    for (float value : {p.speed, p.range, p.shield, p.armor, p.normal}) {
        if (!std::isfinite(value) || value <= 0) {
            throw std::invalid_argument("Invalid weapon impact profile: " + projectile);
        }
    }
    if (!std::isfinite(p.radius) || p.radius < 0 || p.radius > 10 ||
        !std::isfinite(p.mass) || p.mass < 0 ||
        p.penetration < 1 || p.penetration > 6 ||
        p.pellets < 1 || p.pellets > 8) {
        throw std::invalid_argument("Invalid weapon traversal limits: " + projectile);
    }
    return p;
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
