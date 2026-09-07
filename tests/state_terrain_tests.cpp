// State-scale terrain: O'Haven remains the exact original island while the
// separate southeast landmass reads as low, tapered Florangia.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "city/map.h"
#include "city/states.h"
#include "terrain/heightmap.h"
#include "terrain/scatter.h"
#include "terrain/surface.h"
#include "test_assert.h"

using namespace apricot;
using namespace apricot::city;

namespace {

constexpr uint64_t kSeed = kMapSeed;

uint32_t bits(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

void state_catalog_and_world_box_are_authored() {
    REQUIRE(kStateCount == 2);
    REQUIRE(std::strcmp(state_name(StateId::OHaven), "O'Haven") == 0);
    REQUIRE(std::strcmp(state_name(StateId::Florangia), "Florangia") == 0);
    REQUIRE(kWorldHalfMetres == 10240.0f);
    REQUIRE(std::fmod(kWorldHalfMetres, 64.0f) == 0.0f);
    REQUIRE(std::fmod(kWorldHalfMetres, 128.0f) == 0.0f);

    const State& old_state = state(StateId::OHaven);
    const State& new_state = state(StateId::Florangia);
    REQUIRE(height_at(kSeed, old_state.map_label_anchor.x,
                      old_state.map_label_anchor.z) > 0.0f);
    REQUIRE(height_at(kSeed, new_state.map_label_anchor.x,
                      new_state.map_label_anchor.z) > 0.0f);
    REQUIRE(new_state.map_label_anchor.x > old_state.map_label_anchor.x);
    REQUIRE(new_state.map_label_anchor.z > old_state.map_label_anchor.z);

    apricot_test::pass("two named state labels sit on their separate landmasses");
}

void ohaven_stays_on_the_legacy_bit_path() {
    // Florangia is exactly absent throughout the whole former world box and
    // its one-metre normal stencil. That keeps height, normal, and material
    // samples on the legacy path at the old boundary too.
    for (int z = -3072; z <= 3072; z += 32) {
        for (int x = -3072; x <= 3072; x += 32) {
            REQUIRE(bits(florangia_mask(kSeed, static_cast<float>(x),
                                        static_cast<float>(z))) == 0u);
        }
    }
    for (int x = -3072; x <= 3072; x += 32) {
        REQUIRE(bits(florangia_mask(kSeed, static_cast<float>(x),
                                    3073.0f)) == 0u);
    }
    REQUIRE_MSG(bits(normal_at(kSeed, 2350.0f, 3072.0f).z) == 0x3C2E0554u,
                "O'Haven south-edge normal sampled Florangia",
                "legacy terrain boundary");
    const SurfaceSample boundary = surface_at(kSeed, 2350.0f, 3072.0f);
    REQUIRE(bits(boundary.height) == 0xC2294B39u);
    REQUIRE(bits(boundary.slope) == 0x3C336BDAu);
    REQUIRE(boundary.dominant == Surface::Sand);
    REQUIRE(bits(boundary.weights.x) == 0u);
    REQUIRE(bits(boundary.weights.y) == 0u);
    REQUIRE(bits(boundary.weights.z) == 0u);
    REQUIRE(bits(boundary.weights.w) == 0x3F800000u);

    struct Golden { uint64_t seed; float x, z; uint32_t h; };
    const Golden heights[] = {
        {0ull, 0.0f, 0.0f, 0x41400000u},
        {1ull, 100.0f, -250.0f, 0x41400000u},
        {3735928559ull, -800.0f, -1650.0f, 0x41100000u},
        {12648430ull, 37.25f, 991.75f, 0x41600000u},
        {0ull, -1500.0f, 900.0f, 0x42C2B676u},
        {1ull, 900.0f, 1100.0f, 0x4216B2BDu},
        {3735928559ull, 600.0f, -2200.0f, 0x41C1D6D5u},
        {12648430ull, -1900.0f, 1400.0f, 0x41F8A9B2u},
    };
    for (const Golden& g : heights)
        REQUIRE_MSG(bits(height_at(g.seed, g.x, g.z)) == g.h,
                    "O'Haven height bits moved", "legacy terrain");

    apricot_test::pass("O'Haven keeps its exact pre-state terrain bits");
}

void florangia_is_a_low_curved_panhandle_and_peninsula() {
    // Key dry points follow the authored bend south and east.
    for (const Vec2 p : {Vec2{2350.0f, 3500.0f}, Vec2{4600.0f, 3500.0f},
                         Vec2{5250.0f, 4500.0f}, Vec2{5830.0f, 5960.0f},
                         Vec2{6550.0f, 7410.0f}, Vec2{7420.0f, 8640.0f}}) {
        REQUIRE_MSG(height_at(kSeed, p.x, p.z) > 1.0f,
                    "authored Florangia spine is underwater", "state shape");
    }
    // Water beside that spine proves this is a bent, tapered state rather than
    // one broad southeast oval.
    for (const Vec2 p : {Vec2{3150.0f, 4660.0f}, Vec2{3870.0f, 6545.0f},
                         Vec2{4740.0f, 8430.0f}, Vec2{8370.0f, 5675.0f}}) {
        REQUIRE_MSG(height_at(kSeed, p.x, p.z) < 0.0f,
                    "water notch beside Florangia filled in", "state shape");
    }

    float min_x = kWorldHalfMetres, max_x = -kWorldHalfMetres;
    float min_z = kWorldHalfMetres, max_z = -kWorldHalfMetres;
    float max_h = kMinHeightMetres;
    double sum_h = 0.0;
    int land = 0, flat = 0;
    for (int z = 3000; z <= 9800; z += 32) {
        for (int x = 1800; x <= 8700; x += 32) {
            const float h = height_at(kSeed, static_cast<float>(x),
                                      static_cast<float>(z));
            if (h <= 0.0f) continue;
            min_x = std::min(min_x, static_cast<float>(x));
            max_x = std::max(max_x, static_cast<float>(x));
            min_z = std::min(min_z, static_cast<float>(z));
            max_z = std::max(max_z, static_cast<float>(z));
            max_h = std::max(max_h, h);
            sum_h += h;
            const float ny = normal_at(kSeed, static_cast<float>(x),
                                       static_cast<float>(z)).y;
            const float slope = std::sqrt(std::max(0.0f, 1.0f - ny * ny));
            flat += slope < 0.17f;  // under roughly ten degrees
            ++land;
        }
    }
    REQUIRE(land > 5000);
    REQUIRE(min_x >= 1900.0f && min_x <= kFlorangiaMinXMetres + 150.0f);
    REQUIRE(max_x >= kFlorangiaMaxXMetres - 300.0f && max_x <= 8550.0f);
    REQUIRE(min_z >= kFlorangiaMinZMetres && min_z <= 3500.0f);
    REQUIRE(max_z >= kFlorangiaMaxZMetres - 250.0f && max_z <= 9700.0f);
    REQUIRE(max_h < 12.0f);
    REQUIRE(static_cast<float>(flat) / static_cast<float>(land) > 0.80f);
    const double land_km2 =
        static_cast<double>(land) * 32.0 * 32.0 / 1.0e6;
    std::printf("  Florangia land %.2f km2, x %.0f..%.0f z %.0f..%.0f; mean %.2f m, max %.2f m; %.1f%% under 10 deg\n",
                land_km2, min_x, max_x, min_z, max_z, sum_h / land, max_h,
                100.0f * static_cast<float>(flat) / static_cast<float>(land));
    REQUIRE(land_km2 > 9.0 && land_km2 < 11.0);

    // The airport and highway agents share this authored contract. Check the
    // full 650 m field-clearance disc, not only its centre point.
    float airport_max_slope = 0.0f;
    float airport_min_height = kMaxHeightMetres;
    float airport_min_x = 0.0f, airport_min_z = 0.0f;
    for (int dz = -650; dz <= 650; dz += 25) {
        for (int dx = -650; dx <= 650; dx += 25) {
            if (dx * dx + dz * dz > 650 * 650) continue;
            const float x = 4800.0f + static_cast<float>(dx);
            const float z = 4400.0f + static_cast<float>(dz);
            const float airport_h = height_at(kSeed, x, z);
            if (airport_h < airport_min_height) {
                airport_min_height = airport_h;
                airport_min_x = x;
                airport_min_z = z;
            }
            const float ny = normal_at(kSeed, x, z).y;
            const float slope = std::sqrt(std::max(0.0f, 1.0f - ny * ny));
            airport_max_slope = std::max(airport_max_slope, slope);
        }
    }
    std::printf("  airport clearance: 650 m radius; min %.2f m at (%.0f, %.0f); max slope ratio %.3f\n",
                airport_min_height, airport_min_x, airport_min_z,
                airport_max_slope);
    REQUIRE_MSG(airport_min_height > 1.0f,
                "Florangia airport clearance disc is not dry",
                "airport terrain contract");

    apricot_test::pass("Florangia is a low, curved, tapered panhandle state");
}

void open_water_separates_and_surrounds_the_states() {
    // A full east-west water transect between the old southeast shore and the
    // new panhandle prevents the two state masks from touching.
    for (int x = -3072; x <= 6900; x += 16)
        REQUIRE(height_at(kSeed, static_cast<float>(x), 3150.0f) < 0.0f);

    for (int p = -10240; p <= 10240; p += 64) {
        const float q = static_cast<float>(p);
        REQUIRE(height_at(kSeed, q, -kWorldHalfMetres) < 0.0f);
        REQUIRE(height_at(kSeed, q, kWorldHalfMetres) < 0.0f);
        REQUIRE(height_at(kSeed, -kWorldHalfMetres, q) < 0.0f);
        REQUIRE(height_at(kSeed, kWorldHalfMetres, q) < 0.0f);
    }

    apricot_test::pass("open water separates both states and encloses the world");
}

struct Crossing {
    float x = 0.0f;
    float z = 0.0f;
};

Crossing zero_crossing(Crossing a, Crossing b, float ha, float hb) {
    for (int i = 0; i < 20; ++i) {
        const Crossing mid{0.5f * (a.x + b.x), 0.5f * (a.z + b.z)};
        const float hm = height_at(kSeed, mid.x, mid.z);
        if ((ha > 0.0f) == (hm > 0.0f)) {
            a = mid;
            ha = hm;
        } else {
            b = mid;
            hb = hm;
        }
    }
    (void)hb;
    return {0.5f * (a.x + b.x), 0.5f * (a.z + b.z)};
}

float segment_length(Crossing a, Crossing b) {
    const float dx = a.x - b.x, dz = a.z - b.z;
    return std::sqrt(dx * dx + dz * dz);
}

struct CoastLengths {
    double sand = 0.0;
    double rock = 0.0;
    double other = 0.0;
    double total = 0.0;
};

void add_coast_segment(CoastLengths& out, Crossing a, Crossing b) {
    const float length = segment_length(a, b);
    if (length <= 0.0f) return;
    out.total += length;
    for (const Crossing p : {a, b}) {
        const Surface kind = surface_kind_at(kSeed, p.x, p.z);
        double* bucket = &out.other;
        if (kind == Surface::Sand) bucket = &out.sand;
        if (kind == Surface::Rock) bucket = &out.rock;
        *bucket += 0.5 * length;
    }
}

void florangia_coast_is_eighty_percent_sand_twenty_percent_rock() {
    constexpr int kMinX = 1800, kMaxX = 8600;
    constexpr int kMinZ = 3050, kMaxZ = 9750;
    constexpr int kStep = 20;
    constexpr int kCols = (kMaxX - kMinX) / kStep + 1;
    constexpr int kRows = (kMaxZ - kMinZ) / kStep + 1;
    std::vector<float> height(static_cast<std::size_t>(kCols * kRows));
    for (int row = 0; row < kRows; ++row) {
        for (int col = 0; col < kCols; ++col) {
            height[static_cast<std::size_t>(row * kCols + col)] = height_at(
                kSeed, static_cast<float>(kMinX + col * kStep),
                static_cast<float>(kMinZ + row * kStep));
        }
    }

    CoastLengths coast;
    for (int row = 0; row + 1 < kRows; ++row) {
        for (int col = 0; col + 1 < kCols; ++col) {
            const float x = static_cast<float>(kMinX + col * kStep);
            const float z = static_cast<float>(kMinZ + row * kStep);
            const std::array<Crossing, 4> p{{{x, z}, {x + kStep, z},
                                             {x + kStep, z + kStep},
                                             {x, z + kStep}}};
            const std::array<float, 4> h{{
                height[static_cast<std::size_t>(row * kCols + col)],
                height[static_cast<std::size_t>(row * kCols + col + 1)],
                height[static_cast<std::size_t>((row + 1) * kCols + col + 1)],
                height[static_cast<std::size_t>((row + 1) * kCols + col)]}};
            std::array<Crossing, 4> crossing{};
            std::size_t count = 0;
            for (std::size_t edge = 0; edge < 4u; ++edge) {
                const std::size_t next = (edge + 1u) % 4u;
                if ((h[edge] > 0.0f) == (h[next] > 0.0f)) continue;
                crossing[count++] = zero_crossing(p[edge], p[next], h[edge],
                                                  h[next]);
            }
            if (count == 2u) {
                add_coast_segment(coast, crossing[0], crossing[1]);
            } else if (count == 4u) {
                // The shorter non-crossing pairing is the local contour in an
                // ambiguous marching-square cell.
                const float a = segment_length(crossing[0], crossing[1]) +
                                segment_length(crossing[2], crossing[3]);
                const float b = segment_length(crossing[0], crossing[3]) +
                                segment_length(crossing[1], crossing[2]);
                if (a <= b) {
                    add_coast_segment(coast, crossing[0], crossing[1]);
                    add_coast_segment(coast, crossing[2], crossing[3]);
                } else {
                    add_coast_segment(coast, crossing[0], crossing[3]);
                    add_coast_segment(coast, crossing[1], crossing[2]);
                }
            }
        }
    }

    REQUIRE(coast.total > 9000.0);
    const double sand_fraction = coast.sand / coast.total;
    const double rock_fraction = coast.rock / coast.total;
    const double other_fraction = coast.other / coast.total;
    std::printf("  zero contour %.0f m: sand %.2f%%, rock %.2f%%, other %.2f%%\n",
                coast.total, 100.0 * sand_fraction, 100.0 * rock_fraction,
                100.0 * other_fraction);
    REQUIRE(sand_fraction >= 0.76 && sand_fraction <= 0.84);
    REQUIRE(rock_fraction >= 0.16 && rock_fraction <= 0.24);
    REQUIRE(other_fraction < 0.02);

    apricot_test::pass("actual Florangia coast is about 80% sand and 20% rock");
}

void florangia_trees_use_the_palm_family_only() {
    int florangia_trees = 0;
    for (int cz = 76; cz <= 87; ++cz) {
        for (int cx = 72; cx <= 84; ++cx) {
            for (const ScatterProp& prop :
                 scatter_chunk(kSeed, ChunkCoord{cx, cz})) {
                if (prop.kind != PropKind::Tree) continue;
                ++florangia_trees;
                REQUIRE_MSG(is_palm_tree_variant(prop.variant),
                            "Florangia emitted a temperate tree variant",
                            "palm biome");
            }
        }
    }
    REQUIRE_MSG(florangia_trees > 100,
                "Florangia palm check did not sample enough trees",
                "palm biome");

    int ohaven_trees = 0;
    for (const ScatterProp& prop : scatter_chunk(kSeed, ChunkCoord{5, 6})) {
        if (prop.kind != PropKind::Tree) continue;
        ++ohaven_trees;
        REQUIRE_MSG(!is_palm_tree_variant(prop.variant),
                    "O'Haven emitted a palm tree variant", "palm biome");
    }
    REQUIRE_MSG(ohaven_trees > 10,
                "O'Haven control chunk did not sample enough trees",
                "palm biome");

    std::printf("  palms: %d Florangia trees; %d O'Haven control trees\n",
                florangia_trees, ohaven_trees);
    apricot_test::pass("Florangia trees use palms without changing O'Haven's family");
}

}  // namespace

int main() {
    std::printf("state_terrain_tests\n");
    state_catalog_and_world_box_are_authored();
    ohaven_stays_on_the_legacy_bit_path();
    florangia_is_a_low_curved_panhandle_and_peninsula();
    open_water_separates_and_surrounds_the_states();
    florangia_coast_is_eighty_percent_sand_twenty_percent_rock();
    florangia_trees_use_the_palm_family_only();
    return apricot_test::done("state_terrain_tests");
}
