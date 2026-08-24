#pragma once

#include <cstdint>

#include <glm/glm.hpp>

namespace apricot {

// The height field. There is no heightmap file and there never will be — the
// terrain IS this function, evaluated on demand.
//
// The contract that everything else depends on: height_at() is PURE. Same
// (seed, x, z) gives the same metre value on every machine, every run, in any
// order. Chunks are therefore regenerable rather than storable, and physics can
// query the ground at a position no chunk has meshed.
//
// THE WORLD IS NOW A FUNCTION OF (MAP, SEED, COORD), NOT (SEED, COORD). The
// field below is noise plus a radial falloff plus the AUTHORED terrain
// operators in src/city/terrain_ops.h, which is what puts a flat downtown, a
// harbour deep enough for a ship and a channel with a bridge over it where the
// map says they are instead of where the noise felt like putting them. The map
// is compiled constexpr data, so it is part of the binary and nothing about it
// is loaded, parsed or serialised: a save file is a seed plus the build.
//
// The operators are pure by the same rules, and the dependency runs one way.
// terrain includes city; city must never include terrain, or evaluating an
// operator can reach back into the field it is modifying.
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
//
// 96 m hills are the right size to rally over and the wrong size to lay a
// street on: a block is 92 x 62 m, so at 96 m every downtown block sat on its
// own hill. Broader landforms give the long, drivable grades a city needs.
inline constexpr float kFeatureMetres = 240.0f;

// Peak-to-trough amplitude of the field, in metres. The full span from the
// deepest sea floor to the highest ridge. Buys a ~125 m summit on Ferrone Hill
// while the shore band stays the same FRACTION of the range, so beaches do not
// get wider when the mountain gets taller.
inline constexpr float kHeightMetres = 190.0f;

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
//
// Pinatty's island is 5.5 km across, so a third of that diameter is ~1850 m.
// Leaving this at 900 against an island that size is the exact failure the
// paragraph above warns about from the other direction: six separate highland
// lumps instead of one backbone, and no massif to put Ferrone Hill on.
inline constexpr float kContinentMetres = 1850.0f;

// Wavelength of the ridged term that carves the mountain spines. Steepness is
// amplitude over horizontal run, so this constant is the main lever on how
// severe the faces are — shortening it makes the range more alpine.
//
// Longer run, gentler faces, one massif instead of an alpine range. At 260 m
// against this island the measured 99th-percentile slope was over 50 degrees
// and only 9% of the land was under 5 degrees; you cannot lay a city on that.
inline constexpr float kRidgeMetres = 420.0f;

// Distance from the origin, in metres, at which the island has fully given way
// to open water. The playable area is bounded by SEA, not by an invisible
// wall: drive far enough and you are simply swimming.
//
// 2750 m of radius is the ~16 km2 of land the pilot game asks for, inside a
// 6144 m world box. Everything else in this table follows from this one
// number: it is the island, and the rest is what keeps the island in scale.
inline constexpr float kIslandRadiusMetres = 2750.0f;

// Fraction of kIslandRadiusMetres at which the land starts falling away. Below
// this the mask is a flat 1 and the terrain is whatever the noise says.
//
// More of the disc at full strength. This is the constant that turns a 39%
// land fill into a 46% one, which is most of the difference between "an
// island with a city on it" and "a city that keeps running out of island".
inline constexpr float kShoreFalloffStart = 0.78f;

// How far the coastline is allowed to wander in or out from a perfect circle,
// in metres. Without this the island is a poker chip and every player notices
// within ten seconds.
//
// At Pinatty's scale the coastline has to wander by HUNDREDS of metres, not
// tens, or the harbour, the channel and the bay all have to be carved by hand
// and the island between them is a poker chip with three bites out of it.
inline constexpr float kCoastWarpMetres = 560.0f;

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
//
// THAT IS NOW REVERSED. It sits just ABOVE kShoreLevel now, because the
// rally island's shallow inland lagoons were charming; in a city
// they are potholes the size of a city block, and the districts are placed by
// hand on ground that has to still be there.
inline constexpr float kIslandPlatform = 0.235f;

// THERE IS NO SPAWN-LIFT DOME, and its absence is load-bearing.
//
// kHomeRadiusMetres used to lift a 380 m dome of terrain at the world origin
// so that a random seed could not drop the car into a lagoon. It was the right
// answer for a rally island generated fresh per seed. It is the wrong answer
// here for a specific reason: PINATTY'S ORIGIN IS DOWNTOWN. That dome would
// not have been a safety net, it would have BEEN the terrain under the
// financial district -- a 42%-of-headroom bulge in the middle of Vellum Row
// that nobody authored and no district polygon knows about.
//
// An authored map does not need a spawn guarantee, because the spawn is
// authored. The flat ground downtown now comes from a terrain operator in
// src/city/terrain_ops.h, which puts it there ON PURPOSE, at a stated height,
// with a feathered edge, in a table you can read.
//
// Deleting it moved every golden value in tests/terrain_determinism_tests.cpp,
// and that is the cost being paid on purpose rather than discovered later.

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
// radial falloff, which is what makes it an ISLAND and not an infinite plain,
// mapped into metres, and finally passed through the authored terrain
// operators. The operators go LAST, on metres, and the order matters: see the
// note in heightmap.cpp, which explains why the design document's ordering
// would have tilted every waterfront flat in the map.
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
