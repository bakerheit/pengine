#pragma once

#include <cmath>
#include <cstdint>

#include "core/rng.h"

namespace apricot {

// The procedural kernel shared by the height field, the surface classifier and
// the scatter placer. One definition, so a change to the noise moves all three
// together and they can never disagree about what the world looks like.
//
// Everything here is PURE and header-inline: no state, no statics, no clock,
// no allocation. Entropy comes from hash_coord3() and nowhere else.

// Floating-point contraction is switched OFF for every translation unit that
// includes this header, and that is load-bearing, not tidiness. With
// contraction on, the compiler is free to fuse `a * b + c` into a single
// rounding step; the fbm accumulator below is nothing BUT multiply-adds, so
// one machine fusing where another does not produces terrain that differs in
// the last bit — and a last-bit difference in a height is a car that lands a
// millimetre differently, then a metre differently, then a desynced replay.
// Determinism is the product, so we give up the fused multiply-add.
//
// Only clang is handled here, because clang is the only toolchain whose pragma
// for this is reliable — GCC's `#pragma GCC optimize` is documented as best
// effort and MSVC has no equivalent at all. On those, pass -ffp-contract=off
// (or /fp:precise) at the build level and re-pin the golden heights. A pragma
// that silently does nothing is worse than no pragma, so we do not pretend.
#if defined(__clang__)
#pragma clang fp contract(off)
#endif

// Channel bases for the independent noise fields. Spaced far apart on purpose:
// fbm() burns one consecutive channel per octave, so bases a few apart would
// have octave 3 of one field BE octave 0 of the next and two supposedly
// independent fields would share their detail.
inline constexpr uint32_t kChannelContinent = 0x0100u;
inline constexpr uint32_t kChannelHills     = 0x0200u;
inline constexpr uint32_t kChannelRidge     = 0x0300u;
inline constexpr uint32_t kChannelCoast     = 0x0400u;
inline constexpr uint32_t kChannelSeaFloor  = 0x0500u;
inline constexpr uint32_t kChannelSurface   = 0x0600u;
inline constexpr uint32_t kChannelScatter   = 0x0700u;

// Standard fBm shape. Lacunarity 2 and gain 0.5 give the 1/f spectrum real
// landscapes have; they are not arbitrary and are not worth "tuning".
inline constexpr float kLacunarity = 2.0f;
inline constexpr float kGain = 0.5f;

// Each octave is sampled on a plane rotated by half a radian. Value noise is
// built on an axis-aligned lattice, so stacking unrotated octaves lines every
// octave's grid up with every other one and the sum shows a square weave that
// is invisible in a heightmap preview and glaringly obvious on a lit hillside
// at a shallow sun angle. Rotating costs four multiplies per octave.
inline constexpr float kOctaveCos = 0.87758256189f;  // cos(0.5)
inline constexpr float kOctaveSin = 0.47942553860f;  // sin(0.5)

inline constexpr float clamp01(float v) {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

// Push a [0, 1] field away from its midpoint, clamping at the ends.
//
// This is not decoration, it is a correction for a real property of fbm. A sum
// of octaves is a sum of independent uniform variables, so by the central limit
// theorem the result piles up around 0.5 and almost never reaches either end —
// a "normalised to [0, 1]" fbm measured over real terrain spans more like
// [0.37, 0.86]. Feeding that straight into a threshold gives a gate that never
// fully opens, which reads in-game as "the mountains are missing" and in the
// code as a weight that looks perfectly reasonable.
inline constexpr float contrast(float v, float amount) {
    return clamp01((v - 0.5f) * amount + 0.5f);
}

// Hermite interpolation between two edges, clamped outside them. Used for
// every soft threshold in terrain generation, because a hard `if` on a
// continuous field draws a visible contour line across the landscape.
inline float smoothstep01(float edge0, float edge1, float v) {
    const float span = edge1 - edge0;
    if (span <= 0.0f) return v < edge0 ? 0.0f : 1.0f;
    const float t = clamp01((v - edge0) / span);
    return t * t * (3.0f - 2.0f * t);
}

// One deterministic value in [0, 1) per integer lattice point, per channel.
inline float lattice_value(uint64_t seed, int32_t ix, int32_t iz,
                           uint32_t channel) {
    return static_cast<float>(hash_coord3(seed, ix, iz, channel) >> 40) *
           (1.0f / 16777216.0f);
}

// Perlin's quintic fade. Smoothstep (3t^2-2t^3) has a discontinuous second
// derivative at the lattice points, which shows up as faint grid-aligned
// creases across a lit hillside. This costs two extra multiplies and removes
// them.
inline constexpr float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

// Bilinear value noise on the integer lattice, in [0, 1).
//
// Coordinates are in LATTICE units, not metres: divide by a wavelength before
// calling. Keeping the conversion at the call site is what lets fbm() scale
// between octaves without re-deriving a metre figure each time.
inline float value_noise(uint64_t seed, float x, float z, uint32_t channel) {
    const float fx = std::floor(x);
    const float fz = std::floor(z);
    const int32_t ix = static_cast<int32_t>(fx);
    const int32_t iz = static_cast<int32_t>(fz);

    const float tx = fade(x - fx);
    const float tz = fade(z - fz);

    const float n00 = lattice_value(seed, ix,     iz,     channel);
    const float n10 = lattice_value(seed, ix + 1, iz,     channel);
    const float n01 = lattice_value(seed, ix,     iz + 1, channel);
    const float n11 = lattice_value(seed, ix + 1, iz + 1, channel);

    const float a = n00 + (n10 - n00) * tx;
    const float b = n01 + (n11 - n01) * tx;
    return a + (b - a) * tz;
}

// Fractal Brownian motion, normalised to [0, 1].
//
// `wavelength` is the metre span of the FIRST octave; each subsequent octave
// is kLacunarity times finer at kGain the amplitude. The result is divided by
// the amplitude sum rather than by a magic number so adding an octave cannot
// silently change the overall range.
inline float fbm(uint64_t seed, float x, float z, float wavelength,
                 int octaves, uint32_t channel) {
    float px = x / wavelength;
    float pz = z / wavelength;

    float sum = 0.0f;
    float amplitude = 1.0f;
    float norm = 0.0f;

    for (int o = 0; o < octaves; ++o) {
        sum += amplitude *
               value_noise(seed, px, pz, channel + static_cast<uint32_t>(o));
        norm += amplitude;
        amplitude *= kGain;

        const float rx = px * kOctaveCos - pz * kOctaveSin;
        const float rz = px * kOctaveSin + pz * kOctaveCos;
        px = rx * kLacunarity;
        pz = rz * kLacunarity;
    }

    return norm > 0.0f ? sum / norm : 0.0f;
}

// Weighted ridged fBm, normalised to [0, 1].
//
// Two things make this look like a mountain range rather than like fBm with
// creases in it:
//
//   * The fold `1 - |2v - 1|` turns each octave's mid-value contour into a
//     sharp crest, then squaring it narrows the crest and flattens the
//     valleys, which is roughly what erosion does to a range.
//   * Each octave is WEIGHTED by the previous one, so fine detail only appears
//     where a coarse ridge already exists. Without the weighting, high-
//     frequency creases cover the valley floors too and the whole field reads
//     as noise rather than as terrain that water ran off.
inline float ridged_fbm(uint64_t seed, float x, float z, float wavelength,
                        int octaves, uint32_t channel) {
    float px = x / wavelength;
    float pz = z / wavelength;

    float sum = 0.0f;
    float amplitude = 1.0f;
    float norm = 0.0f;
    float weight = 1.0f;

    for (int o = 0; o < octaves; ++o) {
        const float v =
            value_noise(seed, px, pz, channel + static_cast<uint32_t>(o));

        float ridge = 1.0f - std::fabs(v * 2.0f - 1.0f);
        ridge = ridge * ridge * weight;

        // Feed the crest forward, amplified then clamped: amplification lets a
        // strong ridge carry full detail, the clamp stops the carry compounding
        // above 1 and pushing the sum out of its normalised range.
        weight = clamp01(ridge * 2.0f);

        sum += amplitude * ridge;
        norm += amplitude;
        amplitude *= kGain;

        const float rx = px * kOctaveCos - pz * kOctaveSin;
        const float rz = px * kOctaveSin + pz * kOctaveCos;
        px = rx * kLacunarity;
        pz = rz * kLacunarity;
    }

    return norm > 0.0f ? sum / norm : 0.0f;
}

}  // namespace apricot
