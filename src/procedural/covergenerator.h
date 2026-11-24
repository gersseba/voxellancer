#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

class World;
class WorldObject;

namespace procedural {

struct CoverVoxel {
    glm::ivec3 cell;
    std::uint32_t color;
};

enum class CoverKind : std::uint8_t {
    Asteroid,
    HullPlate,
    Girder,
    WreckSection,
    Hulk
};

/**
 * Portable deterministic random source (SplitMix64). Unlike the standard
 * distributions, its sequence is identical on every compiler and platform.
 */
class CoverRandom {
public:
    explicit CoverRandom(std::uint64_t seed);

    std::uint64_t next();
    float unit();                          // [0, 1)
    float range(float from, float to);     // [from, to)
    int range(int from, int to);           // [from, to]
    glm::vec3 direction();                 // uniform on the unit sphere

private:
    std::uint64_t m_state;
};

/**
 * Lumpy, cratered rock built from noise-displaced overlapping lobes.
 * Cells are non-negative and 6-connected; radius is clamped to [2, 12].
 */
std::vector<CoverVoxel> generateAsteroid(std::uint64_t seed, int radius);

/**
 * Broken spaceship wreckage: a curved, ribbed hull plate, a bent truss girder,
 * a torn hollow hull section, or a large derelict hulk (a broken capital-ship
 * mid-section). Cells are non-negative and 6-connected.
 */
std::vector<CoverVoxel> generateDebris(std::uint64_t seed, CoverKind kind);

/** A generated cover object, ready to be placed and built. */
struct CoverPiece {
    CoverKind kind = CoverKind::Asteroid;
    std::vector<CoverVoxel> voxels;
    float scale = 1.0f;
    float density = 1.0f;
    float hp = 1.0f;
    glm::quat orientation{1.0f, 0.0f, 0.0f, 0.0f};
    float radius = 0.0f;   // world-space bounding radius around the center of mass
    std::string name;
};

/** Generates an asteroid or a random wreckage piece with size, toughness and orientation. */
CoverPiece makeCoverPiece(CoverKind kind, std::uint64_t seed);

/** Generates an asteroid of a chosen cell radius and world scale. */
CoverPiece makeAsteroidPiece(std::uint64_t seed, int radius, float scale);

/** Builds an unspawned object whose center of mass sits at position. */
WorldObject* buildCover(World& world, const CoverPiece& piece, const glm::vec3& position);

}
