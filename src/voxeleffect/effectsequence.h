#pragma once

#include "combateffects.h"

class EffectSequence {
public:
    enum class Kind { Muzzle, Projectile, Impact, ShieldIdle, ShieldHit, ShieldBreak, ShieldRecharge };
    EffectSequence(const std::string& group, const std::string& kind, int fps);
    CombatEffectSettings& settings() { return m_effects.settings; }
    const std::vector<CombatEffectBox>& frame(int index);
    int fps() const { return m_fps; }
    std::size_t group() const { return m_group; }
    Kind kind() const { return m_kind; }
    static glm::vec3 viewer() { return {10,8,18}; }

private:
    CombatEffects m_effects;
    ShieldEffectBody m_body;
    std::size_t m_group;
    Kind m_kind;
    int m_fps, m_frame = -1;
    void restart();
    void step(int index);
};
