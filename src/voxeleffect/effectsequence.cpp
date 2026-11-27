#include "effectsequence.h"

#include <array>
#include <stdexcept>

EffectSequence::EffectSequence(const std::string& group, const std::string& kind, int fps):
    m_group(CombatEffectSettings::GroupCount), m_kind(Kind::Muzzle), m_fps(fps) {
    if (fps < 1 || fps > 240) throw std::invalid_argument("Effect lab FPS must be 1..240.");
    for (std::size_t i = 0; i < CombatEffectSettings::Global; ++i)
        if (CombatEffectSettings::key(i) == group) m_group = i;
    if (m_group == CombatEffectSettings::GroupCount)
        throw std::invalid_argument("Unknown effect lab group: " + group);
    const std::array<std::string,7> kinds{{"muzzle","projectile","impact",
        "shield-idle","shield-hit","shield-break","shield-recharge"}};
    bool found = false;
    for (std::size_t i = 0; i < kinds.size(); ++i) if (kinds[i] == kind) {
        m_kind = static_cast<Kind>(i);
        found = true;
    }
    if (!found || ((m_group < CombatEffectSettings::GunCount) != (m_kind <= Kind::Impact)))
        throw std::invalid_argument("Effect lab kind does not match group: " + kind);
}

void EffectSequence::restart() {
    m_effects.clear();
    m_frame = -1;
    if (m_group < CombatEffectSettings::GunCount) {
        if (m_kind == Kind::Muzzle) m_effects.fire(m_group,{0,0,0},{1,0,0});
        if (m_kind == Kind::Impact) m_effects.impact(m_group,{0,0,0},{0,0,1});
        return;
    }
    m_body = CombatEffects::previewShield(m_group);
    m_body.fraction = m_kind == Kind::ShieldRecharge ? 0 : 1;
    m_effects.shields({m_body});
    if (m_kind == Kind::ShieldHit)
        m_effects.shieldHit(m_body.id,{0,0,m_body.type == ShieldType::Sphere ? 3.5f : 2.75f},20);
    if (m_kind == Kind::ShieldBreak || m_kind == Kind::ShieldRecharge) {
        m_body.fraction = m_kind == Kind::ShieldBreak ? 0 : 0.75f;
        m_effects.shields({m_body});
    }
}

void EffectSequence::step(int index) {
    if (index > 0) m_effects.advance(1.0f/m_fps);
    if (m_kind == Kind::Projectile) {
        m_effects.beginProjectiles();
        const float time = static_cast<float>(index)/m_fps;
        if (time <= 0.8f)
            m_effects.projectile({1,1},m_group,{-4+10*time,0,0},{1,0,0});
        m_effects.endProjectiles();
    }
}

const std::vector<CombatEffectBox>& EffectSequence::frame(int index) {
    if (index < 0 || index > 14400) throw std::invalid_argument("Effect lab frame must be 0..14400.");
    if (m_frame < 0 || index < m_frame) restart();
    while (m_frame < index) step(++m_frame);
    return m_effects.build(viewer(),m_kind == Kind::ShieldIdle);
}
