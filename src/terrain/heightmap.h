#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace apricot {

// The height field. There is no heightmap file and there never will be — the
// terrain IS this function, evaluated on demand.
//
// The contract that everything else depends on: height_at() is PURE. Same
// (seed, x, z) gives the same metre value on every machine, every run, in any
// order. Chunks are therefore regenerable rather than storable, a save file is
// a seed, and physics can query the ground at a position no chunk has meshed.
//
// Consequences, which are not negotiable:
//   * No caching inside these functions. A cache with a stale entry turns a
//     pure function into a source of desync that only shows up after an hour.
//   * No state, no statics, no clock, no allocation.
//   * A chunk's edge vertices must come out bit-identical to its neighbour's,
//     which they do for free because both evaluate the same function at the
//     same coordinate. Never "fix" a seam by averaging across chunks.

// Horizontal distance between height-field control points, in metres. Larger
// = broader, smoother landforms.
inline constexpr float kFeatureMetres = 96.0f;

// Peak-to-trough amplitude of the field, in metres.
inline constexpr float kHeightMetres = 42.0f;

// Terrain height in metres at a world XZ position.
//
// PLACEHOLDER SHAPE. This is a single octave of bilinear value noise. It is
// genuinely pure and genuinely deterministic — the contract above holds — but
// it is one octave, so the world is smooth rolling hills with no ridges, no
// erosion, no biomes and nothing worth rallying over.
// TODO(terrain ticket): replace the body with multi-octave ridged fBm plus
// erosion and biome blending. Do NOT change this signature while doing so;
// physics, chunk meshing and the collider all call through it.
float height_at(uint64_t seed, float x, float z);

// Surface normal at a world XZ position, by central difference on height_at().
// Pure for the same reasons.
glm::vec3 normal_at(uint64_t seed, float x, float z);

}  // namespace apricot
