#include "shieldcatalog.h"

#include <array>
#include <cmath>
#include <stdexcept>
#include "property/property.h"

CatalogShield catalogShield(std::uint8_t id) {
    static const std::array<std::string, 4> keys{{
        "thinshield", "thinshieldmk2", "bulwarkshield", "fluxshield"}};
    if (id >= keys.size()) throw std::invalid_argument("Unknown shield catalog ID.");
    const auto& key = keys[id];
    const int price = Property<int>(key + ".shop.price");
    const float capacity = Property<float>(key + ".general.hp");
    const float regeneration = Property<float>(key + ".general.regeneration");
    const float delay = Property<float>(key + ".general.rechargeDelay", 0);
    if (price < 0 || (id != 0 && price == 0) || !std::isfinite(capacity) ||
        capacity <= 0 || !std::isfinite(regeneration) || regeneration <= 0 ||
        !std::isfinite(delay) || delay < 0)
        throw std::invalid_argument("Invalid shield catalog profile: " + key);
    return {id,key,Property<std::string>(key + ".shop.name"),
        Property<std::string>(key + ".shop.role"),static_cast<std::uint32_t>(price),
        capacity,regeneration,delay};
}
