#include "terrain/surface.h"

#include <cmath>

#include "terrain/heightmap.h"
#include "terrain/noise.h"

namespace apricot {
namespace {

// --- slope thresholds, in SINE of the slope angle ----------------------------
// Sine rather than degrees because recovering an angle needs acos(), and libm's
// inverse trig is not correctly rounded across platforms. sin = sqrt(1 - ny^2)
// is one IEEE-exact sqrt. Degrees are given so the numbers stay readable.
constexpr float kRockSlopeStart = 0.53f;  // ~32 deg: rock begins to show through
constexpr float kRockSlopeFull = 0.77f;   // ~50 deg: bare rock face

constexpr float kGravelSlopeStart = 0.31f;  // ~18 deg
constexpr float kGravelSlopeFull = 0.56f;   // ~34 deg

// --- altitude bands, in metres above sea level -------------------------------
// The beach is a band, not a line. Sand runs from below the water up to a few
// metres, faded out over the last stretch so the transition to grass is a dune
// and not a stripe.
constexpr float kBeachTopStart = 1.5f;
constexpr float kBeachTopEnd = 4.5f;

// Above the treeline the ground goes to scree regardless of how gentle it is.
constexpr float kAlpineStart = 52.0f;
constexpr float kAlpineFull = 74.0f;

// --- boundary variation -------------------------------------------------------
// The altitude used for BAND CLASSIFICATION ONLY is perturbed by a noise term.
// Without it the beach top and the treeline are exact contour lines running
// around the island at a constant altitude, which is the single most obvious
// tell that terrain was generated. The perturbation never touches the real
// height — geometry and physics use the true value.
constexpr float kBandVariationMetres = 45.0f;   // wavelength
constexpr float kBandVariationAmplitude = 2.6f; // +/- metres
constexpr int kBandVariationOctaves = 3;

}  // namespace

const SurfaceProperties& surface_properties(Surface s) {
    // Immutable table, so this is a constant, not global mutable state.
    static constexpr SurfaceProperties kTable[kSurfaceCount] = {
        //  scatter
        {0.05f},  // Rock   - no crags grow trees
        {0.25f},  // Gravel - scree holds the odd scrub
        {1.00f},  // Grass  - where everything grows
        {0.10f},  // Sand   - a beach holds almost nothing
    };
    const std::size_t i = surface_index(s);
    return kTable[i < kSurfaceCount ? i : surface_index(Surface::Grass)];
}

const char* surface_name(Surface s) {
    switch (s) {
        case Surface::Rock:   return "rock";
        case Surface::Gravel: return "gravel";
        case Surface::Grass:  return "grass";
        case Surface::Sand:   return "sand";
    }
    return "grass";
}

SurfaceSample classify_surface(uint64_t seed, float x, float z, float height,
                               const glm::vec3& normal) {
    SurfaceSample out;
    out.height = height;
    out.normal = normal;

    // sin of the slope angle from its cosine. Clamped before the sqrt because a
    // normal that is a hair over unit length from rounding would otherwise put
    // a tiny negative under the root and hand back a NaN that then propagates
    // silently into every weight downstream.
    const float ny = clamp01(std::fabs(normal.y));
    out.slope = std::sqrt(clamp01(1.0f - ny * ny));

    // Classification altitude: the true height nudged by a slow noise so the
    // band boundaries wander instead of drawing contour lines.
    const float band_h =
        height + (fbm(seed, x, z, kBandVariationMetres, kBandVariationOctaves,
                      kChannelSurface) -
                  0.5f) *
                     (2.0f * kBandVariationAmplitude);

    const float rock_f =
        smoothstep01(kRockSlopeStart, kRockSlopeFull, out.slope);
    const float slope_gravel =
        smoothstep01(kGravelSlopeStart, kGravelSlopeFull, out.slope);
    const float alpine = smoothstep01(kAlpineStart, kAlpineFull, band_h);
    const float beach = 1.0f - smoothstep01(kBeachTopStart, kBeachTopEnd, band_h);

    // Gravel appears either on middling slopes or above the treeline; take
    // whichever claim is stronger rather than adding them, or a steep alpine
    // face would count twice and push past its share.
    const float gravel_f = slope_gravel > alpine ? slope_gravel : alpine;

    // A CASCADE, not four scores normalised afterwards. Each material takes a
    // share of what the ones above it left, so the weights sum to exactly 1
    // with no division, and the order encodes priority:
    //
    //   rock   - a cliff is rock whatever its altitude. Wins outright.
    //   gravel - scree and alpine ground, on what rock did not take.
    //   sand   - the beach band, but only where it is not already scree.
    //   grass  - whatever is left. The default, so it can never be squeezed
    //            out by a rounding error and leave a vertex with no material.
    float remaining = 1.0f;

    const float w_rock = remaining * rock_f;
    remaining -= w_rock;

    const float w_gravel = remaining * gravel_f;
    remaining -= w_gravel;

    const float w_sand = remaining * beach;
    remaining -= w_sand;

    const float w_grass = remaining;

    out.weights = glm::vec4{w_rock, w_gravel, w_grass, w_sand};

    // Dominant = argmax, with ties broken by Surface order so the answer is
    // fully determined by the inputs. Physics reads this, and a grip value
    // that flickers between two materials on a boundary would be felt.
    Surface best = Surface::Rock;
    float best_w = w_rock;
    if (w_gravel > best_w) { best = Surface::Gravel; best_w = w_gravel; }
    if (w_grass > best_w)  { best = Surface::Grass;  best_w = w_grass; }
    if (w_sand > best_w)   { best = Surface::Sand; }
    out.dominant = best;

    return out;
}

SurfaceSample surface_at(uint64_t seed, float x, float z) {
    return classify_surface(seed, x, z, height_at(seed, x, z),
                            normal_at(seed, x, z));
}

Surface surface_kind_at(uint64_t seed, float x, float z) {
    return surface_at(seed, x, z).dominant;
}

}  // namespace apricot
