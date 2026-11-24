#include <bandit/bandit.h>

#include <algorithm>
#include <cstdint>
#include <queue>
#include <set>
#include <stdexcept>
#include <tuple>
#include <vector>

#include "procedural/covergenerator.h"

using namespace bandit;
using namespace procedural;

namespace {
    using Key = std::tuple<int, int, int>;

    bool connected(const std::vector<CoverVoxel>& voxels) {
        std::set<Key> cells;
        for (const auto& voxel : voxels) cells.insert({voxel.cell.x, voxel.cell.y, voxel.cell.z});
        std::set<Key> seen{*cells.begin()};
        std::queue<Key> open;
        open.push(*cells.begin());
        const int offsets[6][3] = {{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}};
        while (!open.empty()) {
            const auto [x, y, z] = open.front();
            open.pop();
            for (const auto& offset : offsets) {
                const Key next{x + offset[0], y + offset[1], z + offset[2]};
                if (cells.count(next) && seen.insert(next).second) open.push(next);
            }
        }
        return seen.size() == cells.size();
    }

    void expectUsable(const std::vector<CoverVoxel>& voxels, std::size_t minimum) {
        AssertThat(voxels.size() >= minimum, IsTrue());
        std::set<Key> unique;
        for (const auto& voxel : voxels) {
            AssertThat(glm::all(glm::greaterThanEqual(voxel.cell, glm::ivec3(0))), IsTrue());
            unique.insert({voxel.cell.x, voxel.cell.y, voxel.cell.z});
        }
        AssertThat(unique.size(), Equals(voxels.size()));
        AssertThat(connected(voxels), IsTrue());
    }

    bool sameShape(const std::vector<CoverVoxel>& a, const std::vector<CoverVoxel>& b) {
        return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(),
            [](const CoverVoxel& l, const CoverVoxel& r) { return l.cell == r.cell && l.color == r.color; });
    }
}

go_bandit([]() {
    describe("procedural cover generator", []() {
        it("produces deterministic solid asteroids that vary with the seed", []() {
            for (std::uint64_t seed = 1; seed <= 12; ++seed) {
                const auto rock = generateAsteroid(seed, 5);
                expectUsable(rock, 150);
                AssertThat(sameShape(rock, generateAsteroid(seed, 5)), IsTrue());
                AssertThat(sameShape(rock, generateAsteroid(seed + 100, 5)), IsFalse());
            }
        });

        it("produces connected debris for every wreckage kind", []() {
            for (auto kind : {CoverKind::HullPlate, CoverKind::Girder, CoverKind::WreckSection, CoverKind::Hulk}) {
                for (std::uint64_t seed = 1; seed <= 8; ++seed) {
                    const auto debris = generateDebris(seed, kind);
                    expectUsable(debris, 20);
                    AssertThat(sameShape(debris, generateDebris(seed, kind)), IsTrue());
                }
            }
            AssertThrows(std::invalid_argument, generateDebris(1, CoverKind::Asteroid));
        });

        it("sizes pieces with a bounding radius around their center", []() {
            const auto rock = makeCoverPiece(CoverKind::Asteroid, 7);
            const auto wreck = makeCoverPiece(CoverKind::WreckSection, 7);
            AssertThat(rock.name, Equals("ASTEROID"));
            AssertThat(wreck.name, Equals("WRECKAGE"));
            AssertThat(rock.radius > rock.scale * 2.0f, IsTrue());
            AssertThat(wreck.radius > wreck.scale, IsTrue());
            AssertThat(rock.density > wreck.density, IsTrue());
            const auto hulk = makeCoverPiece(CoverKind::Hulk, 7);
            AssertThat(hulk.name, Equals("DERELICT HULK"));
            AssertThat(hulk.voxels.size() > 4 * wreck.voxels.size(), IsTrue());
            AssertThat(hulk.radius > 2.0f * wreck.radius, IsTrue());
        });
    });
});
