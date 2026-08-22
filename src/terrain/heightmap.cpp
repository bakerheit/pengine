#include "terrain/heightmap.h"

#include <cmath>

#include "core/rng.h"

namespace apricot {
namespace {

// One deterministic value in [0, 1) per integer lattice point.
float lattice_value(uint64_t seed, int32_t ix, int32_t iz) {
    return static_cast<float>(hash_coord(seed, ix, iz) >> 40) *
           (1.0f / 16777216.0f);
}

// Perlin's quintic fade. Smoothstep (3t^2-2t^3) has a discontinuous second
// derivative at the lattice points, which shows up as faint grid-aligned
// creases across a lit hillside. This costs two extra multiplies and removes
// them.
float fade(float t) { return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f); }

}  // namespace

float height_at(uint64_t seed, float x, float z) {
    const float sx = x / kFeatureMetres;
    const float sz = z / kFeatureMetres;

    const float fx = std::floor(sx);
    const float fz = std::floor(sz);
    const int32_t ix = static_cast<int32_t>(fx);
    const int32_t iz = static_cast<int32_t>(fz);

    const float tx = fade(sx - fx);
    const float tz = fade(sz - fz);

    const float n00 = lattice_value(seed, ix,     iz);
    const float n10 = lattice_value(seed, ix + 1, iz);
    const float n01 = lattice_value(seed, ix,     iz + 1);
    const float n11 = lattice_value(seed, ix + 1, iz + 1);

    const float a = n00 + (n10 - n00) * tx;
    const float b = n01 + (n11 - n01) * tx;

    // Centred on zero so sea level is y = 0 rather than half the amplitude.
    return ((a + (b - a) * tz) - 0.5f) * kHeightMetres;
}

glm::vec3 normal_at(uint64_t seed, float x, float z) {
    // One metre either side. Small enough to track real slope, large enough
    // that the difference does not vanish into float noise on a gentle grade.
    constexpr float kEps = 1.0f;

    const float hl = height_at(seed, x - kEps, z);
    const float hr = height_at(seed, x + kEps, z);
    const float hd = height_at(seed, x, z - kEps);
    const float hu = height_at(seed, x, z + kEps);

    // Cross product of the two tangents, written out: the gradient gives the
    // X and Z components directly and Y is the sample span.
    return glm::normalize(glm::vec3{hl - hr, 2.0f * kEps, hd - hu});
}

}  // namespace apricot
