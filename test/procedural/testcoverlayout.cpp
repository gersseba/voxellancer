#include <bandit/bandit.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include "procedural/coverlayout.h"

using namespace bandit;
using namespace procedural;

namespace {
    CoverArena testArena() {
        CoverArena arena;
        arena.nearZ = -170.0f;
        arena.farZ = -1010.0f;
        arena.centerX = 80.0f;
        arena.halfWidth = 520.0f;
        arena.keepOut = {{glm::vec3(0.0f), 80.0f}, {glm::vec3(0.0f, 0.0f, -600.0f), 60.0f},
            {glm::vec3(80.0f, 0.0f, -1160.0f), 80.0f}};
        return arena;
    }

    void expectClear(const std::vector<PlacedCover>& cover, const CoverArena& arena) {
        for (std::size_t i = 0; i < cover.size(); ++i) {
            for (const auto& keepOut : arena.keepOut)
                AssertThat(glm::distance(cover[i].position, keepOut.position) >
                    cover[i].piece.radius + keepOut.radius, IsTrue());
            for (std::size_t j = i + 1; j < cover.size(); ++j)
                AssertThat(glm::distance(cover[i].position, cover[j].position) >
                    cover[i].piece.radius + cover[j].piece.radius, IsTrue());
        }
    }

    int count(const std::vector<PlacedCover>& cover, CoverKind kind) {
        return static_cast<int>(std::count_if(cover.begin(), cover.end(),
            [kind](const PlacedCover& placed) { return placed.piece.kind == kind; }));
    }
}

go_bandit([]() {
    describe("procedural cover layouts", []() {
        it("round-trips the configured layout names", []() {
            for (auto layout : {CoverLayout::Empty, CoverLayout::AsteroidField, CoverLayout::AsteroidLine,
                    CoverLayout::DebrisCluster, CoverLayout::DebrisChunk})
                AssertThat(coverLayoutFromName(coverLayoutName(layout)) == layout, IsTrue());
            AssertThrows(std::invalid_argument, coverLayoutFromName("asteroids"));
        });

        it("places each layout deterministically without overlaps", []() {
            const auto arena = testArena();
            AssertThat(generateCoverLayout(CoverLayout::Empty, 3, arena).empty(), IsTrue());
            for (auto layout : {CoverLayout::AsteroidField, CoverLayout::AsteroidLine,
                    CoverLayout::DebrisCluster, CoverLayout::DebrisChunk}) {
                for (std::uint64_t seed = 1; seed <= 6; ++seed) {
                    const auto cover = generateCoverLayout(layout, seed, arena);
                    AssertThat(cover.empty(), IsFalse());
                    expectClear(cover, arena);
                    const auto again = generateCoverLayout(layout, seed, arena);
                    AssertThat(again.size(), Equals(cover.size()));
                    for (std::size_t i = 0; i < cover.size(); ++i)
                        AssertThat(again[i].position == cover[i].position, IsTrue());
                }
            }
        });

        it("gives each layout its own composition and shape", []() {
            const auto arena = testArena();
            for (std::uint64_t seed = 1; seed <= 6; ++seed) {
                const auto field = generateCoverLayout(CoverLayout::AsteroidField, seed, arena);
                AssertThat(count(field, CoverKind::Asteroid), Equals(static_cast<int>(field.size())));
                AssertThat(field.size() >= 8u, IsTrue());

                // The belt stretches across the lane far more than along it.
                const auto line = generateCoverLayout(CoverLayout::AsteroidLine, seed, arena);
                AssertThat(count(line, CoverKind::Asteroid), Equals(static_cast<int>(line.size())));
                AssertThat(line.size() >= 6u, IsTrue());
                const auto [left, right] = std::minmax_element(line.begin(), line.end(),
                    [](const PlacedCover& a, const PlacedCover& b) { return a.position.x < b.position.x; });
                AssertThat(right->position.x - left->position.x > 600.0f, IsTrue());

                const auto cluster = generateCoverLayout(CoverLayout::DebrisCluster, seed, arena);
                AssertThat(count(cluster, CoverKind::Asteroid) + count(cluster, CoverKind::Hulk), Equals(0));
                AssertThat(cluster.size() >= 5u, IsTrue());
                glm::vec3 center(0.0f);
                for (const auto& placed : cluster) center += placed.position;
                center /= static_cast<float>(cluster.size());
                for (const auto& placed : cluster)
                    AssertThat(glm::distance(placed.position, center) < 320.0f, IsTrue());

                const auto chunk = generateCoverLayout(CoverLayout::DebrisChunk, seed, arena);
                AssertThat(count(chunk, CoverKind::Hulk), Equals(1));
                AssertThat(count(chunk, CoverKind::Asteroid), Equals(0));
                AssertThat(chunk.size() >= 2u, IsTrue());
            }
        });
    });
});
