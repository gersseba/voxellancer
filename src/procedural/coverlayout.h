#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "procedural/covergenerator.h"

namespace procedural {

/** How an arena's cover is arranged; independent of the encounter fought in it. */
enum class CoverLayout : std::uint8_t {
    Empty,          // open space
    AsteroidField,  // many mixed-size rocks scattered through the whole arena
    AsteroidLine,   // a curved belt of rocks across the approach, with fly-through gaps
    DebrisCluster,  // a tight knot of hull plates, girders and hull sections
    DebrisChunk     // one huge derelict hulk ringed by a few shed fragments
};

/** Parses the config names: none, asteroidfield, asteroidline, debriscluster, debrischunk. */
CoverLayout coverLayoutFromName(const std::string& name);
const char* coverLayoutName(CoverLayout layout);

struct CoverKeepOut {
    glm::vec3 position;
    float radius;
};

/**
 * The volume cover may occupy: z spans [farZ, nearZ] (both negative, players at
 * z = 0 facing -z), x spans centerX +- halfWidth, y spans +- halfHeight.
 * Pieces never come closer than their radius plus a margin to a keep-out.
 */
struct CoverArena {
    float nearZ = -170.0f;
    float farZ = -1000.0f;
    float centerX = 0.0f;
    float halfWidth = 500.0f;
    float halfHeight = 90.0f;
    std::vector<CoverKeepOut> keepOut;
};

struct PlacedCover {
    CoverPiece piece;
    glm::vec3 position;
};

/**
 * Generates and places the cover for a layout. The result is deterministic for
 * a seed and arena, and no two pieces (or a piece and a keep-out) overlap.
 * Pieces that find no free spot are dropped rather than forced in.
 */
std::vector<PlacedCover> generateCoverLayout(CoverLayout layout, std::uint64_t seed, const CoverArena& arena);

}
