#include "world/heightmap.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

#include "core/log.h"
#include "world/road_grid.h"

#include "stb_image.h"
#include "stb_image_write.h"

namespace pengine {

namespace {

// Cached samples (row-major, z-major). Initialised by Heightmap::init().
std::vector<float> g_heights;
int                g_resolution  = 0;
float              g_world_size  = 0.f;

inline float smoothstep01(float t) {
    t = std::clamp(t, 0.f, 1.f);
    return t * t * (3.f - 2.f * t);
}

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// ---------------------------------------------------------------------------
// Cincinnati starter field (analytic).
//
// World layout, "inspired-not-literal":
//
//   z = 0 ────────────── north plateau (Clifton/UC) ───────────────────
//                                                                     │
//   z ≈ 3000 ─── basin floor (downtown + OTR) ────────────── Mt. Adams│
//                                                              bluff  │
//   z ≈ 5000 ── Ohio River (sinuous, E–W) ─────────────────────────────
//   z ≈ 6000 ── Covington / Newport basin ─────────────────────────────
//   z = 8192 ── Northern Kentucky hills ───────────────────────────────
//
//   x = 0 ── Price Hill ── basin ── ─ Mt. Adams ─── eastern hills ── x = 8192
// ---------------------------------------------------------------------------
float cincinnati_field(float wx, float wz) {
    // River centreline meanders sinusoidally across the world's southern half.
    constexpr float WORLD       = 8192.f;
    constexpr float RIVER_BASE  = 5500.f;
    constexpr float RIVER_AMP   = 600.f;
    constexpr float RIVER_HALF  = 220.f;   // half-width of the trough
    constexpr float BANK_TOP    = 6.f;     // bank height above river surface
    constexpr float RIVER_FLOOR = -6.f;    // bed below river surface

    float river_z      = RIVER_BASE + RIVER_AMP * std::sin(wx / WORLD * 4.0f);
    float dist_signed  = wz - river_z;          // <0 = north of river
    float abs_river    = std::abs(dist_signed);

    // Cross-section through the trough: U-shape lerped from floor to bank.
    float trough_t   = smoothstep01(abs_river / RIVER_HALF);
    float trough_y   = lerp(RIVER_FLOOR, BANK_TOP, trough_t);

    // Basin shoulders rise gently away from the banks.
    float basin_north = lerp(BANK_TOP, 12.f,
                              smoothstep01((-dist_signed - RIVER_HALF) / 1500.f));
    float basin_south = lerp(BANK_TOP, 14.f,
                              smoothstep01(( dist_signed - RIVER_HALF) / 1500.f));

    float ground;
    if (abs_river <= RIVER_HALF)        ground = trough_y;
    else if (dist_signed < 0.f)         ground = basin_north;
    else                                ground = basin_south;

    // Hills add additively above basin level. North-of-river only for
    // Cincinnati-side hills; south-of-river for Kentucky hills.
    bool north = dist_signed < 0.f;

    if (north) {
        // Mt. Adams: eastern bluff. Sharp transition centred at x ≈ 5200.
        float adams_t = smoothstep01((wx - 4800.f) / 700.f);
        ground += adams_t * 70.f;

        // Eastern hills beyond Mt. Adams: gentle rolling beyond x ≈ 6500.
        float east_t = smoothstep01((wx - 6500.f) / 1200.f);
        ground += east_t * 25.f;

        // Clifton/UC plateau: northern, rises from z ≈ 3200 toward z = 0.
        float clifton_t = smoothstep01((3200.f - wz) / 1100.f);
        ground += clifton_t * 60.f;

        // Price Hill: western bluff, rises from x ≈ 2400 toward x = 0.
        float price_t = smoothstep01((2400.f - wx) / 800.f);
        ground += price_t * 55.f;
    } else {
        // Northern Kentucky hills, beyond Covington basin.
        float ky_t = smoothstep01((dist_signed - 1500.f) / 1500.f);
        ground += ky_t * 50.f;

        // Slight east-side rise (mirrors the Cincinnati side faintly).
        float ky_east_t = smoothstep01((wx - 5500.f) / 1500.f);
        ground += ky_east_t * 15.f;
    }

    return ground;
}

bool load_png(const std::filesystem::path& path) {
    int w = 0, h = 0, channels = 0;
    unsigned char* data = stbi_load(path.string().c_str(), &w, &h, &channels, 1);
    if (!data) return false;
    if (w != h) {
        PE_WARN("Heightmap: PNG '%s' is %dx%d (expected square); using min dim",
                path.string().c_str(), w, h);
    }
    int n = std::min(w, h);
    g_resolution = n;
    size_t res = static_cast<size_t>(n);
    size_t row_w = static_cast<size_t>(w);
    g_heights.assign(res * res, 0.f);
    for (size_t z = 0; z < res; ++z) {
        for (size_t x = 0; x < res; ++x) {
            float t = data[z * row_w + x] / 255.f;
            g_heights[z * res + x] =
                Heightmap::HEIGHT_MIN_M +
                t * (Heightmap::HEIGHT_MAX_M - Heightmap::HEIGHT_MIN_M);
        }
    }
    stbi_image_free(data);
    return true;
}

bool bake_starter_png(const std::filesystem::path& path, int resolution) {
    g_resolution = resolution;
    size_t res = static_cast<size_t>(resolution);
    g_heights.assign(res * res, 0.f);
    std::vector<unsigned char> bytes(res * res);

    for (size_t z = 0; z < res; ++z) {
        for (size_t x = 0; x < res; ++x) {
            float wx = (static_cast<float>(x) + 0.5f) /
                       static_cast<float>(res) * g_world_size;
            float wz = (static_cast<float>(z) + 0.5f) /
                       static_cast<float>(res) * g_world_size;
            float h  = cincinnati_field(wx, wz);
            g_heights[z * res + x] = h;

            float t = (h - Heightmap::HEIGHT_MIN_M) /
                      (Heightmap::HEIGHT_MAX_M - Heightmap::HEIGHT_MIN_M);
            t = std::clamp(t, 0.f, 1.f);
            bytes[z * res + x] =
                static_cast<unsigned char>(t * 255.f + 0.5f);
        }
    }

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (!stbi_write_png(path.string().c_str(),
                        resolution, resolution, 1,
                        bytes.data(), resolution)) {
        PE_WARN("Heightmap: failed to write starter PNG '%s'",
                path.string().c_str());
        return false;
    }
    PE_INFO("Heightmap: baked starter PNG %dx%d -> %s",
            resolution, resolution, path.string().c_str());
    return true;
}

}  // namespace

bool Heightmap::init(float world_size_m, const std::filesystem::path& png_path) {
    g_world_size = world_size_m;

    if (std::filesystem::exists(png_path)) {
        if (load_png(png_path)) {
            PE_INFO("Heightmap: loaded %dx%d from %s",
                    g_resolution, g_resolution, png_path.string().c_str());
            return true;
        }
        PE_WARN("Heightmap: '%s' exists but failed to load; baking starter",
                png_path.string().c_str());
    }

    return bake_starter_png(png_path, /*resolution=*/ 256);
}

float Heightmap::raw_sample(float wx, float wz) {
    if (g_heights.empty() || g_world_size <= 0.f) return 0.f;

    // Pixel grid: pixel centres lie at (i + 0.5, j + 0.5) of the resolution
    // grid, mapped linearly to world coordinates.
    float fx = wx / g_world_size * static_cast<float>(g_resolution) - 0.5f;
    float fz = wz / g_world_size * static_cast<float>(g_resolution) - 0.5f;

    int x0 = static_cast<int>(std::floor(fx));
    int z0 = static_cast<int>(std::floor(fz));
    int x1 = x0 + 1;
    int z1 = z0 + 1;
    float tx = fx - static_cast<float>(x0);
    float tz = fz - static_cast<float>(z0);

    int max = g_resolution - 1;
    x0 = std::clamp(x0, 0, max);
    x1 = std::clamp(x1, 0, max);
    z0 = std::clamp(z0, 0, max);
    z1 = std::clamp(z1, 0, max);

    size_t res  = static_cast<size_t>(g_resolution);
    size_t sx0  = static_cast<size_t>(x0);
    size_t sx1  = static_cast<size_t>(x1);
    size_t sz0  = static_cast<size_t>(z0);
    size_t sz1  = static_cast<size_t>(z1);
    float h00 = g_heights[sz0 * res + sx0];
    float h10 = g_heights[sz0 * res + sx1];
    float h01 = g_heights[sz1 * res + sx0];
    float h11 = g_heights[sz1 * res + sx1];
    float h0  = lerp(h00, h10, tx);
    float h1  = lerp(h01, h11, tx);
    return lerp(h0, h1, tz);
}

float Heightmap::sample(float wx, float wz) {
    RoadBandClass c = classify_road_band(wx, wz);

    // Partition-of-unity smooth snap. s_x peaks at 1 on the NS centerline and
    // fades linearly to 0 at the NS band edge; s_z is the EW analogue.
    //
    //   f_ns_only = s_x · (1 − s_z)   weight for raw(ns_x, wz)
    //   f_ew_only = s_z · (1 − s_x)   weight for raw(wx, ew_z)
    //   f_other   = 1 − f_ns_only − f_ew_only   weight for raw(wx, wz)
    //
    // The three weights sum to 1 by algebraic identity. Properties this
    // gives us:
    //   • Along EW centerline (s_z = 1): all three reference samples equal
    //     raw(wx, ew_z), so the carving is invisible — driving along the EW
    //     road never feels a bump as you cross intersections.
    //   • Inside an intersection (both s_x≈1 and s_z≈1): f_ns_only and
    //     f_ew_only both go to zero, f_other dominates. The heightmap
    //     collapses to raw(wx, wz) — no plateau hump for sidewalk corners
    //     at the intersection band edges to inherit.
    //   • At a band edge (s_x = 0 or s_z = 0): the corresponding "only"
    //     weight drops to zero, leaving a smooth blend with the off-road
    //     case — the carved field is C0-continuous everywhere.
    float s_x = c.in_ns ? std::max(0.f, 1.f - std::abs(wx - c.ns_x) / ROAD_HALF_WIDTH) : 0.f;
    float s_z = c.in_ew ? std::max(0.f, 1.f - std::abs(wz - c.ew_z) / ROAD_HALF_WIDTH) : 0.f;

    float f_ns_only = s_x * (1.f - s_z);
    float f_ew_only = s_z * (1.f - s_x);
    float f_other   = 1.f - f_ns_only - f_ew_only;

    float h = f_other * raw_sample(wx, wz);
    if (f_ns_only > 0.f) h += f_ns_only * raw_sample(c.ns_x, wz);
    if (f_ew_only > 0.f) h += f_ew_only * raw_sample(wx, c.ew_z);
    return h;
}

glm::vec3 Heightmap::normal(float wx, float wz) {
    // The carved heightmap is now continuous across band edges (the
    // intersection blend smooths the previous sharp curb), so plain central
    // differences are safe — no need for one-sided gradient detection.
    constexpr float e = 0.5f;
    float hl = sample(wx - e, wz);
    float hr = sample(wx + e, wz);
    float hd = sample(wx, wz - e);
    float hu = sample(wx, wz + e);
    glm::vec3 n{ hl - hr, 2.f * e, hd - hu };
    return glm::normalize(n);
}

int   Heightmap::resolution()   { return g_resolution; }
float Heightmap::world_size_m() { return g_world_size; }

}  // namespace pengine
