#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

enum class CombatCueKind { Fire, Shield, Hull };

struct CombatCue {
    std::uint64_t emitter;
    CombatCueKind kind;
    glm::vec3 position;
    bool personal;
    float distanceSquared;
};

inline bool sameCombatCue(const CombatCue& a, const CombatCue& b) {
    return a.emitter == b.emitter && a.kind == b.kind;
}

inline bool strongerCombatCue(const CombatCue& a, const CombatCue& b) {
    if (a.personal != b.personal) return a.personal;
    if (a.personal && (a.kind == CombatCueKind::Fire) != (b.kind == CombatCueKind::Fire))
        return a.kind != CombatCueKind::Fire;
    return a.distanceSquared < b.distanceSquared;
}

class CombatCueBudget {
public:
    static constexpr std::size_t MaxVoices = 12;
    static constexpr std::size_t MaxWorldVoices = 8;

    void submit(CombatCue cue) {
        constexpr float audibleRange = 700.0f;
        if (!cue.personal && !(cue.distanceSquared < audibleRange * audibleRange)) return;
        const auto duplicate = std::find_if(begin(), end(),
            [&](const auto& entry) { return sameCombatCue(cue, entry); });
        if (duplicate != end()) {
            if (strongerCombatCue(cue, *duplicate)) *duplicate = cue;
        } else if (m_count < m_cues.size()) {
            m_cues[m_count++] = cue;
        } else {
            const auto weakest = std::max_element(begin(), end(), strongerCombatCue);
            if (strongerCombatCue(cue, *weakest)) *weakest = cue;
        }
    }

    void prioritize() { std::sort(begin(), end(), strongerCombatCue); }
    auto begin() -> CombatCue* { return m_cues.data(); }
    auto end() -> CombatCue* { return begin() + m_count; }

    template<typename Voice>
    static bool admit(const CombatCue& cue, Voice first, Voice last, Voice& replace) {
        std::size_t total = 0, world = 0;
        replace = last;
        for (auto it = first; it != last; ++it) {
            if (sameCombatCue(cue, it->cue)) return false;
            ++total;
            if (!it->cue.personal) ++world;
        }
        const bool atWorldLimit = !cue.personal && world >= MaxWorldVoices;
        if (!atWorldLimit && total < MaxVoices) return true;
        for (auto it = first; it != last; ++it) {
            if ((atWorldLimit && it->cue.personal) || !strongerCombatCue(cue, it->cue)) continue;
            // Avoid cutting off a world cue for a nearly identical distance.
            if (!cue.personal && cue.distanceSquared >= 0.64f * it->cue.distanceSquared) continue;
            if (replace == last || strongerCombatCue(replace->cue, it->cue)) replace = it;
        }
        return replace != last;
    }

private:
    std::array<CombatCue, 2 * MaxVoices> m_cues{};
    std::size_t m_count = 0;
};
