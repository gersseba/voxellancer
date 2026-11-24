#include "covergenerator.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <limits>
#include <stdexcept>

#include <glm/gtc/constants.hpp>

#include "voxel/voxel.h"
#include "world/world.h"
#include "worldobject/worldobject.h"
#include "worldobject/worldobjectinfo.h"

namespace procedural {

namespace {

std::uint64_t mix(std::uint64_t value) {
    value += 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

float latticeValue(std::uint64_t seed, int x, int y, int z) {
    std::uint64_t h = mix(seed ^ mix(static_cast<std::uint32_t>(x) * 0x8da6b343ull ^
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(y)) * 0xd8163841ull ^
        static_cast<std::uint64_t>(static_cast<std::uint32_t>(z)) * 0xcb1ab31full));
    return static_cast<float>(h >> 40) / static_cast<float>(1u << 24) * 2.0f - 1.0f;
}

// Smoothly interpolated lattice noise in [-1, 1].
float valueNoise(std::uint64_t seed, const glm::vec3& p) {
    const glm::vec3 base = glm::floor(p);
    const glm::ivec3 cell(base);
    glm::vec3 t = p - base;
    t = t * t * (3.0f - 2.0f * t);
    float corners[8];
    for (int i = 0; i < 8; ++i)
        corners[i] = latticeValue(seed, cell.x + (i & 1), cell.y + ((i >> 1) & 1), cell.z + ((i >> 2) & 1));
    const auto lerp = [](float a, float b, float f) { return a + (b - a) * f; };
    const float x00 = lerp(corners[0], corners[1], t.x), x10 = lerp(corners[2], corners[3], t.x);
    const float x01 = lerp(corners[4], corners[5], t.x), x11 = lerp(corners[6], corners[7], t.x);
    return lerp(lerp(x00, x10, t.y), lerp(x01, x11, t.y), t.z);
}

std::uint32_t shade(std::uint32_t color, float factor) {
    std::uint32_t result = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        const float channel = static_cast<float>((color >> shift) & 0xff) * factor;
        result |= static_cast<std::uint32_t>(std::clamp(channel, 0.0f, 255.0f)) << shift;
    }
    return result;
}

std::uint32_t blend(std::uint32_t from, std::uint32_t to, float amount) {
    std::uint32_t result = 0;
    for (int shift = 0; shift <= 16; shift += 8) {
        const float a = static_cast<float>((from >> shift) & 0xff);
        const float b = static_cast<float>((to >> shift) & 0xff);
        result |= static_cast<std::uint32_t>(std::clamp(a + (b - a) * amount, 0.0f, 255.0f)) << shift;
    }
    return result;
}

/** Dense voxel grid used while carving shapes; color 0 marks an empty cell. */
class Grid {
public:
    explicit Grid(const glm::ivec3& size) : m_size(size),
        m_cells(static_cast<std::size_t>(size.x) * size.y * size.z, 0u) {}

    const glm::ivec3& size() const { return m_size; }
    bool inside(const glm::ivec3& c) const {
        return c.x >= 0 && c.y >= 0 && c.z >= 0 && c.x < m_size.x && c.y < m_size.y && c.z < m_size.z;
    }
    std::uint32_t& at(const glm::ivec3& c) {
        return m_cells[(static_cast<std::size_t>(c.z) * m_size.y + c.y) * m_size.x + c.x];
    }
    void set(const glm::ivec3& c, std::uint32_t color) {
        if (inside(c)) at(c) = color | 0xff000000u;
    }
    void clear(const glm::ivec3& c) { if (inside(c)) at(c) = 0; }
    bool solid(const glm::ivec3& c) const {
        return inside(c) && m_cells[(static_cast<std::size_t>(c.z) * m_size.y + c.y) * m_size.x + c.x] != 0;
    }

    // Keeps the largest 6-connected component so the object never starts split,
    // and translates it so the smallest cell coordinate is zero.
    std::vector<CoverVoxel> largestComponent() {
        std::vector<int> label(m_cells.size(), -1);
        std::vector<std::size_t> sizes;
        const std::array<glm::ivec3, 6> neighbours{glm::ivec3(1,0,0), glm::ivec3(-1,0,0),
            glm::ivec3(0,1,0), glm::ivec3(0,-1,0), glm::ivec3(0,0,1), glm::ivec3(0,0,-1)};
        const auto index = [this](const glm::ivec3& c) {
            return (static_cast<std::size_t>(c.z) * m_size.y + c.y) * m_size.x + c.x;
        };
        std::deque<glm::ivec3> open;
        for (int z = 0; z < m_size.z; ++z) for (int y = 0; y < m_size.y; ++y) for (int x = 0; x < m_size.x; ++x) {
            const glm::ivec3 start(x, y, z);
            if (!solid(start) || label[index(start)] >= 0) continue;
            const int component = static_cast<int>(sizes.size());
            sizes.push_back(0);
            label[index(start)] = component;
            open.push_back(start);
            while (!open.empty()) {
                const glm::ivec3 cell = open.front();
                open.pop_front();
                ++sizes.back();
                for (const auto& offset : neighbours) {
                    const glm::ivec3 next = cell + offset;
                    if (solid(next) && label[index(next)] < 0) {
                        label[index(next)] = component;
                        open.push_back(next);
                    }
                }
            }
        }
        std::vector<CoverVoxel> voxels;
        if (sizes.empty()) return voxels;
        const int keep = static_cast<int>(std::max_element(sizes.begin(), sizes.end()) - sizes.begin());
        glm::ivec3 low(std::numeric_limits<int>::max());
        for (int z = 0; z < m_size.z; ++z) for (int y = 0; y < m_size.y; ++y) for (int x = 0; x < m_size.x; ++x) {
            const glm::ivec3 cell(x, y, z);
            if (label[index(cell)] != keep) continue;
            voxels.push_back({cell, at(cell) & 0x00ffffffu});
            low = glm::min(low, cell);
        }
        for (auto& voxel : voxels) voxel.cell -= low;
        return voxels;
    }

private:
    glm::ivec3 m_size;
    std::vector<std::uint32_t> m_cells;
};

constexpr std::array<std::uint32_t, 3> HullColors{0x8c9399u, 0x7b8794u, 0x9a9083u};
constexpr std::array<std::uint32_t, 4> AccentColors{0xb3412eu, 0xc7862bu, 0xc2b23au, 0x3f78a8u};
constexpr std::uint32_t ScorchColor = 0x2d2a27u;
constexpr std::uint32_t FrameColor = 0x5b6067u;

std::vector<CoverVoxel> hullPlate(CoverRandom& rng) {
    const int width = rng.range(8, 13), length = rng.range(12, 18), thickness = rng.range(1, 2);
    const float curvature = rng.range(0.03f, 0.08f);
    const float half = (width - 1) * 0.5f;
    const int maxSag = static_cast<int>(std::round(curvature * half * half));
    Grid grid(glm::ivec3(width, thickness + maxSag + 2, length));
    const std::uint32_t hull = HullColors[rng.range(0, 2)];
    const std::uint32_t accent = AccentColors[rng.range(0, 3)];
    const bool striped = rng.unit() < 0.7f;
    const int stripe = rng.range(1, std::max(1, width - 3));
    const std::uint64_t noiseSeed = rng.next();
    for (int z = 0; z < length; ++z) for (int x = 0; x < width; ++x) {
        const float dx = x - half;
        const int base = 1 + maxSag - static_cast<int>(std::round(curvature * dx * dx));
        for (int t = 0; t < thickness; ++t) {
            std::uint32_t color = (z % 6 == 0 || x % 5 == 0) ? shade(hull, 0.84f) : hull;
            if (striped && (x == stripe || x == stripe + 1)) color = accent;
            grid.set(glm::ivec3(x, base + t, z), color);
        }
        if (z % 4 == 1 || x == width / 2)   // stiffening ribs and keel underneath
            grid.set(glm::ivec3(x, base - 1, z), FrameColor);
    }
    // Torn, jagged rim: erode cells close to the border by a noisy depth.
    for (int z = 0; z < length; ++z) for (int x = 0; x < width; ++x) {
        const int border = std::min({x, width - 1 - x, z, length - 1 - z});
        const int depth = static_cast<int>((valueNoise(noiseSeed, glm::vec3(x, 0, z) * 0.45f) + 1.0f) * 1.6f);
        if (border < depth)
            for (int y = 0; y < grid.size().y; ++y) grid.clear(glm::ivec3(x, y, z));
    }
    const int holes = rng.range(0, 2);
    for (int hole = 0; hole < holes; ++hole) {
        const glm::vec2 center(rng.range(2.0f, width - 2.0f), rng.range(3.0f, length - 3.0f));
        const float radius = rng.range(1.2f, 2.2f);
        for (int z = 0; z < length; ++z) for (int x = 0; x < width; ++x) {
            const float distance = glm::length(glm::vec2(x, z) - center);
            for (int y = 0; y < grid.size().y; ++y) {
                const glm::ivec3 cell(x, y, z);
                if (distance < radius) grid.clear(cell);
                else if (distance < radius + 1.6f && grid.solid(cell))
                    grid.at(cell) = blend(grid.at(cell), ScorchColor, 0.7f) | 0xff000000u;
            }
        }
    }
    return grid.largestComponent();
}

std::vector<CoverVoxel> girder(CoverRandom& rng) {
    const int length = rng.range(16, 26);
    const int kink = rng.range(length / 3, 2 * length / 3);
    const int sideBend = rng.range(3, 5), dropBend = rng.range(4, 7);
    const int maxShift = (length - kink) / std::min(sideBend, dropBend) + 1;
    Grid grid(glm::ivec3(3 + maxShift, 3 + maxShift, length));
    const std::uint32_t metal = rng.unit() < 0.5f ? 0x767c84u : 0x6d7178u;
    const std::uint64_t rustSeed = rng.next();
    std::array<int, 4> cutoff{};
    for (auto& end : cutoff) end = length - rng.range(0, 4);
    for (int z = 0; z < length; ++z) {
        const int bent = std::max(0, z - kink);
        const glm::ivec3 offset(bent / sideBend, bent / dropBend, z);
        const auto color = [&](const glm::ivec3& c, std::uint32_t base) {
            const float rust = valueNoise(rustSeed, glm::vec3(c) * 0.35f);
            return rust > 0.35f ? blend(base, 0x8a5a3cu, std::min(1.0f, (rust - 0.35f) * 3.0f)) : base;
        };
        for (int corner = 0; corner < 4; ++corner) {
            if (z >= cutoff[corner]) continue;
            const glm::ivec3 cell = offset + glm::ivec3((corner & 1) * 2, (corner >> 1) * 2, 0);
            grid.set(cell, color(cell, metal));
        }
        const int ring = std::min({cutoff[0], cutoff[1], cutoff[2], cutoff[3]});
        if (z % 3 == 0 && z < ring) {  // bracing frame between the four longerons
            for (int i = 0; i < 3; ++i) {
                for (const glm::ivec3 edge : {glm::ivec3(i, 0, 0), glm::ivec3(i, 2, 0),
                        glm::ivec3(0, i, 0), glm::ivec3(2, i, 0)}) {
                    const glm::ivec3 cell = offset + edge;
                    grid.set(cell, color(cell, shade(metal, 0.85f)));
                }
            }
        }
    }
    return grid.largestComponent();
}

std::vector<CoverVoxel> wreckSection(CoverRandom& rng) {
    const int width = rng.range(6, 9), height = rng.range(5, 7), length = rng.range(10, 15);
    Grid grid(glm::ivec3(width, height, length));
    const std::uint32_t hull = HullColors[rng.range(0, 2)];
    const std::uint32_t accent = AccentColors[rng.range(0, 3)];
    const bool striped = rng.unit() < 0.7f;
    const int bulkhead = rng.range(3, length - 5);
    const std::uint64_t tearSeed = rng.next(), scorchSeed = rng.next();
    for (int z = 0; z < length; ++z) for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const bool shell = x == 0 || y == 0 || x == width - 1 || y == height - 1;
        const bool doorway = std::abs(2 * x - (width - 1)) <= 2 && y >= 1 && y <= 3;
        const bool wall = (z == 0 || z == bulkhead) && !doorway;
        if (!shell && !wall) continue;
        std::uint32_t color = z % 4 == 0 ? shade(hull, 0.85f) : hull;
        if (!shell) color = FrameColor;
        if (striped && (x == 0 || x == width - 1) && y == height / 2) color = accent;
        const float scorch = valueNoise(scorchSeed, glm::vec3(x, y, z) * 0.4f);
        if (scorch > 0.4f) color = blend(color, ScorchColor, std::min(1.0f, (scorch - 0.4f) * 2.5f));
        grid.set(glm::ivec3(x, y, z), color);
    }
    // Torn open far end with a ragged edge.
    for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
        const int tear = static_cast<int>((valueNoise(tearSeed, glm::vec3(x, y, 0) * 0.5f) + 1.0f) * 2.2f);
        for (int z = length - 1 - tear; z < length; ++z) grid.clear(glm::ivec3(x, y, z));
    }
    const int breaches = rng.range(1, 2);
    for (int breach = 0; breach < breaches; ++breach) {
        const bool side = rng.unit() < 0.5f;
        const glm::vec3 center = side
            ? glm::vec3(rng.unit() < 0.5f ? 0.0f : width - 1.0f, rng.range(1.0f, height - 2.0f), rng.range(2.0f, length - 3.0f))
            : glm::vec3(rng.range(1.0f, width - 2.0f), rng.unit() < 0.5f ? 0.0f : height - 1.0f, rng.range(2.0f, length - 3.0f));
        const float radius = rng.range(1.3f, 2.4f);
        for (int z = 0; z < length; ++z) for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
            const glm::ivec3 cell(x, y, z);
            const float distance = glm::length(glm::vec3(cell) - center);
            if (distance < radius) grid.clear(cell);
            else if (distance < radius + 1.5f && grid.solid(cell))
                grid.at(cell) = blend(grid.at(cell), ScorchColor, 0.75f) | 0xff000000u;
        }
    }
    const int crates = rng.range(2, 4);
    for (int crate = 0; crate < crates; ++crate) {
        const glm::ivec3 cell(rng.range(1, width - 2), 1, rng.range(1, length - 4));
        grid.set(cell, crate % 2 ? 0xc8a040u : 0x3f4a52u);
        grid.set(cell + glm::ivec3(0, 1, 0), 0x3f4a52u);
    }
    return grid.largestComponent();
}

// A large broken mid-section of a capital ship: an elliptical ribbed hull with a
// deck, sheared ends, and scorched breaches you can see (and shoot) through.
std::vector<CoverVoxel> hulk(CoverRandom& rng) {
    const float a = rng.range(7.0f, 9.0f), b = rng.range(5.0f, 6.5f);
    const int length = rng.range(30, 38);
    const glm::ivec3 size(static_cast<int>(2.0f * a) + 3, static_cast<int>(2.0f * b) + 3, length);
    const glm::vec2 center((size.x - 1) * 0.5f, (size.y - 1) * 0.5f);
    Grid grid(size);
    const std::uint32_t hull = HullColors[rng.range(0, 2)];
    const std::uint32_t accent = AccentColors[rng.range(0, 3)];
    const int ringSpacing = rng.range(4, 6);
    const float deck = -b * rng.range(0.25f, 0.45f);
    const std::uint64_t tearSeed = rng.next(), scorchSeed = rng.next();
    const auto ellipse = [&](float dx, float dy, float inset) {
        const float ea = a - inset, eb = b - inset;
        return (dx * dx) / (ea * ea) + (dy * dy) / (eb * eb);
    };
    for (int z = 0; z < length; ++z) for (int y = 0; y < size.y; ++y) for (int x = 0; x < size.x; ++x) {
        const float dx = x - center.x, dy = y - center.y;
        if (ellipse(dx, dy, 0.0f) > 1.0f) continue;
        const bool ring = z % ringSpacing == 0;
        const bool shell = ellipse(dx, dy, ring ? 2.2f : 1.2f) > 1.0f;
        const bool floor = std::abs(dy - deck) < 0.5f;
        if (!shell && !floor) continue;
        std::uint32_t color = ring ? FrameColor : (z % 3 == 0 ? shade(hull, 0.88f) : hull);
        if (floor && !shell) color = shade(FrameColor, 1.1f);
        else if (!ring && std::abs(dy) < 1.0f && std::abs(dx) > a - 1.5f) color = accent;
        const float scorch = valueNoise(scorchSeed, glm::vec3(x, y, z) * 0.3f);
        if (scorch > 0.45f) color = blend(color, ScorchColor, std::min(1.0f, (scorch - 0.45f) * 2.5f));
        grid.set(glm::ivec3(x, y, z), color);
    }
    // Both ends were torn off; the far one is sheared at an angle.
    const float shear = rng.range(-0.5f, 0.5f);
    for (int y = 0; y < size.y; ++y) for (int x = 0; x < size.x; ++x) {
        const float noise = valueNoise(tearSeed, glm::vec3(x, y, 0.0f) * 0.4f) + 1.0f;
        const int front = static_cast<int>(noise * 1.8f);
        const int back = static_cast<int>(noise * 2.2f + 3.0f + shear * (x - center.x));
        for (int z = 0; z < length; ++z)
            if (z < front || z >= length - std::max(0, back)) grid.clear(glm::ivec3(x, y, z));
    }
    const int breaches = rng.range(2, 4);
    for (int breach = 0; breach < breaches; ++breach) {
        const float angle = rng.range(0.0f, glm::two_pi<float>());
        const glm::vec3 hit(center.x + std::cos(angle) * a, center.y + std::sin(angle) * b,
            rng.range(5.0f, length - 8.0f));
        const float radius = rng.range(2.2f, 3.6f);
        for (int z = 0; z < length; ++z) for (int y = 0; y < size.y; ++y) for (int x = 0; x < size.x; ++x) {
            const glm::ivec3 cell(x, y, z);
            const float distance = glm::length(glm::vec3(cell) - hit);
            if (distance < radius) grid.clear(cell);
            else if (distance < radius + 2.0f && grid.solid(cell))
                grid.at(cell) = blend(grid.at(cell), ScorchColor, 0.75f) | 0xff000000u;
        }
    }
    return grid.largestComponent();
}

glm::vec3 centerOfCells(const std::vector<CoverVoxel>& voxels) {
    glm::vec3 sum(0.0f);
    for (const auto& voxel : voxels) sum += glm::vec3(voxel.cell);
    return voxels.empty() ? sum : sum / static_cast<float>(voxels.size());
}

}

CoverRandom::CoverRandom(std::uint64_t seed) : m_state(seed) {}

std::uint64_t CoverRandom::next() {
    m_state += 0x9e3779b97f4a7c15ull;
    std::uint64_t value = m_state;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    return value ^ (value >> 31);
}

float CoverRandom::unit() {
    return static_cast<float>(next() >> 40) / static_cast<float>(1u << 24);
}

float CoverRandom::range(float from, float to) {
    return from + (to - from) * unit();
}

int CoverRandom::range(int from, int to) {
    if (to <= from) return from;
    const auto span = static_cast<std::uint64_t>(to - from) + 1;
    return from + static_cast<int>(next() % span);
}

glm::vec3 CoverRandom::direction() {
    const float z = range(-1.0f, 1.0f);
    const float angle = range(0.0f, glm::two_pi<float>());
    const float planar = std::sqrt(std::max(0.0f, 1.0f - z * z));
    return glm::vec3(planar * std::cos(angle), planar * std::sin(angle), z);
}

std::vector<CoverVoxel> generateAsteroid(std::uint64_t seed, int radius) {
    radius = std::clamp(radius, 2, 12);
    CoverRandom rng(seed);
    const float r = static_cast<float>(radius);
    // Room for stretching and noise displacement so the rock is never clipped flat.
    const int margin = radius / 2 + 2;
    const int size = 2 * (radius + margin) + 1;
    const glm::vec3 center(static_cast<float>(radius + margin));

    struct Sphere { glm::vec3 center; float radius; };
    std::vector<Sphere> lobes{{center, r * rng.range(0.72f, 0.85f)}};
    const int extraLobes = rng.range(2, 4);
    for (int lobe = 0; lobe < extraLobes; ++lobe) {
        const float lobeRadius = r * rng.range(0.38f, 0.6f);
        const float offset = std::min(r * rng.range(0.3f, 0.5f), r - lobeRadius);
        lobes.push_back({center + rng.direction() * offset, lobeRadius});
    }
    const glm::vec3 stretch(rng.range(0.8f, 1.15f), rng.range(0.75f, 1.05f), rng.range(0.85f, 1.2f));
    std::vector<Sphere> craters;
    const int craterCount = rng.range(1, 3) + (radius >= 5 ? 1 : 0);
    for (int crater = 0; crater < craterCount; ++crater)
        craters.push_back({center + rng.direction() * r * rng.range(0.85f, 1.05f), r * rng.range(0.25f, 0.42f)});

    constexpr std::array<std::uint32_t, 3> rockColors{0x5d544bu, 0x7a746bu, 0x67707au};
    const std::uint32_t rock = rockColors[rng.range(0, 2)];
    const std::uint32_t ore = rng.unit() < 0.5f ? 0xb0874au : 0x4fa3a5u;
    const bool hasOre = rng.unit() < 0.5f;
    const std::uint64_t shapeSeed = rng.next(), detailSeed = rng.next();
    const std::uint64_t toneSeed = rng.next(), oreSeed = rng.next();
    const float frequency = 1.0f / std::max(2.0f, r * 0.45f);

    Grid grid{glm::ivec3(size)};
    for (int z = 0; z < size; ++z) for (int y = 0; y < size; ++y) for (int x = 0; x < size; ++x) {
        const glm::vec3 p(x, y, z);
        const glm::vec3 q = center + (p - center) / stretch;
        float field = -1.0f;
        for (const auto& lobe : lobes)
            field = std::max(field, 1.0f - glm::length(q - lobe.center) / lobe.radius);
        field += 0.22f * (0.65f * valueNoise(shapeSeed, p * frequency) +
            0.35f * valueNoise(detailSeed, p * frequency * 2.3f));
        float craterShade = 1.0f;
        for (const auto& crater : craters) {
            const float distance = glm::length(p - crater.center) / crater.radius;
            if (distance < 1.0f) field = -1.0f;
            else if (distance < 1.4f) craterShade = std::min(craterShade, 0.78f);
        }
        if (field <= 0.0f) continue;
        float tone = 1.0f + 0.14f * valueNoise(toneSeed, p * 0.5f) +
            0.04f * latticeValue(toneSeed ^ 0x51ull, x, y, z);
        std::uint32_t color = shade(rock, tone * craterShade);
        if (hasOre && valueNoise(oreSeed, p * 0.35f) > 0.55f) color = shade(ore, tone);
        grid.set(glm::ivec3(x, y, z), color);
    }
    return grid.largestComponent();
}

std::vector<CoverVoxel> generateDebris(std::uint64_t seed, CoverKind kind) {
    CoverRandom rng(seed);
    switch (kind) {
    case CoverKind::HullPlate: return hullPlate(rng);
    case CoverKind::Girder: return girder(rng);
    case CoverKind::WreckSection: return wreckSection(rng);
    case CoverKind::Hulk: return hulk(rng);
    case CoverKind::Asteroid: break;
    }
    throw std::invalid_argument("generateDebris requires a debris kind.");
}

namespace {

void finishPiece(CoverPiece& piece, CoverRandom& rng) {
    piece.orientation = glm::angleAxis(rng.range(0.0f, glm::two_pi<float>()), rng.direction());
    const glm::vec3 center = centerOfCells(piece.voxels);
    float extent = 0.0f;
    for (const auto& voxel : piece.voxels)
        extent = std::max(extent, glm::length(glm::vec3(voxel.cell) - center));
    piece.radius = (extent + 0.87f) * piece.scale;
}

}

CoverPiece makeAsteroidPiece(std::uint64_t seed, int radius, float scale) {
    CoverRandom rng(seed);
    CoverPiece piece;
    piece.kind = CoverKind::Asteroid;
    piece.voxels = generateAsteroid(rng.next(), radius);
    piece.scale = scale;
    piece.density = 4.0f;
    piece.hp = 200.0f;
    piece.name = "ASTEROID";
    finishPiece(piece, rng);
    return piece;
}

CoverPiece makeCoverPiece(CoverKind kind, std::uint64_t seed) {
    CoverRandom rng(seed);
    if (kind == CoverKind::Asteroid)
        return makeAsteroidPiece(rng.next(), rng.range(3, 6), rng.range(5.0f, 8.0f));
    CoverPiece piece;
    piece.kind = kind;
    piece.voxels = generateDebris(rng.next(), kind);
    if (kind == CoverKind::Hulk) {
        piece.scale = rng.range(4.0f, 4.6f);
        piece.density = 3.0f;
        piece.hp = 140.0f;
        piece.name = "DERELICT HULK";
    } else {
        piece.scale = rng.range(3.0f, 4.2f);
        piece.density = 2.0f;
        piece.hp = 90.0f;
        piece.name = "WRECKAGE";
    }
    finishPiece(piece, rng);
    return piece;
}

WorldObject* buildCover(World& world, const CoverPiece& piece, const glm::vec3& position) {
    if (piece.voxels.empty()) throw std::invalid_argument("Cover piece has no voxels.");
    auto* object = new WorldObject();
    object->setWorld(world);
    for (const auto& voxel : piece.voxels)
        object->addVoxel(new Voxel(voxel.cell, voxel.color, piece.density, piece.hp));
    object->transform().setScale(piece.scale);
    object->transform().setOrientation(piece.orientation);
    object->transform().setPosition(position);
    object->info().setName(piece.name);
    return object;
}

}
