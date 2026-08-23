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
//
// ONE HONEST CAVEAT ON "every machine". Bit-identity holds for any two runs of
// the same binary, and that is what terrain_determinism_tests proves. Across
// COMPILERS it additionally requires that floating-point contraction is not
// applied differently — an `a * b + c` fused into one rounding step lands on a
// different last bit than two. heightmap.cpp asks the compiler to switch
// contraction off for exactly this reason; if you port to a toolchain that
// ignores that request, re-pin the golden values there and say so out loud
// rather than discovering it as a replay desync.

// ---------------------------------------------------------------------------
//  Shape constants
//
//  These are the world. Changing any of them changes every seed's terrain, so
//  they are pinned by golden values in tests/terrain_determinism_tests.cpp and
//  editing one is a deliberate act that invalidates every existing replay.
// ---------------------------------------------------------------------------

// Horizontal wavelength of the base hill octave, in metres. Larger = broader,
// smoother landforms.
inline constexpr float kFeatureMetres = 96.0f;

// Peak-to-trough amplitude of the field, in metres. The full span from the
// deepest sea floor to the highest ridge.
inline constexpr float kHeightMetres = 150.0f;

// Where sea level sits inside the normalised [0, 1] shape, before the span
// above is applied. Everything below this is underwater; the fraction is
// therefore literally "how much of the island's vertical range is ocean".
inline constexpr float kShoreLevel = 0.22f;

// Wavelength of the continental term — the field that decides WHERE the high
// ground is, as opposed to what it looks like up close.
//
// Roughly a third of the island diameter, and that ratio is the tuning. Longer
// than the island and you sample less than one lattice cell of the base
// octave: the field becomes a single smooth blob with no structure and a
// measured range of about [0.37, 0.86], so any threshold placed on it never
// resolves and the mountains never switch on. Much shorter and the island
// grows six separate highland lumps instead of a backbone.
inline constexpr float kContinentMetres = 900.0f;

// Wavelength of the ridged term that carves the mountain spines. Steepness is
// amplitude over horizontal run, so this constant is the main lever on how
// severe the faces are — shortening it makes the range more alpine.
inline constexpr float kRidgeMetres = 260.0f;

// Distance from the origin, in metres, at which the island has fully given way
// to open water. The playable area is bounded by SEA, not by an invisible
// wall: drive far enough and you are simply swimming.
inline constexpr float kIslandRadiusMetres = 1400.0f;

// Fraction of kIslandRadiusMetres at which the land starts falling away. Below
// this the mask is a flat 1 and the terrain is whatever the noise says.
inline constexpr float kShoreFalloffStart = 0.45f;

// How far the coastline is allowed to wander in or out from a perfect circle,
// in metres. Without this the island is a poker chip and every player notices
// within ten seconds.
inline constexpr float kCoastWarpMetres = 260.0f;

// The island's base platform, in normalised shape units, applied before the
// radial mask.
//
// Without it the mask only SCALES the terrain: a seed whose noise happens to
// run low across the middle stays below sea level even where the mask says
// "this is the island", and the result is an archipelago — measured on one
// seed, a 220 m islet at the spawn point, open water out to 600 m, and 9.4%
// land against 26-32% for its neighbours. That is not an interesting variation
// for a driving game, it is a world with nowhere to drive.
//
// Sitting a hair BELOW kShoreLevel rather than above it is the deliberate part.
// Above, and the island becomes a solid disc with no inland water at all;
// below by this much, and the flattest ground inside the mask comes out as a
// shallow lagoon a metre or so deep while everything else is comfortably dry.
inline constexpr float kIslandPlatform = 0.21f;

// Radius, in metres, of the guaranteed-dry area around the world origin.
//
// The land shape is free to put a bay anywhere, which is most of what makes
// the coastline interesting — but the origin is where the car spawns, and a
// seed that drops the player into a lagoon is a broken game rather than an
// interesting variation. Inside this radius the shape is lifted toward dry
// land, weighted so that low ground is raised a lot and ground that is already
// high is barely touched.
inline constexpr float kHomeRadiusMetres = 380.0f;

// Peak-to-trough relief of the sea floor, in metres, applied only where the
// island mask has faded out. Stops the ocean bed being a suspiciously exact
// plane, which is the sort of thing that makes an underwater bug invisible.
inline constexpr float kSeaFloorReliefMetres = 7.0f;

// Analytic bounds of height_at(), derived from the constants above rather than
// measured, so they cannot drift away from the generator. Culling, streaming
// and any "is this value sane" assertion can lean on these.
inline constexpr float kMaxHeightMetres = (1.0f - kShoreLevel) * kHeightMetres;
inline constexpr float kMinHeightMetres =
    -kShoreLevel * kHeightMetres - kSeaFloorReliefMetres * 0.5f;

// Sea level, by construction. Named rather than spelled `0.0f` at nine call
// sites, because the day it stops being zero you want one place to change.
inline constexpr float kSeaLevelMetres = 0.0f;

// ---------------------------------------------------------------------------
//  Queries
// ---------------------------------------------------------------------------

// Terrain height in metres at a world XZ position.
//
// Multi-octave value fBm for the rolling ground, a weighted ridged fBm for the
// mountain spines, gated so the spines only bite where the continental term is
// already high — a ridge that erupts out of a bay reads as a bug even to
// someone who could not tell you why. The whole thing is then multiplied by a
// radial falloff, which is what makes it an ISLAND and not an infinite plain.
float height_at(uint64_t seed, float x, float z);

// Surface normal at a world XZ position, by central difference on height_at().
// Pure for the same reasons. Always unit length and always has normal.y > 0:
// a height field cannot overhang, so a downward-facing terrain normal would be
// a maths error, never a real feature.
glm::vec3 normal_at(uint64_t seed, float x, float z);

// The island falloff on its own, in [0, 1]. 1 well inside the coast, 0 out in
// open water. Exposed because scatter and any "am I still on the island"
// question want the mask itself, not a height they then have to guess about.
float island_mask(uint64_t seed, float x, float z);

}  // namespace apricot
