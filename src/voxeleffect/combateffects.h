#pragma once

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include "equipment/shieldtype.h"
#include "geometry/transform.h"
#include "protocol/serverprotocol.h"
#include "settings/configurationfield.h"

class CombatEffectSettings {
public:
    static constexpr std::size_t GunCount = 10, HullShield = 10, SphereShield = 11,
        Global = 12, GroupCount = 13;
    enum GunField { MuzzleSize, MuzzleTime, MuzzleLight, ProjectileSize, ProjectileLength,
        TrailLength, TrailTime, TrailSegments, ImpactSize, ImpactTime, SparkCount, Hue, RecoilDistance };
    enum ShieldField { IdleLight, CellSize, SweepPeriod, SweepLight, HitLight, HitRadius,
        HitTime, BreakLight, BreakTime, RechargeLight, RechargeTime, ShieldHue };
    enum GlobalField { Brightness, ViewDistance, BoxBudget, CameraKick };
    using Field = ConfigurationField;
    CombatEffectSettings();
    const std::string& name(std::size_t group) const;
    const std::vector<Field>& fields(std::size_t group) const;
    float value(std::size_t group, std::size_t field) const;
    void set(std::size_t group, std::size_t field, float value);
    void reset(std::size_t group);
    void restore(const class SettingsStore& settings);
    void persist(class SettingsStore& settings, std::size_t group) const;
    static std::string key(std::size_t group);
    static std::optional<std::size_t> gun(const std::string& key);
    static glm::vec3 color(float hue);

private:
    std::array<std::string, GroupCount> m_names;
    std::array<std::vector<Field>, GroupCount> m_fields;
    std::array<std::vector<float>, GroupCount> m_values;
};

struct CombatEffectBox {
    glm::vec3 position{0}, extent{1};
    glm::quat orientation{1,0,0,0};
    glm::vec4 color{1};
};

struct ShieldEffectPoint {
    glm::vec3 position, normal;
    float size = 1;
};

struct ShieldEffectBody {
    voxellancer::protocol::EntityId id;
    Transform transform;
    ShieldType type = ShieldType::None;
    float fraction = 0;
    glm::vec3 center{0};
    float radius = 1;
    std::shared_ptr<const std::vector<ShieldEffectPoint>> surface;
};

class CombatEffects {
public:
    CombatEffectSettings settings;
    void clear();
    void advance(float deltaSec);
    void fire(std::size_t gun, glm::vec3 position, glm::vec3 direction, float scale = 1,
        std::optional<voxellancer::protocol::EntityId> owner = {});
    void impact(std::size_t gun, glm::vec3 position, glm::vec3 normal, float scale = 1);
    void kick(float strength);
    float cameraKick() const;
    glm::vec3 recoil(voxellancer::protocol::EntityId id) const;
    void shieldHit(voxellancer::protocol::EntityId id, glm::vec3 localPosition, float damage);
    void shields(const std::vector<ShieldEffectBody>& bodies);
    void beginProjectiles();
    void projectile(voxellancer::protocol::EntityId id, std::size_t gun, glm::vec3 position,
        glm::vec3 direction);
    void endProjectiles();
    const std::vector<CombatEffectBox>& build(glm::vec3 viewer, bool includeShieldIdle = true);
    const std::vector<CombatEffectBox>& boxes() const { return m_boxes; }
    std::vector<CombatEffectBox> preview(std::size_t group, float time) const;
    static ShieldEffectBody previewShield(std::size_t group);
    static std::shared_ptr<const std::vector<ShieldEffectPoint>> sphereSurface(
        glm::vec3 center, float radius);

private:
    struct Burst {
        std::size_t gun;
        glm::vec3 position, direction;
        float scale, age = 0;
        bool impact;
    };
    struct ShieldPulse {
        voxellancer::protocol::EntityId id;
        glm::vec3 position;
        float strength, age = 0;
        enum Kind { Hit, Break, Recharge } kind = Hit;
    };
    struct ShieldState {
        ShieldEffectBody body;
        bool rising = false;
    };
    struct Sample { glm::vec3 position; float time; };
    struct Trail {
        std::size_t gun = 0;
        glm::vec3 direction{0,0,-1};
        std::deque<Sample> samples;
        bool seen = false;
    };
    struct Recoil {
        glm::vec3 direction;
        float distance, age;
    };
    float m_time = 0;
    float m_kick = 0;
    std::vector<Burst> m_bursts;
    std::vector<ShieldPulse> m_pulses;
    std::unordered_map<std::uint64_t, ShieldState> m_shields;
    std::unordered_map<std::uint64_t, Trail> m_trails;
    std::unordered_map<std::uint64_t, Recoil> m_recoils;
    std::vector<CombatEffectBox> m_boxes;
    void box(glm::vec3 position, glm::vec3 extent, glm::vec3 color, float alpha,
        glm::quat orientation = {1,0,0,0});
    void drawBurst(const Burst& burst);
    void drawProjectile(std::size_t gun, glm::vec3 position, glm::vec3 direction, float age = 0);
    void drawShield(const ShieldEffectBody& body, glm::vec3 viewer, bool includeIdle);
};
