#include "coverlayout.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace procedural {

namespace {

constexpr std::array<std::pair<CoverLayout, const char*>, 5> LayoutNames{{
    {CoverLayout::Empty, "none"},
    {CoverLayout::AsteroidField, "asteroidfield"},
    {CoverLayout::AsteroidLine, "asteroidline"},
    {CoverLayout::DebrisCluster, "debriscluster"},
    {CoverLayout::DebrisChunk, "debrischunk"},
}};

constexpr float SpawnMargin = 20.0f;
constexpr float PackedMargin = 6.0f;

class Placer {
public:
    explicit Placer(const CoverArena& arena) : m_arena(arena), m_taken(arena.keepOut) {}

    bool fits(const glm::vec3& position, float radius, float margin) const {
        return std::all_of(m_taken.begin(), m_taken.end(), [&](const CoverKeepOut& other) {
            return glm::distance(other.position, position) > other.radius + radius + margin;
        });
    }

    // Uniform point in a sub-box of the arena: lateral/vertical are fractions of the
    // half extents, near/far are fractions of the depth from nearZ towards farZ.
    glm::vec3 point(CoverRandom& rng, float lateral = 1.0f, float nearFraction = 0.0f,
            float farFraction = 1.0f, float vertical = 1.0f) const {
        return glm::vec3(m_arena.centerX + rng.range(-lateral, lateral) * m_arena.halfWidth,
            rng.range(-vertical, vertical) * m_arena.halfHeight,
            glm::mix(m_arena.nearZ, m_arena.farZ, rng.range(nearFraction, farFraction)));
    }

    bool tryPlace(const CoverPiece& piece, const glm::vec3& position, float margin) {
        if (!fits(position, piece.radius, margin)) return false;
        m_taken.push_back({position, piece.radius});
        m_placed.push_back({piece, position});
        return true;
    }

    const CoverArena& arena() const { return m_arena; }
    std::vector<PlacedCover> take() { return std::move(m_placed); }

private:
    const CoverArena& m_arena;
    std::vector<CoverKeepOut> m_taken;
    std::vector<PlacedCover> m_placed;
};

CoverKind randomDebrisKind(CoverRandom& rng) {
    constexpr std::array<CoverKind, 3> kinds{CoverKind::HullPlate, CoverKind::Girder, CoverKind::WreckSection};
    return kinds[static_cast<std::size_t>(rng.range(0, 2))];
}

// Mixed sizes scattered through the whole arena: a few boulders among many smaller rocks.
void asteroidField(CoverRandom& rng, Placer& placer) {
    const int count = rng.range(10, 14);
    for (int i = 0; i < count; ++i) {
        const bool boulder = rng.unit() < 0.3f;
        const auto piece = makeAsteroidPiece(rng.next(), boulder ? rng.range(5, 6) : rng.range(2, 4),
            boulder ? rng.range(6.5f, 8.0f) : rng.range(4.0f, 6.5f));
        for (int attempt = 0; attempt < 60; ++attempt)
            if (placer.tryPlace(piece, placer.point(rng), SpawnMargin)) break;
    }
}

// A bowed belt of rocks crossing the approach at a slant, broken by one or two
// gaps wide enough to fly through.
void asteroidLine(CoverRandom& rng, Placer& placer) {
    const auto& arena = placer.arena();
    const glm::vec3 anchor(arena.centerX + rng.range(-0.2f, 0.2f) * arena.halfWidth,
        rng.range(-0.4f, 0.4f) * arena.halfHeight,
        glm::mix(arena.nearZ, arena.farZ, rng.range(0.4f, 0.6f)));
    const float angle = rng.range(-0.45f, 0.45f);
    const glm::vec3 along = glm::normalize(
        glm::vec3(std::cos(angle), rng.range(-0.08f, 0.08f), std::sin(angle)));
    const glm::vec3 across(-std::sin(angle), 0.0f, std::cos(angle));
    const float bow = rng.range(-120.0f, 120.0f);
    const float halfLength = arena.halfWidth * 1.05f;
    constexpr float GapWidth = 90.0f;
    std::vector<float> gaps{rng.range(-0.6f, -0.05f) * halfLength};
    if (rng.unit() < 0.5f) gaps.push_back(rng.range(0.05f, 0.6f) * halfLength);

    float t = -halfLength;
    for (int count = 0; t < halfLength && count < 16;) {
        const auto piece = makeAsteroidPiece(rng.next(), rng.range(3, 5), rng.range(5.0f, 7.0f));
        const float center = t + piece.radius;
        const auto gap = std::find_if(gaps.begin(), gaps.end(), [&](float g) {
            return std::abs(center - g) < GapWidth * 0.5f + piece.radius;
        });
        if (gap != gaps.end()) {
            t = *gap + GapWidth * 0.5f;
            continue;
        }
        const float s = center / halfLength;
        const glm::vec3 position = anchor + along * center +
            across * (bow * (1.0f - s * s) + rng.range(-18.0f, 18.0f)) +
            glm::vec3(0.0f, rng.range(-15.0f, 15.0f), 0.0f);
        if (placer.tryPlace(piece, position, PackedMargin)) ++count;
        t = center + piece.radius + rng.range(10.0f, 30.0f);
    }
}

// A tight knot of wreckage around one point near the middle of the approach.
void debrisCluster(CoverRandom& rng, Placer& placer) {
    constexpr float ClusterRadius = 150.0f;
    glm::vec3 center = placer.point(rng, 0.5f, 0.35f, 0.65f, 0.4f);
    for (int attempt = 0; attempt < 40 && !placer.fits(center, ClusterRadius * 0.5f, 0.0f); ++attempt)
        center = placer.point(rng, 0.5f, 0.35f, 0.65f, 0.4f);
    const int count = rng.range(7, 10);
    for (int i = 0; i < count; ++i) {
        const auto piece = makeCoverPiece(randomDebrisKind(rng), rng.next());
        for (int attempt = 0; attempt < 80; ++attempt) {
            glm::vec3 offset = rng.direction() * ClusterRadius * std::sqrt(rng.unit());
            offset.y *= 0.6f;
            if (placer.tryPlace(piece, center + offset, PackedMargin)) break;
        }
    }
}

// One huge derelict hulk dominating the lane, ringed by a few fragments it shed.
void debrisChunk(CoverRandom& rng, Placer& placer) {
    const auto hulk = makeCoverPiece(CoverKind::Hulk, rng.next());
    glm::vec3 position;
    bool placed = false;
    for (int attempt = 0; attempt < 100 && !placed; ++attempt) {
        position = placer.point(rng, 0.4f, 0.3f, 0.7f, 0.4f);
        placed = placer.tryPlace(hulk, position, SpawnMargin + 10.0f);
    }
    if (!placed) return;
    const int fragments = rng.range(3, 5);
    for (int i = 0; i < fragments; ++i) {
        const auto piece = makeCoverPiece(
            rng.unit() < 0.5f ? CoverKind::HullPlate : CoverKind::Girder, rng.next());
        for (int attempt = 0; attempt < 60; ++attempt) {
            const glm::vec3 spot = position +
                rng.direction() * (hulk.radius + piece.radius + rng.range(10.0f, 90.0f));
            if (placer.tryPlace(piece, spot, PackedMargin)) break;
        }
    }
}

}

CoverLayout coverLayoutFromName(const std::string& name) {
    for (const auto& entry : LayoutNames)
        if (name == entry.second) return entry.first;
    throw std::invalid_argument("Unknown cover layout: " + name);
}

const char* coverLayoutName(CoverLayout layout) {
    for (const auto& entry : LayoutNames)
        if (layout == entry.first) return entry.second;
    throw std::invalid_argument("Unknown cover layout.");
}

std::vector<PlacedCover> generateCoverLayout(CoverLayout layout, std::uint64_t seed, const CoverArena& arena) {
    if (!(arena.farZ < arena.nearZ) || arena.halfWidth <= 0.0f || arena.halfHeight < 0.0f)
        throw std::invalid_argument("Invalid cover arena.");
    CoverRandom rng(seed);
    Placer placer(arena);
    switch (layout) {
    case CoverLayout::Empty: break;
    case CoverLayout::AsteroidField: asteroidField(rng, placer); break;
    case CoverLayout::AsteroidLine: asteroidLine(rng, placer); break;
    case CoverLayout::DebrisCluster: debrisCluster(rng, placer); break;
    case CoverLayout::DebrisChunk: debrisChunk(rng, placer); break;
    }
    return placer.take();
}

}
