#pragma once

#include <cstddef>

#include <glm/glm.hpp>

#include "terrain/surface.h"

namespace apricot {

// How a tyre experiences a surface.
//
// WHAT THE GROUND IS belongs to terrain: `Surface`, in terrain/surface.h, is
// the one material enum in the engine and terrain is what draws it. WHAT A
// MATERIAL GRIPS LIKE belongs here, because it is a property of the tyre model
// and a second vehicle scales it rather than redefining it.
//
// physics used to own a parallel `SurfaceMaterial` enum with its own
// classifier, and the two drifted exactly the way that always goes: measured
// over 11,559 land samples in the home basin, the two classifiers named a
// DIFFERENT material 40.85% of the time, and physics never once returned sand
// anywhere on the island — every beach in the game gripped like grass. See
// PENG-40. That is why there is one enum now and it is not this module's.
//
// The tyre model never reads a material directly — it reads the GRIP that
// comes out of this table. Keeping the lookup here rather than inside the
// vehicle means "sand is slippery" is one number in one place instead of a
// branch buried in the force loop, and it means terrain can gain a fifth
// material without the vehicle knowing.

struct TyreSurface {
    // Peak friction coefficient (mu) the tyre can reach on this surface.
    float dry_grip;

    // Same, soaked. Every entry is <= dry_grip so "rain makes it worse" holds
    // everywhere. Real rally gravel is a nuisance to this rule — a damp gravel
    // stage keys up and grips BETTER than a dry, loose one — so gravel's wet
    // penalty is deliberately the smallest of the four rather than inverted,
    // which would make "grip drops in rain" false for a quarter of the world.
    float wet_grip;

    // Multiplier on the vehicle's rolling resistance. Sand does not just
    // slide, it drags: that is why a car bogs in it instead of merely
    // understeering across it.
    float rolling_scale;
};

// The whole feel of a surface, in one row each. Indexed by Surface, so the row
// order is terrain's enum order — rock, gravel, grass, sand — and inserting a
// material in the middle of that enum re-tunes the car as well as re-texturing
// the world. Append only, on both sides.
inline constexpr TyreSurface kTyreSurfaceTable[kSurfaceCount] = {
    /* Rock   */ {1.15f, 0.82f, 1.00f},
    /* Gravel */ {0.95f, 0.88f, 1.35f},
    /* Grass  */ {0.72f, 0.46f, 1.60f},
    /* Sand   */ {0.55f, 0.48f, 3.20f},
};

inline constexpr const TyreSurface& tyre_surface(Surface s) {
    const std::size_t i = surface_index(s);
    // Clamped rather than asserted: a material that arrives out of range from
    // a future terrain module should degrade to rock, not read off the end of
    // the table in a release build where the assert is gone.
    return kTyreSurfaceTable[i < kSurfaceCount ? i : 0u];
}

// Peak friction coefficient for a material at a given wetness in [0, 1].
// Linear between the dry and wet rows — a puddle is not a phase change, and a
// curve here would only be a second thing to tune.
inline float surface_grip(Surface s, float wetness) {
    const TyreSurface& p = tyre_surface(s);
    const float w = glm::clamp(wetness, 0.0f, 1.0f);
    return p.dry_grip + (p.wet_grip - p.dry_grip) * w;
}

// Rolling-resistance multiplier. Wetness does not change it: a wet surface is
// slipperier, not stickier, and folding both effects into one number would
// make the two impossible to tune apart.
inline float surface_rolling_scale(Surface s) {
    return tyre_surface(s).rolling_scale;
}

}  // namespace apricot
