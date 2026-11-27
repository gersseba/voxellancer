#pragma once

#include <string>
#include <vector>
#include "settings/configurationfield.h"

class World;
class SettingsStore;

class WeaponTuning {
public:
    using Field = ConfigurationField;
    explicit WeaponTuning(const std::string& weapon);
    const std::vector<Field>& fields() const { return m_fields; }
    float value(std::size_t index) const;
    void set(std::size_t index, float value);
    void reset();
    void refresh(World& world) const;
    void restore(const SettingsStore& settings);
    void persist(SettingsStore& settings) const;

private:
    std::string m_weapon, m_projectile;
    std::vector<Field> m_fields;
    std::vector<float> m_values;
};
