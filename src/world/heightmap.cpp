#include "world/heightmap.h"

#include <algorithm>
#include <cmath>

namespace pengine {

namespace {

std::uint32_t g_seed = 0x9E3779B9u;

// Cell layout: each streaming cell flattens to its mean noise height. The
// inner 224 m of each 256 m cell is dead flat; the outer 16 m smoothly ramps
// to the neighbour's mean. Result: drivable, grid-friendly terraces with
// gentle slope transitions at boundaries.
constexpr float CELL_SIZE   = 256.f;
constexpr float RAMP_WIDTH  = 16.f;

inline std::uint32_t hash_u32(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352du;
    x ^= x >> 15;
    x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}

inline float hash01(int xi, int zi) {
    std::uint32_t h = hash_u32(static_cast<std::uint32_t>(xi) * 0x27d4eb2du
                             ^ static_cast<std::uint32_t>(zi) * 0x165667b1u
                             ^ g_seed);
    return static_cast<float>(h & 0xFFFFFFu) / static_cast<float>(0x1000000);
}

inline float smoothstep01(float t) { return t * t * (3.f - 2.f * t); }
inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

float value_noise_2d(float x, float z) {
    float fx = std::floor(x), fz = std::floor(z);
    int   xi = static_cast<int>(fx), zi = static_cast<int>(fz);
    float tx = x - fx,            tz = z - fz;
    float u  = smoothstep01(tx),  v  = smoothstep01(tz);

    float a = hash01(xi,     zi);
    float b = hash01(xi + 1, zi);
    float c = hash01(xi,     zi + 1);
    float d = hash01(xi + 1, zi + 1);
    return lerp(lerp(a, b, u), lerp(c, d, u), v);
}

float fbm(float x, float z) {
    float total = 0.f, amp = 1.f, freq = 1.f, norm = 0.f;
    for (int i = 0; i < 4; ++i) {
        total += value_noise_2d(x * freq, z * freq) * amp;
        norm  += amp;
        amp   *= 0.5f;
        freq  *= 2.f;
    }
    return total / norm; // [0, 1]
}

// Mean ground height for the cell (i, j). Noise sampled at cell centre.
float cell_mean(int i, int j) {
    float cx = (static_cast<float>(i) + 0.5f) * CELL_SIZE;
    float cz = (static_cast<float>(j) + 0.5f) * CELL_SIZE;
    float n  = fbm(cx / Heightmap::FEATURE_SIZE, cz / Heightmap::FEATURE_SIZE);
    float biased = n * n * (3.f - 2.f * n);
    return (biased - 0.4f) * Heightmap::AMPLITUDE;
}

} // namespace

void Heightmap::set_seed(std::uint32_t seed) { g_seed = seed ^ 0x9E3779B9u; }

float Heightmap::sample(float wx, float wz) {
    int xi = static_cast<int>(std::floor(wx / CELL_SIZE));
    int zi = static_cast<int>(std::floor(wz / CELL_SIZE));

    // Position within cell, [0, CELL_SIZE].
    float lx = wx - static_cast<float>(xi) * CELL_SIZE;
    float lz = wz - static_cast<float>(zi) * CELL_SIZE;

    // Distance from cell centre on each axis. If within (CELL/2 - RAMP/2) of
    // centre on both axes we're in the flat interior — early out.
    float centre = CELL_SIZE * 0.5f;
    float flat_radius = (CELL_SIZE - 2.f * RAMP_WIDTH) * 0.5f; // = 112 if 256 / 16
    float dx = lx - centre;
    float dz = lz - centre;
    float ax = std::abs(dx);
    float az = std::abs(dz);

    if (ax <= flat_radius && az <= flat_radius) {
        return cell_mean(xi, zi);
    }

    // Ramping. Each axis: tx = blend factor toward neighbour.
    float tx = 0.f, tz = 0.f;
    int   dxi = 0, dzi = 0;
    if (ax > flat_radius) {
        tx  = std::min(1.f, (ax - flat_radius) / RAMP_WIDTH);
        tx  = smoothstep01(tx);
        dxi = (dx > 0.f) ? 1 : -1;
    }
    if (az > flat_radius) {
        tz  = std::min(1.f, (az - flat_radius) / RAMP_WIDTH);
        tz  = smoothstep01(tz);
        dzi = (dz > 0.f) ? 1 : -1;
    }

    float h00 = cell_mean(xi,        zi);
    float h10 = cell_mean(xi + dxi,  zi);
    float h01 = cell_mean(xi,        zi + dzi);
    float h11 = cell_mean(xi + dxi,  zi + dzi);
    float h0  = lerp(h00, h10, tx);
    float h1  = lerp(h01, h11, tx);
    return lerp(h0, h1, tz);
}

glm::vec3 Heightmap::normal(float wx, float wz) {
    constexpr float e = 0.5f;
    float hl = sample(wx - e, wz);
    float hr = sample(wx + e, wz);
    float hd = sample(wx, wz - e);
    float hu = sample(wx, wz + e);
    glm::vec3 n{ hl - hr, 2.f * e, hd - hu };
    return glm::normalize(n);
}

} // namespace pengine
