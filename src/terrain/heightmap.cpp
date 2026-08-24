#include "terrain/heightmap.h"

#include <cmath>

#include "city/terrain_ops.h"
#include "terrain/noise.h"

namespace apricot {

// The authored operators are blends toward a target, so every height they can
// produce lies between the field and that target. Assert the op table's legal
// target window sits inside the field's own analytic bounds and the two facts
// compose into a proof: operators cannot push a height outside the range that
// culling, streaming and the bounds test in terrain_determinism_tests rely on.
// city/terrain_ops.h asserts the other half — that every op target is inside
// that window — and the two modules do not include each other, so this is the
// one place both numbers are visible at once.
static_assert(city::kOpTargetFloorMetres >= kMinHeightMetres,
              "a terrain operator may carve below the height field's analytic "
              "minimum");
static_assert(city::kOpTargetCeilingMetres <= kMaxHeightMetres,
              "a terrain operator may mound above the height field's analytic "
              "maximum");

namespace {

// --- octave counts -----------------------------------------------------------
// The continental term only decides where the landmass is, so three octaves is
// plenty; spending more there buys detail that the hill and ridge terms are
// about to overwrite anyway.
constexpr int kContinentOctaves = 3;

// Six octaves off the 240 m base takes the finest hill detail down to 7.5 m.
// It used to reach 3 m off a 96 m base, which is about the size of a rut —
// right for a rally stage, and wrong for a street, where a 3 m undulation is a
// pothole in every carriageway. The octave count did not change; the base
// wavelength did, and the finest detail moved with it.
constexpr int kHillOctaves = 6;

constexpr int kRidgeOctaves = 5;
constexpr int kCoastOctaves = 3;
constexpr int kSeaFloorOctaves = 3;

// --- how the terms are mixed --------------------------------------------------
//
// LOWLAND plus gated MOUNTAIN, not an average of three fields. That structure
// is the whole difference between an island with a spine and a green pillow,
// and it was arrived at by measuring rather than by taste:
//
// Averaging three independent noise fields with comparable weights means their
// peaks never coincide, so the sum regresses hard to the middle and the world
// comes out uniformly medium. Measured over the island footprint that produced
// a 56 m maximum against a 117 m ceiling and not one face steeper than 45
// degrees. Making the mountain term ADDITIVE ON TOP of a gentle base, and
// gating it so it is genuinely absent over most of the map, gives the same
// average relief with peaks that actually arrive.
//
// The two spans sum to exactly 1, which is what keeps the field inside [0, 1]
// without a clamp. A clamp here would be a lie: it would flatten every peak in
// the world to the same altitude and read, from a distance, as a plateau biome
// nobody designed.
//
// Pinatty moves these from 0.42/0.58 to 0.30/0.70: flatter ordinary ground,
// with the relief concentrated where the spine gate switches it on. A city
// needs most of its land boring.
constexpr float kLowlandSpan = 0.30f;
constexpr float kMountainSpan = 0.70f;

// Split of the lowland term between "where the land is high" and "what the
// ground does underfoot".
//
// Weighted toward the continental term for Pinatty (was 0.55/0.45): less local
// bumpiness underfoot, which is what a street grid needs and what a rally
// stage did not.
constexpr float kLowlandContinentShare = 0.70f;
constexpr float kLowlandHillShare = 0.30f;

// Contrast applied to the continental term before it is used, for the reason
// spelled out on contrast() in noise.h: the raw field spans about [0.37, 0.86]
// rather than [0, 1], so an ungated threshold on it never resolves to "no
// mountain here" and the spines smear across the whole island.
constexpr float kContinentContrast = 2.4f;

// The ridged term is gated by the CONTRAST-EXPANDED continental term through
// this window: no spine at all below kSpineStart, full spine above kSpineFull.
// Mountains grow out of high ground. A ridge that erupts from a coastal flat
// reads as broken even to someone who could not say why.
//
// Gated harder for Pinatty (was 0.35/0.72), so the mountain is genuinely
// absent from about 90% of the map. One massif you can see from everywhere
// beats four you keep driving into.
constexpr float kSpineStart = 0.55f;
constexpr float kSpineFull = 0.85f;

// Sharpening applied to the ridged term: r^1.5, spelled `r * sqrt(r)`.
//
// Raising the exponent above 1 narrows the crests and widens the valleys,
// which both looks more like an eroded range and — because the same height now
// falls off over a shorter horizontal run — produces the genuinely steep faces
// the rock material needs something to classify.
//
// It is spelled as a multiply and a sqrt rather than std::pow(r, 1.5f) FOR
// DETERMINISM, and this is not paranoia. IEEE 754 requires sqrt to be
// correctly rounded, so it gives the same bits on every conforming platform.
// pow() carries no such requirement: implementations are free to be a fraction
// of an ulp apart, and every libm is. A last-bit difference here is a
// different world from the same seed, discovered as a desync months later.
inline float ridge_sharpen(float r) { return r * std::sqrt(r); }

// Wavelength of the noise that pushes the coastline in and out.
constexpr float kCoastWarpWavelengthMetres = 700.0f;

// Wavelength of the sea-floor swell.
constexpr float kSeaFloorWavelengthMetres = 340.0f;

// The normalised [0, 1] land shape BEFORE the island mask and before metres.
// Split out because island_mask() and height_at() both need it and because it
// is the one place the three terms meet.
float land_shape(uint64_t seed, float x, float z) {
    const float continent = contrast(
        fbm(seed, x, z, kContinentMetres, kContinentOctaves, kChannelContinent),
        kContinentContrast);
    const float hills =
        fbm(seed, x, z, kFeatureMetres, kHillOctaves, kChannelHills);
    const float ridges =
        ridged_fbm(seed, x, z, kRidgeMetres, kRidgeOctaves, kChannelRidge);

    const float lowland = kLowlandContinentShare * continent +
                          kLowlandHillShare * hills;

    const float spine = smoothstep01(kSpineStart, kSpineFull, continent);
    const float mountain = ridge_sharpen(ridges) * spine;

    const float shape = kLowlandSpan * lowland + kMountainSpan * mountain;

    // No spawn-lift dome. See the note where kHomeRadiusMetres used to be
    // declared: on an authored map the spawn is authored, and the dome sat
    // exactly under the financial district.
    return shape;
}

}  // namespace

float island_mask(uint64_t seed, float x, float z) {
    // Perturb the radius rather than the position. Warping the position would
    // also warp the hills, which is a different (and much stronger) effect;
    // all we want here is a coastline that wanders.
    const float warp = (fbm(seed, x, z, kCoastWarpWavelengthMetres,
                            kCoastOctaves, kChannelCoast) -
                        0.5f) *
                       (2.0f * kCoastWarpMetres);

    const float r = std::sqrt(x * x + z * z) + warp;
    const float d = r / kIslandRadiusMetres;

    // 1 well inside the coast, 0 at and beyond the island radius. Smoothstep
    // rather than a linear ramp so the shoreline meets the water tangentially
    // and beaches come out as beaches instead of as a chamfer.
    return 1.0f - smoothstep01(kShoreFalloffStart, 1.0f, d);
}

float height_at(uint64_t seed, float x, float z) {
    const float mask = island_mask(seed, x, z);

    // Platform first, THEN the mask. The order matters: the platform is what
    // the island is made of, so it has to fall away with the coastline like
    // everything else. Adding it after the mask would raise the open ocean by
    // the same amount and there would be no sea to bound the world with.
    const float shape =
        kIslandPlatform + (1.0f - kIslandPlatform) * land_shape(seed, x, z);
    const float shaped = shape * mask;

    // Map the normalised shape into metres about sea level. One linear map for
    // the whole range on purpose: a piecewise map with a different scale above
    // and below the water line puts a crease in the gradient at exactly y = 0,
    // which is precisely where the player is looking.
    float h = (shaped - kShoreLevel) * kHeightMetres;

    // Sea-floor relief, faded in as the island fades out so it never disturbs
    // the beach. Multiplied by (1 - mask) rather than added flat: on land the
    // term is exactly zero, so it cannot roughen a road surface.
    const float swell = fbm(seed, x, z, kSeaFloorWavelengthMetres,
                            kSeaFloorOctaves, kChannelSeaFloor) -
                        0.5f;
    h += (1.0f - mask) * kSeaFloorReliefMetres * swell;

    // THE AUTHORED TERRAIN OPERATORS, LAST, ON METRES.
    //
    // This is where the map stops being noise and starts being Pinatty: the
    // flat plate under the financial district, the harbour deep enough for a
    // ship, the channel the bridge crosses, the terraces on Ferrone Hill and
    // the graded corridor up the Shoulder. See src/city/terrain_ops.h.
    //
    // WHY LAST, AND WHY ON METRES. docs/design/pinatty.md section 1.3 puts the
    // operators inside the normalised shape instead —
    //
    //     h = map_to_metres( apply_ops( noise_shape(x, z), x, z ) )
    //
    // — and that ordering does not survive contact with the map. Two reasons,
    // and the second is the one that decides it:
    //
    //   * Targets would be authored in normalised shape units. A designer says
    //     "the dock apron is at 4.5 m", not "the dock apron is at 0.244 of the
    //     island's vertical span", and a Carve could not reach below sea level
    //     to a stated depth at all.
    //   * The island mask MULTIPLIES the shape. Applying an operator before the
    //     mask means the mask then scales the authored flat down — and worse,
    //     tilts it, because the mask has a gradient. Every flattened area in
    //     this map is near the coast: the dock apron, the Strand promenade, the
    //     Camber airfield on its spit, the causeway. A flatten inside the mask
    //     comes out neither flat nor at the height it was asked for, exactly
    //     where it matters most.
    //
    // Applying them here costs the operators the mask's protection at the very
    // edge of the world box, which is why every op target is asserted inside
    // the height field's own analytic bounds below rather than trusted.
    return city::apply_terrain_ops(h, x, z);
}

glm::vec3 normal_at(uint64_t seed, float x, float z) {
    // One metre either side. Small enough to track real slope, large enough
    // that the difference does not vanish into float noise on a gentle grade,
    // and matched to the one-metre spacing of the chunk mesh so the normal
    // describes the triangle the player is actually standing on rather than a
    // sub-triangle detail the mesh never represented.
    constexpr float kEps = 1.0f;

    const float hl = height_at(seed, x - kEps, z);
    const float hr = height_at(seed, x + kEps, z);
    const float hd = height_at(seed, x, z - kEps);
    const float hu = height_at(seed, x, z + kEps);

    // Cross product of the two tangents, written out: the gradient gives the
    // X and Z components directly and Y is the sample span. Y is positive by
    // construction, which is the invariant a height field must never break.
    return glm::normalize(glm::vec3{hl - hr, 2.0f * kEps, hd - hu});
}

}  // namespace apricot
