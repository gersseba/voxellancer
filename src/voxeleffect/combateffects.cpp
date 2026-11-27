#include "combateffects.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/quaternion.hpp>

#include "utils/safenormalize.h"
#include "settings/settingsstore.h"

namespace {
    constexpr std::size_t MaxBursts = 256, MaxPulses = 64, MaxTrails = 512;
    std::uint64_t key(voxellancer::protocol::EntityId id) {
        return (static_cast<std::uint64_t>(id.generation) << 32) | id.index;
    }
    const std::array<std::string, 10> Guns{{
        "pulselaser", "pulselasermk2", "heavyautocannon", "kineticrailgun",
        "plasmamortar", "scattercannon", "gun", "ioncannon", "railgun", "snowcanon"
    }};
    const std::array<std::string, 10> Projectiles{{
        "pulselaser_bullet", "pulselasermk2_bullet", "heavyautocannon_bullet",
        "kineticrailgun_bullet", "plasmamortar_bullet", "scattercannon_bullet",
        "gunbullet", "ionbullet", "railgunbullet", "snowball"
    }};
    bool rail(std::size_t gun) { return gun == 3 || gun == 8; }
    bool plasma(std::size_t gun) { return gun == 4 || gun == 7; }
    bool kinetic(std::size_t gun) { return gun == 2 || gun == 5 || gun == 6; }
    glm::quat facing(glm::vec3 direction) {
        return glm::rotation(glm::vec3(0,0,1), safeNormalize(direction, glm::vec3(0,0,1)));
    }
}

CombatEffectSettings::CombatEffectSettings() {
    m_names = {{"Pulse Repeater I", "Pulse Repeater II", "Heavy Autocannon", "Railgun",
        "Plasma Mortar", "Scatter Cannon", "Legacy Gun", "Ion Cannon", "Legacy Railgun",
        "Snow Cannon", "Hull shield", "Spherical shield", "Global effects"}};
    const float hues[]{188,196,32,265,285,42,30,210,265,180};
    for (std::size_t g = 0; g < GunCount; ++g) {
        m_fields[g] = {
            {"MUZZLE SIZE",0,3,rail(g) ? 0.5f : 0.8f,false,"muzzleSize"},
            {"MUZZLE SECONDS",0.02f,0.3f,kinetic(g) || plasma(g) ? 0.13f : 0.08f,false,"muzzleTime"},
            {"MUZZLE BRIGHTNESS",0,2,0.9f,false,"muzzleLight"},
            {"PROJECTILE WIDTH",0,2,g == 5 ? 0.10f : plasma(g) ? 0.45f : 0.18f,false,"projectileSize"},
            {"PROJECTILE LENGTH",0.1f,12,rail(g) ? 5.0f : plasma(g) ? 0.8f : 1.8f,false,"projectileLength"},
            {"TRAIL LENGTH U",0,100,rail(g) ? 32.0f : plasma(g) ? 8.0f : 10.0f,false,"trailLength"},
            {"TRAIL SECONDS",0.01f,0.5f,rail(g) ? 0.16f : 0.08f,false,"trailTime"},
            {"TRAIL SEGMENTS",1,32,rail(g) ? 16.0f : 4.0f,true,"trailSegments"},
            {"IMPACT SIZE",0,5,plasma(g) ? 1.7f : kinetic(g) ? 1.1f : 0.6f,false,"impactSize"},
            {"IMPACT SECONDS",0.03f,1,plasma(g) ? 0.5f : 0.4f,false,"impactTime"},
            {"IMPACT SPARKS",0,24,plasma(g) ? 16.0f : kinetic(g) ? 12.0f : 7.0f,true,"sparkCount"},
            {"COLOR HUE",0,360,hues[g],false,"hue"},
            {"RECOIL DISTANCE",0,1,kinetic(g) || rail(g) ? 0.20f : 0.08f,false,"recoilDistance"}
        };
    }
    for (const auto g : {HullShield, SphereShield}) {
        m_fields[g] = {
            {"IDLE BRIGHTNESS",0,0.5f,0.025f,false,"idleLight"},
            {"CELL SIZE",0.2f,2,0.85f,false,"cellSize"},
            {"SWEEP PERIOD SEC",1,15,6,false,"sweepPeriod"},
            {"SWEEP BRIGHTNESS",0,1,0.10f,false,"sweepLight"},
            {"HIT BRIGHTNESS",0,2,0.85f,false,"hitLight"},
            {"HIT RADIUS BLOCKS",0.5f,15,4,false,"hitRadius"},
            {"HIT SECONDS",0.1f,1.5f,0.4f,false,"hitTime"},
            {"BREAK BRIGHTNESS",0,2,0.8f,false,"breakLight"},
            {"BREAK SECONDS",0.1f,2,0.65f,false,"breakTime"},
            {"RECHARGE BRIGHTNESS",0,1,0.18f,false,"rechargeLight"},
            {"RECHARGE SECONDS",0.1f,3,1.2f,false,"rechargeTime"},
            {"COLOR HUE",0,360,195,false,"hue"}
        };
    }
    m_fields[Global] = {{"MASTER BRIGHTNESS",0,2,1,false,"brightness"},
        {"VIEW DISTANCE U",100,3000,1600,false,"viewDistance"},
        {"BOX BUDGET",256,8192,4096,true,"boxBudget"},
        {"CAMERA KICK",0,1,0.35f,false,"cameraKick"}};
    for (std::size_t g = 0; g < GroupCount; ++g)
        for (std::size_t i = 0; i < m_fields[g].size(); ++i) {
            m_values[g].push_back(m_fields[g][i].initial);
        }
}

const std::string& CombatEffectSettings::name(std::size_t group) const { return m_names.at(group); }
const std::vector<CombatEffectSettings::Field>& CombatEffectSettings::fields(std::size_t group) const {
    return m_fields.at(group);
}
float CombatEffectSettings::value(std::size_t group, std::size_t field) const {
    return m_values.at(group).at(field);
}
void CombatEffectSettings::set(std::size_t group, std::size_t index, float value) {
    const auto& field = m_fields.at(group).at(index);
    field.validate(value);
    m_values.at(group).at(index) = value;
}
void CombatEffectSettings::reset(std::size_t group) {
    for (std::size_t i = 0; i < fields(group).size(); ++i)
        set(group, i, fields(group)[i].initial);
}
std::string CombatEffectSettings::key(std::size_t group) {
    if (group < GunCount) return Guns.at(group);
    if (group == HullShield) return "hullshield";
    if (group == SphereShield) return "sphereshield";
    if (group == Global) return "global";
    throw std::out_of_range("Unknown combat effect group.");
}
void CombatEffectSettings::restore(const SettingsStore& settings) {
    auto restored = *this;
    for (std::size_t group = 0; group < GroupCount; ++group) {
        const auto& saved = settings.group("effects_" + key(group));
        for (std::size_t field = 0; field < fields(group).size(); ++field) {
            const auto value = saved.find(fields(group)[field].property);
            if (value != saved.end()) restored.set(group,field,SettingsStore::number(value->second));
        }
    }
    m_values = std::move(restored.m_values);
}
void CombatEffectSettings::persist(SettingsStore& settings, std::size_t group) const {
    SettingsStore::Values values;
    for (std::size_t field = 0; field < fields(group).size(); ++field)
        values[fields(group)[field].property] = SettingsStore::number(value(group,field));
    settings.save({{"effects_" + key(group),std::move(values)}});
}
std::optional<std::size_t> CombatEffectSettings::gun(const std::string& name) {
    for (std::size_t i = 0; i < Guns.size(); ++i)
        if (name == Guns[i] || name == Projectiles[i]) return i;
    return {};
}
glm::vec3 CombatEffectSettings::color(float hue) {
    const glm::vec3 p = glm::abs(glm::fract(glm::vec3(hue / 360) +
        glm::vec3(0,2.0f/3,1.0f/3)) * 6.0f - 3.0f);
    return glm::mix(glm::vec3(1), glm::clamp(p-1.0f,0.0f,1.0f),0.7f);
}

void CombatEffects::clear() {
    m_bursts.clear(); m_pulses.clear(); m_shields.clear(); m_trails.clear(); m_recoils.clear(); m_boxes.clear();
    m_time = 0;
    m_kick = 0;
}
void CombatEffects::advance(float deltaSec) {
    if (!std::isfinite(deltaSec) || deltaSec < 0)
        throw std::invalid_argument("Combat effect delta must be finite and nonnegative.");
    m_time += deltaSec;
    m_kick = std::max(0.0f,m_kick-deltaSec*5.0f);
    for (auto it = m_recoils.begin(); it != m_recoils.end();) {
        it->second.age += deltaSec;
        if (it->second.age >= 0.2f) it = m_recoils.erase(it);
        else ++it;
    }
    for (auto& burst : m_bursts) burst.age += deltaSec;
    m_bursts.erase(std::remove_if(m_bursts.begin(),m_bursts.end(),[&](const auto& burst) {
        return burst.age > settings.value(burst.gun, burst.impact ?
            CombatEffectSettings::ImpactTime : CombatEffectSettings::MuzzleTime);
    }),m_bursts.end());
    for (auto& pulse : m_pulses) pulse.age += deltaSec;
    m_pulses.erase(std::remove_if(m_pulses.begin(),m_pulses.end(),[](const auto& pulse) {
        return pulse.age > 3;
    }),m_pulses.end());
}
void CombatEffects::fire(std::size_t gun, glm::vec3 position, glm::vec3 direction, float scale,
    std::optional<voxellancer::protocol::EntityId> owner) {
    settings.fields(gun);
    direction = safeNormalize(direction,glm::vec3(0,0,-1));
    if (m_bursts.size() < MaxBursts)
        m_bursts.push_back({gun,position,direction,scale,0,false});
    if (owner && (m_recoils.count(key(*owner)) || m_recoils.size() < MaxBursts))
        m_recoils[key(*owner)] = {direction,settings.value(gun,CombatEffectSettings::RecoilDistance)*scale,0};
}
void CombatEffects::kick(float strength) {
    if (!std::isfinite(strength) || strength < 0)
        throw std::invalid_argument("Camera kick strength must be finite and nonnegative.");
    m_kick = std::min(1.0f,m_kick+strength);
}
float CombatEffects::cameraKick() const {
    return m_kick*settings.value(CombatEffectSettings::Global,CombatEffectSettings::CameraKick);
}
glm::vec3 CombatEffects::recoil(voxellancer::protocol::EntityId id) const {
    const auto it = m_recoils.find(key(id));
    if (it == m_recoils.end()) return glm::vec3(0);
    return -it->second.direction*it->second.distance*(1-it->second.age/0.2f);
}
void CombatEffects::impact(std::size_t gun, glm::vec3 position, glm::vec3 normal, float scale) {
    settings.fields(gun);
    if (m_bursts.size() < MaxBursts)
        m_bursts.push_back({gun,position,safeNormalize(normal,glm::vec3(0,1,0)),scale,0,true});
}
void CombatEffects::shieldHit(voxellancer::protocol::EntityId id, glm::vec3 position, float damage) {
    for (auto& pulse : m_pulses) {
        if (pulse.id == id && pulse.kind == ShieldPulse::Hit && pulse.age < 0.06f &&
            glm::distance(pulse.position,position) < 2) {
            pulse.strength = std::min(2.0f,pulse.strength + damage / 20);
            return;
        }
    }
    if (m_pulses.size() < MaxPulses)
        m_pulses.push_back({id,position,glm::clamp(damage/20,0.5f,2.0f),0,ShieldPulse::Hit});
}
void CombatEffects::shields(const std::vector<ShieldEffectBody>& bodies) {
    std::unordered_map<std::uint64_t, ShieldState> next;
    for (const auto& body : bodies) {
        if (body.type == ShieldType::None) continue;
        bool rising = false;
        const auto previous = m_shields.find(key(body.id));
        if (previous != m_shields.end()) {
            const float old = previous->second.body.fraction;
            rising = body.fraction > old + 0.00001f;
            if (body.fraction <= 0 && old > 0) {
                if (m_pulses.size() >= MaxPulses) {
                    const auto hit = std::find_if(m_pulses.begin(),m_pulses.end(),
                        [](const auto& pulse) { return pulse.kind == ShieldPulse::Hit; });
                    if (hit != m_pulses.end()) m_pulses.erase(hit);
                }
                if (m_pulses.size() < MaxPulses)
                    m_pulses.push_back({body.id,{},1,0,ShieldPulse::Break});
            } else if (rising && !previous->second.rising && m_pulses.size() < MaxPulses) {
                m_pulses.push_back({body.id,{},1,0,ShieldPulse::Recharge});
            }
        }
        next.emplace(key(body.id),ShieldState{body,rising});
    }
    m_shields.swap(next);
}
void CombatEffects::beginProjectiles() {
    for (auto& entry : m_trails) entry.second.seen = false;
}
void CombatEffects::projectile(voxellancer::protocol::EntityId id, std::size_t gun,
    glm::vec3 position, glm::vec3 direction) {
    if (!m_trails.count(key(id)) && m_trails.size() >= MaxTrails) return;
    auto& trail = m_trails[key(id)];
    trail.gun = gun;
    trail.direction = safeNormalize(direction,glm::vec3(0,0,-1));
    trail.seen = true;
    if (trail.samples.empty() || glm::distance(trail.samples.back().position,position) > 0.001f)
        trail.samples.push_back({position,m_time});
    const float duration = settings.value(gun,CombatEffectSettings::TrailTime);
    while (trail.samples.size() > 2 &&
        (trail.samples[1].time < m_time-duration || trail.samples.size() > 32))
        trail.samples.pop_front();
}
void CombatEffects::endProjectiles() {
    for (auto it = m_trails.begin(); it != m_trails.end();) {
        const auto& trail = it->second;
        if (!trail.seen && (trail.samples.empty() || m_time-trail.samples.back().time >
            settings.value(trail.gun,CombatEffectSettings::TrailTime)))
            it = m_trails.erase(it);
        else ++it;
    }
}
void CombatEffects::box(glm::vec3 position, glm::vec3 extent, glm::vec3 color, float alpha,
    glm::quat orientation) {
    alpha *= settings.value(CombatEffectSettings::Global,CombatEffectSettings::Brightness);
    if (alpha <= 0.002f || glm::any(glm::lessThanEqual(extent,glm::vec3(0))) ||
        m_boxes.size() >= static_cast<std::size_t>(
            settings.value(CombatEffectSettings::Global,CombatEffectSettings::BoxBudget))) return;
    m_boxes.push_back({position,extent,orientation,{color,glm::clamp(alpha,0.0f,1.0f)}});
}
void CombatEffects::drawBurst(const Burst& b) {
    using S = CombatEffectSettings;
    const auto v = [&](int field) { return settings.value(b.gun,field); };
    const float duration = v(b.impact ? S::ImpactTime : S::MuzzleTime);
    const float t = glm::clamp(b.age/duration,0.0f,1.0f);
    const float size = v(b.impact ? S::ImpactSize : S::MuzzleSize)*b.scale;
    const auto color = S::color(v(S::Hue));
    const auto q = facing(b.direction);
    const float light = (1-t)*(b.impact ? 0.9f : v(S::MuzzleLight));
    if (!b.impact) {
        const float flash = glm::clamp(1-b.age/0.05f,0.0f,1.0f);
        box(b.position,{size*0.35f,size*0.35f,size*(0.6f+t)},glm::vec3(1),
            v(S::MuzzleLight)*flash,q);
        if (plasma(b.gun) || b.gun == 9) {
            for (int i = 0; i < 5; ++i) {
                const float angle = i*glm::two_pi<float>()/5;
                const auto offset = q*glm::vec3(std::cos(angle),std::sin(angle),0.5f)*size*(0.5f+t);
                box(b.position+offset,glm::vec3(size*0.3f*(1-t)),color,light,q);
            }
        } else if (rail(b.gun)) {
            for (float side : {-1.0f,1.0f}) {
                box(b.position+q*glm::vec3(side*size*0.5f,0,0),
                    {size*0.12f,size,size*0.12f},color,light,q);
                box(b.position+q*glm::vec3(0,side*size*0.5f,0),
                    {size,size*0.12f,size*0.12f},color,light,q);
            }
        } else if (kinetic(b.gun)) {
            const int count = b.gun == 5 ? 7 : 5;
            for (int i = 0; i < count; ++i) {
                const float angle = glm::two_pi<float>()*i/count;
                const auto direction = b.gun == 5 ? glm::vec3((i-3)*0.28f,0,0.7f) :
                    glm::vec3(std::cos(angle)*0.5f,std::sin(angle)*0.5f,0.5f);
                box(b.position+q*direction*size*(0.5f+t*2),
                    {size*0.16f,size*0.16f,size*(0.4f+0.1f*(i%2))},color,light,q);
            }
        } else {
            box(b.position,{size*1.6f,size*0.18f,size*0.18f},color,light,q);
            box(b.position,{size*0.18f,size*1.6f,size*0.18f},color,light,q);
            if (b.gun == 1)
                box(b.position-b.direction*size*0.3f,{size*1.9f,size*0.12f,size*0.12f},color,light*0.6f,q);
        }
        return;
    }
    const float flash = glm::clamp(1-b.age/0.06f,0.0f,1.0f);
    box(b.position,glm::vec3(size*(0.45f+flash*0.4f)),glm::vec3(1),flash);
    const int count = static_cast<int>(v(S::SparkCount));
    for (int i = 0; i < count; ++i) {
        const float angle = glm::two_pi<float>()*i/std::max(count,1) + (i%3)*0.13f;
        const glm::vec3 radial = q*glm::vec3(std::cos(angle),std::sin(angle),0);
        const float variation = 0.65f + (i%5)*0.16f;
        const float travel = size*(0.15f+t*(plasma(b.gun) ? 2.8f : 3.2f)*variation);
        const float depth = plasma(b.gun) ? (0.25f+(i%4)*0.28f) : (0.7f+(i%3)*0.4f);
        const float fragment = size*(plasma(b.gun) ? 0.28f : (i%3 == 0 ? 0.25f : 0.14f));
        const auto fragmentColor = i%3 == 0 ? color*0.55f : color;
        box(b.position+radial*travel+b.direction*size*t*depth,
            glm::vec3(fragment*(1-t*0.65f)),fragmentColor,light);
        if (plasma(b.gun) && i%2 == 0) {
            box(b.position+radial*travel*0.65f+b.direction*size*t*(depth+0.6f),
                glm::vec3(fragment*0.75f*(1-t)),color,light*0.65f);
        }
    }
}
void CombatEffects::drawProjectile(std::size_t gun, glm::vec3 position, glm::vec3 direction, float age) {
    using S = CombatEffectSettings;
    const float width = settings.value(gun,S::ProjectileSize);
    const float length = settings.value(gun,S::ProjectileLength);
    const auto color = S::color(settings.value(gun,S::Hue));
    const auto q = facing(direction);
    box(position,{width,width,length},kinetic(gun) ? color*0.3f : glm::vec3(0.9f),0.9f,q);
    box(position+direction*length*0.3f,{width*1.4f,width*1.4f,length*0.2f},color,0.95f,q);
    if (gun == 1) {
        for (float side : {-1.0f,1.0f})
            box(position+q*glm::vec3(side*width*1.5f,0,0),
                {width*0.4f,width*0.4f,length},color,0.7f,q);
    }
    if (plasma(gun) || gun == 9) {
        for (int i = 0; i < 4; ++i) {
            const float angle = age*8+i*glm::half_pi<float>();
            const auto offset = q*glm::vec3(std::cos(angle)*width*1.3f,
                std::sin(angle)*width*1.3f,0);
            box(position+offset,glm::vec3(width*0.55f),color,0.7f,
                glm::angleAxis(angle,glm::normalize(glm::vec3(1,1,0))));
        }
    }
}

std::shared_ptr<const std::vector<ShieldEffectPoint>> CombatEffects::sphereSurface(
    glm::vec3 center, float radius) {
    auto points = std::make_shared<std::vector<ShieldEffectPoint>>();
    if (!std::isfinite(radius) || radius <= 0)
        throw std::invalid_argument("Shield radius must be positive and finite.");
    const int resolution = static_cast<int>(std::ceil(std::clamp(radius*2,4.0f,12.0f)));
    const float cell = radius*2/resolution;
    const auto position = [=](int x, int y, int z) {
        return (glm::vec3(x,y,z)+0.5f)*cell-glm::vec3(radius);
    };
    const auto inside = [&](int x, int y, int z) {
        if (x < 0 || y < 0 || z < 0 || x >= resolution || y >= resolution || z >= resolution)
            return false;
        const auto p = position(x,y,z);
        return glm::dot(p,p) <= radius*radius;
    };
    constexpr glm::ivec3 neighbors[] = {{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
    for (int z = 0; z < resolution; ++z)
        for (int y = 0; y < resolution; ++y)
            for (int x = 0; x < resolution; ++x) {
                if (!inside(x,y,z)) continue;
                for (const auto& neighbor : neighbors) {
                    if (inside(x+neighbor.x,y+neighbor.y,z+neighbor.z)) continue;
                    const glm::vec3 normal(neighbor);
                    points->push_back({center+position(x,y,z)+normal*cell*0.5f,normal,cell});
                }
            }
    return points;
}
void CombatEffects::drawShield(const ShieldEffectBody& body, glm::vec3 viewer, bool includeIdle) {
    using S = CombatEffectSettings;
    if (!body.surface) return;
    const auto group = body.type == ShieldType::Sphere ? S::SphereShield : S::HullShield;
    const auto v = [&](int field) { return settings.value(group,field); };
    const auto baseColor = S::color(v(S::ShieldHue));
    const auto localViewer = body.transform.inverseApplyTo(viewer);
    const float period = v(S::SweepPeriod);
    const float phase = std::fmod(m_time + body.id.index*0.37f,period)/period;
    std::vector<std::pair<const ShieldPulse*,glm::vec3>> pulses;
    for (const auto& pulse : m_pulses) {
        if (!(pulse.id == body.id)) continue;
        auto position = pulse.position;
        if (pulse.kind == ShieldPulse::Hit && body.type == ShieldType::Sphere &&
            !body.surface->empty()) {
            const auto nearest = std::min_element(body.surface->begin(),body.surface->end(),
                [&](const auto& a, const auto& b) {
                    return glm::distance(a.position,position) < glm::distance(b.position,position);
                });
            position = nearest->position;
        }
        pulses.emplace_back(&pulse,position);
    }
    for (std::size_t i = 0; i < body.surface->size(); ++i) {
        const auto& point = (*body.surface)[i];
        const auto view = safeNormalize(localViewer-point.position,point.normal);
        const float rim = 1-std::abs(glm::dot(view,point.normal));
        const float sweep = (point.position.x-body.center.x)/std::max(body.radius,0.1f);
        const bool swept = std::abs(sweep-(phase*2.6f-1.3f)) < 0.08f;
        float light = includeIdle && body.fraction > 0 ?
            v(S::IdleLight)*(0.15f+rim*rim) + (swept ? v(S::SweepLight) : 0) : 0;
        glm::vec3 color = baseColor;
        float expansion = 0;
        float fracture = 0;
        for (const auto& entry : pulses) {
            const auto& pulse = *entry.first;
            const float duration = v(pulse.kind == ShieldPulse::Hit ? S::HitTime :
                pulse.kind == ShieldPulse::Break ? S::BreakTime : S::RechargeTime);
            if (pulse.age >= duration) continue;
            const float t = pulse.age/duration;
            if (pulse.kind == ShieldPulse::Hit) {
                const float radius = v(S::HitRadius)*std::sqrt(pulse.strength);
                const float distance = glm::distance(point.position,entry.second);
                const float ring = std::abs(distance-radius*t);
                const float width = std::max(point.size*1.2f,radius*0.18f);
                if (distance < radius && ring < width) {
                    if (body.fraction > 0 && body.fraction < 0.25f && i%3 == 0) continue;
                    light += (1-ring/width)*(1-t)*v(S::HitLight) *
                        glm::clamp(pulse.strength,0.5f,2.0f);
                    if (pulse.age < 0.06f) color = glm::mix(baseColor,glm::vec3(1),0.85f);
                }
            } else if (pulse.kind == ShieldPulse::Break) {
                const float wave = std::max(0.0f,1-std::abs(sweep-(t*2.6f-1.3f))/0.55f);
                const float flash = glm::clamp(1-pulse.age/0.08f,0.0f,1.0f);
                light += v(S::BreakLight)*(flash*0.55f+(1-t)*wave) *
                    ((i%5) < 2 ? 1.0f : 0.25f);
                color = glm::mix(baseColor,glm::vec3(1),flash*0.8f);
                expansion = t*t*body.radius*(0.15f+(i%5)*0.045f);
                fracture = glm::clamp((t-0.08f)/0.38f,0.0f,1.0f);
            } else {
                const float band = std::abs(sweep-(t*2.4f-1.2f));
                if (band < 0.2f) light += v(S::RechargeLight)*(1-band/0.2f);
            }
        }
        if (light <= 0.002f) continue;
        const float size = point.size*v(S::CellSize);
        const auto q = body.transform.orientation()*facing(point.normal);
        const auto position = body.transform.applyTo(point.position+point.normal*expansion);
        const float scale = body.transform.scale();
        const float edge = size*0.07f;
        if (fracture > static_cast<float>(i%7)/7.0f) {
            if (i%3 == 0)
                box(position,glm::vec3(size*0.38f,size*0.38f,edge*2)*scale,color,light,q);
            continue;
        }
        for (float side : {-1.0f,1.0f}) {
            box(position+q*glm::vec3(side*size*0.5f*scale,0,0),
                glm::vec3(edge,size,edge)*scale,color,light,q);
            box(position+q*glm::vec3(0,side*size*0.5f*scale,0),
                glm::vec3(size,edge,edge)*scale,color,light,q);
        }
    }
}
const std::vector<CombatEffectBox>& CombatEffects::build(glm::vec3 viewer, bool includeShieldIdle) {
    using S = CombatEffectSettings;
    m_boxes.clear();
    const float distance = settings.value(S::Global,S::ViewDistance);
    const auto visible = [&](glm::vec3 p) { return glm::distance(viewer,p) <= distance; };
    for (const auto& burst : m_bursts) if (visible(burst.position)) drawBurst(burst);
    for (const auto& entry : m_trails) {
        const auto& trail = entry.second;
        if (trail.samples.empty() || !visible(trail.samples.back().position)) continue;
        if (trail.seen) drawProjectile(trail.gun,trail.samples.back().position,trail.direction,m_time);
        float remaining = settings.value(trail.gun,S::TrailLength);
        const float duration = settings.value(trail.gun,S::TrailTime);
        const float spacing = std::max(0.1f,remaining/settings.value(trail.gun,S::TrailSegments));
        const float width = settings.value(trail.gun,S::ProjectileSize)*0.55f;
        const auto color = S::color(settings.value(trail.gun,S::Hue));
        int emitted = 0;
        for (std::size_t i = trail.samples.size(); i > 1 && remaining > 0; --i) {
            const auto& a = trail.samples[i-1];
            const auto& b = trail.samples[i-2];
            const auto delta = b.position-a.position;
            const float length = glm::length(delta);
            if (length < 0.001f) continue;
            const float lengthToDraw = std::min(length,remaining);
            for (float d = 0; d < lengthToDraw && emitted < 32; d += spacing, ++emitted) {
                const float part = std::min(spacing,lengthToDraw-d);
                const float fraction = (d+part*0.5f)/length;
                const float time = glm::mix(a.time,b.time,fraction);
                const float fade = glm::clamp(1-(m_time-time)/duration,0.0f,1.0f);
                const auto position = a.position+delta*fraction;
                if (plasma(trail.gun) || trail.gun == 9) {
                    const auto orbit = facing(delta)*glm::vec3(std::sin(emitted*2.4f),
                        std::cos(emitted*2.4f),0)*width;
                    box(position+orbit,glm::vec3(width*fade*1.7f),color,fade*0.55f);
                } else {
                    box(position,{width*fade,width*fade,part*0.65f},
                        color,fade*0.55f,facing(delta));
                }
            }
            remaining -= length;
        }
    }
    std::vector<const ShieldState*> shields;
    for (const auto& entry : m_shields)
        if (visible(entry.second.body.transform.position())) shields.push_back(&entry.second);
    const auto hasPulse = [&](const ShieldState* state) {
        return std::any_of(m_pulses.begin(),m_pulses.end(),[&](const auto& pulse) {
            return pulse.id == state->body.id && pulse.age < 1;
        });
    };
    std::sort(shields.begin(),shields.end(),[&](const auto* a, const auto* b) {
        if (hasPulse(a) != hasPulse(b)) return hasPulse(a);
        const float da = glm::distance(viewer,a->body.transform.position());
        const float db = glm::distance(viewer,b->body.transform.position());
        return da != db ? da < db : key(a->body.id) < key(b->body.id);
    });
    for (const auto* state : shields) drawShield(state->body,viewer,includeShieldIdle);
    return m_boxes;
}

ShieldEffectBody CombatEffects::previewShield(std::size_t group) {
    using S = CombatEffectSettings;
    if (group != S::HullShield && group != S::SphereShield)
        throw std::invalid_argument("Preview shield requires a shield effect group.");
    ShieldEffectBody body;
    body.id = {1,1}; body.type = group == S::HullShield ? ShieldType::Hull : ShieldType::Sphere;
    body.radius = 3.5f; body.fraction = 1;
    if (body.type == ShieldType::Sphere) body.surface = sphereSurface({},body.radius);
    else {
        auto surface = std::make_shared<std::vector<ShieldEffectPoint>>();
        for (int axis = 0; axis < 3; ++axis) for (int side : {-1,1})
            for (int y = -2; y <= 2; ++y) for (int x = -2; x <= 2; ++x) {
                glm::vec3 p(0), normal(0);
                p[axis] = side*2.75f; normal[axis] = static_cast<float>(side);
                p[(axis+1)%3] = static_cast<float>(x);
                p[(axis+2)%3] = static_cast<float>(y);
                surface->push_back({p,normal,1});
            }
        body.surface = surface;
    }
    return body;
}

std::vector<CombatEffectBox> CombatEffects::preview(std::size_t group, float time) const {
    using S = CombatEffectSettings;
    CombatEffects demo;
    demo.settings = settings;
    const glm::vec3 viewer(10,8,18);
    const auto gun = group < S::GunCount ? group : 0;
    const float phase = std::fmod(time,2.0f);
    const float flight = 0.9f;
    demo.m_time = time;
    if (group < S::GunCount || group == S::Global) {
        demo.m_bursts.push_back({gun,{-5,0,0},{1,0,0},1,phase,false});
        if (phase < flight) {
            Trail trail;
            trail.gun = gun; trail.direction = {1,0,0}; trail.seen = true;
            for (int i = 0; i <= 16; ++i) {
                const float t = std::max(0.0f,phase-i*0.012f);
                trail.samples.push_front({{-5+t/flight*10,0,0},time-(phase-t)});
            }
            demo.m_trails.emplace(1,trail);
        } else {
            demo.m_bursts.push_back({gun,{5,0,0},{-1,0,0},1,phase-flight,true});
        }
    } else {
        auto body = previewShield(group);
        const float cycle = std::fmod(time,8.0f);
        if (cycle > 2 && cycle < 4) {
            body.fraction = 0.2f;
            demo.m_pulses.push_back({body.id,{0,0,body.type == ShieldType::Sphere ? 3.5f : 2.75f},
                1,std::fmod(cycle-2,1.0f),ShieldPulse::Hit});
        } else if (cycle >= 4 && cycle < 5.5f) {
            body.fraction = 0;
            demo.m_pulses.push_back({body.id,{},1,cycle-4,ShieldPulse::Break});
        } else if (cycle >= 5.5f) {
            demo.m_pulses.push_back({body.id,{},1,cycle-5.5f,ShieldPulse::Recharge});
        }
        demo.m_shields.emplace(1,ShieldState{body,false});
    }
    demo.build(viewer);
    const glm::vec3 hullColor(0.12f,0.2f,0.27f);
    if (group == S::HullShield || group == S::SphereShield) {
        demo.box({},glm::vec3(4.5f),hullColor,0.6f);
    } else {
        demo.box({-6,0,0},{1.5f,1,1},hullColor,0.7f);
        demo.box({5.6f,0,0},{1,4,4},hullColor,0.7f);
    }
    return demo.m_boxes;
}
