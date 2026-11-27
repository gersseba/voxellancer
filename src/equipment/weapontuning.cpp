#include "weapontuning.h"

#include <cmath>
#include <stdexcept>

#include "equipment/hardpoint.h"
#include "equipment/weapons/gun.h"
#include "property/property.h"
#include "settings/settingsstore.h"
#include "world/world.h"
#include "worldobject/ship.h"
#include "worldobject/worldobjectcomponents.h"

WeaponTuning::WeaponTuning(const std::string& weapon):
    m_weapon(weapon),
    m_projectile(Property<std::string>(weapon + ".general.bullet").get())
{
    const auto add = [&](const std::string& label, const std::string& key,
                         float minimum, float maximum, bool integer = false) {
        const float initial = integer ? static_cast<float>(Property<int>(key).get())
            : Property<float>(key).get();
        m_fields.push_back({label, minimum, maximum, initial, integer, key});
        m_values.push_back(initial);
    };
    add("VELOCITY U/S", m_projectile + ".general.speed", 1, 2000);
    add("INTERVAL SEC", weapon + ".general.cooldownTime", 0.02f, 5);
    Property<float>(weapon + ".general.energyCost", 0);
    add("ENERGY / TRIGGER", weapon + ".general.energyCost", 0, 100);
    const std::string impact = m_projectile + ".impact.";
    if (Property<bool>(impact + "enabled", false)) {
        Property<float>(impact + "scatter", 0);
        Property<int>(impact + "pellets", 1);
        Property<float>(impact + "radius", 0);
        Property<float>(impact + "propagationAngle", 360);
        Property<float>(impact + "propagationFraction", 1);
        add("RANGE U", impact + "range", 1, 5000);
        add("SHIELD / PROJECTILE", impact + "shield", 0.1f, 500);
        add("BLOCK / PROJECTILE", impact + "normal", 0.1f, 500);
        add("SCATTER DEGREES", impact + "scatter", 0, 5);
        add("PELLETS / TRIGGER", impact + "pellets", 1, 8, true);
        add("PROPAGATION RADIUS", impact + "radius",
            Property<bool>(impact + "isBlast", false) ? 0.1f : 0, 10);
        add("PROPAGATION DEGREES", impact + "propagationAngle", 0, 360);
        add("PROPAGATION FRACTION", impact + "propagationFraction", 0, 1);
    } else {
        add("LIFETIME SEC", m_projectile + ".general.lifetime", 0.02f, 30);
    }
}

float WeaponTuning::value(std::size_t index) const {
    return m_values.at(index);
}

void WeaponTuning::set(std::size_t index, float value) {
    const auto& field = m_fields.at(index);
    field.validate(value);
    m_values[index] = value;
}

void WeaponTuning::reset() {
    for (std::size_t i = 0; i < m_fields.size(); ++i) set(i, m_fields[i].initial);
}

void WeaponTuning::restore(const SettingsStore& settings) {
    auto restored = *this;
    const auto& saved = settings.group("weapon_" + m_weapon);
    for (std::size_t i = 0; i < m_fields.size(); ++i) {
        const auto value = saved.find(m_fields[i].property);
        if (value != saved.end()) restored.set(i,SettingsStore::number(value->second));
    }
    m_values = std::move(restored.m_values);
}

void WeaponTuning::persist(SettingsStore& settings) const {
    SettingsStore::Values values;
    for (std::size_t i = 0; i < m_fields.size(); ++i)
        values[m_fields[i].property] = SettingsStore::number(value(i));
    settings.save({{"weapon_" + m_weapon,std::move(values)}});
}

void WeaponTuning::refresh(World& world) const {
    auto profile = WeaponProfile::load(m_projectile);
    float lifetime = profile.enabled ? value(3)/value(0) : value(3);
    if (profile.enabled) {
        profile.speed = value(0);
        profile.range = value(3);
        profile.shield = value(4);
        profile.normal = profile.armor = value(5);
        profile.scatter = value(6);
        profile.pellets = static_cast<int>(value(7));
        profile.radius = value(8);
        profile.propagationAngle = value(9);
        profile.propagationFraction = value(10);
    }
    for (auto* ship : world.ships()) {
        for (const auto& mount : ship->components().hardpoints()) {
            auto* gun = dynamic_cast<Gun*>(mount->weapon().get());
            if (gun && gun->equipmentKey() == m_weapon) {
                gun->setBulletSpeed(value(0));
                gun->setCooldownTime(value(1));
                gun->setEnergyCost(value(2));
                gun->setProjectileConfiguration(profile, lifetime);
            }
        }
    }
}
