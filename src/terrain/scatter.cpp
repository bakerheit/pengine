#include "city/map.h"
#include "city/terrain_ops.h"
#include "terrain/scatter.h"

#include <cmath>

#include "core/rng.h"
#include "terrain/heightmap.h"
#include "terrain/noise.h"

namespace apricot {
namespace {

// Overall placement rate before the material's own density is applied. At 0.62
// a full-density material fills most of its cells, which at a 4 m pitch reads
// as woodland rather than as a plantation.
constexpr float kBaseDensity = 0.62f;

// Nothing grows in the sea, and nothing grows on the wet strip either. Props
// start a little above the water line so a swell never puts a tree in the surf.
constexpr float kMinPropAltitudeMetres = 0.8f;

// Sine of the steepest slope a prop will stand on. ~0.64 is about 40 degrees;
// above that a vertical trunk visibly fails to meet the ground and a rock
// reads as glued on rather than resting.
constexpr float kMaxPropSlope = 0.64f;

// Fraction of a cell a prop may wander from the centre, each axis. Kept below
// 0.5 so a prop can never leave the cell that generated it — which is what
// guarantees exactly one chunk emits it.
constexpr float kJitterFraction = 0.42f;

// kTwoPi is core's (core/rng.h). Four modules each declared their own before
// they were built together; all four spellings differed after the ninth digit.

// Per-kind scale spread. Trees vary a lot, rocks less; a boulder field where
// every rock is a different size looks like an asteroid belt.
constexpr float kTreeScaleMin = 0.72f;
constexpr float kTreeScaleMax = 1.45f;
constexpr float kRockScaleMin = 0.55f;
constexpr float kRockScaleMax = 1.30f;

// How strongly each material wants a TREE rather than a rock. Weighted by the
// surface blend, so a gravel-to-grass transition gets a gradient of tree
// density instead of a line with forest on one side.
constexpr float kTreeBiasRock = 0.00f;
constexpr float kTreeBiasGravel = 0.35f;
constexpr float kTreeBiasGrass = 0.94f;
constexpr float kTreeBiasSand = 0.12f;

}  // namespace

const PropDims& prop_dims(PropKind k) {
    // Immutable table: a constant, not global mutable state.
    static constexpr PropDims kTable[kPropKindCount] = {
        //  radius  height  draw distance
        {1.9f, 6.4f, 340.0f},  // Tree - tall, so it stays visible a long way out
        {0.8f, 0.9f, 160.0f},  // Rock - small; drops out of the draw list early
    };
    const std::size_t i = prop_index(k);
    return kTable[i < kPropKindCount ? i : prop_index(PropKind::Tree)];
}

std::vector<ScatterProp> scatter_chunk(uint64_t seed, ChunkCoord coord) {
    std::vector<ScatterProp> out;

    const glm::vec2 origin = chunk_origin(coord);

    // Global cell index of this chunk's first cell. Keying the RNG to the
    // GLOBAL cell — not to a chunk-local index — is the whole trick: a prop is
    // a function of where it is in the world, so it is identical no matter
    // which chunk asks, in what order, or how many times.
    const int32_t base_cx = coord.x * kScatterCellsPerChunk;
    const int32_t base_cz = coord.z * kScatterCellsPerChunk;

    for (int jz = 0; jz < kScatterCellsPerChunk; ++jz) {
        for (int ix = 0; ix < kScatterCellsPerChunk; ++ix) {
            const int32_t cx = base_cx + static_cast<int32_t>(ix);
            const int32_t cz = base_cz + static_cast<int32_t>(jz);

            Rng rng = rng_at(seed, cx, cz, kChannelScatter);

            // FIXED PULL ORDER. Every draw below happens for every cell, even
            // ones that end up rejected, so that adding or removing a rejection
            // test cannot shift the stream for cells that were already fine.
            // Rolling lazily is how a one-line change silently re-scatters the
            // entire world.
            const float accept_roll = rng.next_float();
            const float jitter_x = rng.range(-kJitterFraction, kJitterFraction);
            const float jitter_z = rng.range(-kJitterFraction, kJitterFraction);
            const float kind_roll = rng.next_float();
            const float yaw_roll = rng.next_float();
            const float scale_roll = rng.next_float();
            const float variant_roll = rng.next_float();

            const float px = origin.x + (static_cast<float>(ix) + 0.5f +
                                         jitter_x) *
                                            kScatterCellMetres;
            const float pz = origin.y + (static_cast<float>(jz) + 0.5f +
                                         jitter_z) *
                                            kScatterCellMetres;

            // Classify against the SHADING normal and the field height: this
            // is a "what is the ground like here" question, and the smooth
            // field answers it better than one triangle's facet does.
            const SurfaceSample s = surface_at(seed, px, pz);

            if (s.height < kMinPropAltitudeMetres) continue;
            if (s.slope > kMaxPropSlope) continue;

            // Nothing grows in the road. Asks the same operator weight that
            // graded the road bed, so the mask cannot drift away from the
            // asphalt it is masking — and the corridor's feathered margin is
            // included, because a tree overhanging the kerb is the same
            // complaint as a tree in the carriageway.
            //
            // Scatter is seed-keyed per candidate, so rejecting here removes a
            // prop and moves none: the trees either side of a road sit exactly
            // where they sat before the road existed.
            if (city::road_corridor_weight(px, pz) > 0.0f) continue;

            // Density from the blend, not from the dominant material alone. A
            // point that is 51% gravel and 49% grass should be nearly as
            // wooded as one that is 49/51, and taking the dominant material
            // instead draws a hard edge through the middle of a hillside.
            float density = 0.0f;
            float tree_bias = 0.0f;
            for (std::size_t m = 0; m < kSurfaceCount; ++m) {
                const float w = s.weights[static_cast<glm::length_t>(m)];
                density += w * surface_properties(static_cast<Surface>(m))
                                   .scatter_density;
                switch (static_cast<Surface>(m)) {
                    case Surface::Rock:   tree_bias += w * kTreeBiasRock;   break;
                    case Surface::Gravel: tree_bias += w * kTreeBiasGravel; break;
                    case Surface::Grass:  tree_bias += w * kTreeBiasGrass;  break;
                    case Surface::Sand:   tree_bias += w * kTreeBiasSand;   break;
                }
            }

            // A district says how wooded it is. Without this, scatter's only
            // rejections were altitude, slope and a material roll — all of
            // which a flattened downtown plate passes, so the financial
            // district came out the most wooded place on the island at 31,143
            // props. `PropKit`/`density` on a district are STREET FURNITURE and
            // are a different system; `wild` is this one.
            density *= city::wild_scatter_at(px, pz);

            if (accept_roll >= density * kBaseDensity) continue;

            ScatterProp p;
            p.kind = kind_roll < tree_bias ? PropKind::Tree : PropKind::Rock;
            p.ground = s.dominant;
            p.yaw = yaw_roll * kTwoPi;

            if (p.kind == PropKind::Tree) {
                p.scale = kTreeScaleMin +
                          (kTreeScaleMax - kTreeScaleMin) * scale_roll;
                p.variant = static_cast<uint8_t>(
                    static_cast<int>(variant_roll *
                                     static_cast<float>(kTreeVariants)) %
                    kTreeVariants);
            } else {
                p.scale = kRockScaleMin +
                          (kRockScaleMax - kRockScaleMin) * scale_roll;
                p.variant = static_cast<uint8_t>(
                    static_cast<int>(variant_roll *
                                     static_cast<float>(kRockVariants)) %
                    kRockVariants);
            }

            // ON the surface the player can see. See ScatterProp::position.
            p.position = glm::vec3{px, mesh_height_at(seed, px, pz), pz};

            out.push_back(p);
        }
    }

    return out;
}

}  // namespace apricot
