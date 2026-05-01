#include "world/heightmap.h"

#include <cmath>

namespace pengine {

namespace {

std::uint32_t g_seed = 0x9E3779B9u;

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

} // namespace

void Heightmap::set_seed(std::uint32_t seed) { g_seed = seed ^ 0x9E3779B9u; }

float Heightmap::sample(float wx, float wz) {
    float n = fbm(wx / FEATURE_SIZE, wz / FEATURE_SIZE);     // [0, 1]
    // Bias the distribution toward flatter ground with occasional hills.
    float biased = n * n * (3.f - 2.f * n);                  // smoothstep
    return (biased - 0.4f) * AMPLITUDE;                      // ~[-7, +11] m
}

glm::vec3 Heightmap::normal(float wx, float wz) {
    constexpr float e = 0.5f; // metres
    float hl = sample(wx - e, wz);
    float hr = sample(wx + e, wz);
    float hd = sample(wx, wz - e);
    float hu = sample(wx, wz + e);
    glm::vec3 n{ hl - hr, 2.f * e, hd - hu };
    return glm::normalize(n);
}

} // namespace pengine
