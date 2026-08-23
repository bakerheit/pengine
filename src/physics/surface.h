#pragma once

#include <cstddef>
#include <cstdint>

#include <glm/glm.hpp>

namespace apricot {

// What a tyre is standing on, and how much it can hold.
//
// The tyre model never reads a material directly — it reads the GRIP that
// comes out of this table. Keeping the lookup here rather than inside the
// vehicle means "sand is slippery" is one number in one place instead of a
// branch buried in the force loop, and it means the terrain side can gain a
// fifth material without the vehicle knowing.

// Ordered loosest-to-grippiest is tempting but wrong: these values are
// SERIALISED into replay-adjacent tuning and debug output, so the numbers are
// the contract. Append only.
enum class SurfaceMaterial : uint8_t {
    kRock = 0,     // hardpack, bedrock, the fast stuff
    kGravel = 1,   // classic rally loose surface
    kGrass = 2,    // verges and meadows
    kSand = 3,     // riverbeds and dunes; the one that eats momentum
};

inline constexpr std::size_t kSurfaceMaterialCount = 4;

struct SurfaceProperties {
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

// The whole feel of a surface, in one row each.
inline constexpr SurfaceProperties kSurfaceTable[kSurfaceMaterialCount] = {
    /* kRock   */ {1.15f, 0.82f, 1.00f},
    /* kGravel */ {0.95f, 0.88f, 1.35f},
    /* kGrass  */ {0.72f, 0.46f, 1.60f},
    /* kSand   */ {0.55f, 0.48f, 3.20f},
};

inline constexpr const SurfaceProperties& surface_properties(SurfaceMaterial m) {
    const std::size_t i = static_cast<std::size_t>(m);
    // Clamped rather than asserted: a material that arrives out of range from
    // a future terrain module should degrade to rock, not read off the end of
    // the table in a release build where the assert is gone.
    return kSurfaceTable[i < kSurfaceMaterialCount ? i : 0u];
}

// Peak friction coefficient for a material at a given wetness in [0, 1].
// Linear between the dry and wet rows — a puddle is not a phase change, and a
// curve here would only be a second thing to tune.
inline float surface_grip(SurfaceMaterial m, float wetness) {
    const SurfaceProperties& p = surface_properties(m);
    const float w = glm::clamp(wetness, 0.0f, 1.0f);
    return p.dry_grip + (p.wet_grip - p.dry_grip) * w;
}

// Rolling-resistance multiplier. Wetness does not change it: a wet surface is
// slipperier, not stickier, and folding both effects into one number would
// make the two impossible to tune apart.
inline float surface_rolling_scale(SurfaceMaterial m) {
    return surface_properties(m).rolling_scale;
}

}  // namespace apricot
