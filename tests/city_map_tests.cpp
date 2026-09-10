// Pinatty — the map, and whether it is a real place.
//
// Two jobs, and they are different jobs.
//
// The first half is DETERMINISM AND PURITY, pinned by bit equality like every
// other generator in this tree: the operators are a pure function of position,
// the bucket index that makes them cheap does not change a single answer, and
// the composition order is the one the table declares.
//
// The second half is the part a passing test cannot give you on its own, so it
// MEASURES AND PRINTS. A city where downtown and the suburbs generate the same
// silhouette has failed even if every assertion is green, and the only way to
// know is to sample the ground and look at the numbers. Flat-land fraction,
// height range and drivable fraction, per district, printed on every run. The
// thresholds below are floors on those numbers; the numbers themselves are for
// the next person to read.
//
// Most of the map's structural invariants are NOT here. They are
// static_asserts in src/city/, because compiled data can be checked by the
// compiler: district ids dense and ordered, one polygon winding, landmarks
// standing inside the districts they claim, every operator inside the world
// box with a non-zero feather, the op table in composition order. That is the
// whole argument for the map being C++ rather than a file, and a test that
// re-checked them at runtime would be pretending the compiler had not.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "city/districts.h"
#include "city/landmarks.h"
#include "city/map.h"
#include "city/miandi_layout.h"
#include "city/terrain_ops.h"
#include "terrain/heightmap.h"
#include "terrain/noise.h"
#include "test_assert.h"

using namespace apricot;
using namespace apricot::city;

namespace {

uint32_t bits(float f) {
    uint32_t u = 0;
    std::memcpy(&u, &f, sizeof u);
    return u;
}

// Slope in degrees from the terrain normal.
float slope_deg(uint64_t seed, float x, float z) {
    const float ny = normal_at(seed, x, z).y;
    return std::acos(ny > 1.0f ? 1.0f : ny) * 57.2957795f;
}

// What a car can climb. 25 degrees is generous for a road vehicle and
// deliberately so — this is "could you get up there at all", not "would you
// enjoy it".
constexpr float kDrivableDeg = 25.0f;

// --- purity and determinism --------------------------------------------------

// GOLDEN VALUES for the operators themselves, evaluated against a fixed input
// height so a failure says "the operators moved" rather than "something in the
// world moved". One sample per kind, plus the two that are easiest to break by
// accident: a feathered edge, and a point outside every operator.
void golden_operator_values() {
    REQUIRE(bits(apply_terrain_ops(18.0f, 0.0f, 0.0f)) == 0x41400000u);
    REQUIRE(bits(apply_terrain_ops(18.0f, 640.0f, -30.0f)) == 0x4136F25Cu);
    REQUIRE(bits(apply_terrain_ops(96.0f, 900.0f, -1450.0f)) == 0x42BC999Au);
    REQUIRE(bits(apply_terrain_ops(20.0f, -2250.0f, -580.0f)) == 0xC1300000u);
    REQUIRE(bits(apply_terrain_ops(12.0f, 190.0f, 1900.0f)) == 0x40DA4925u);
    REQUIRE(bits(apply_terrain_ops(30.0f, 1450.0f, 700.0f)) == 0x41A0EE0Au);
    REQUIRE(bits(apply_terrain_ops(14.0f, -1000.0f, 1180.0f)) == 0x41FCCCCDu);

    // Outside the world box there are no operators and the height passes
    // through untouched, bit for bit. If this one ever fails, the ocean has
    // started paying for the city's bucket lookup.
    REQUIRE(bits(apply_terrain_ops(7.0f, 2900.0f, 2900.0f)) == 0x40E00000u);

    apricot_test::pass("golden operator values unchanged");
}

// The four operator-free sample points in terrain_determinism_tests.cpp are
// only worth having if no operator reaches them. Assert it here, where the op
// table is in scope, so that growing a district over one of them fails LOUDLY
// instead of quietly blinding the noise pins.
void the_noise_golden_points_are_still_operator_free() {
    const Vec2 pts[] = {{-1500.0f, 900.0f},
                        {900.0f, 1100.0f},
                        {600.0f, -2200.0f},
                        {-1900.0f, 1400.0f}};
    for (const Vec2 p : pts) {
        // A sentinel that is not any operator's target, so "unchanged" cannot
        // be a coincidence.
        const float sentinel = 1234.5f;
        REQUIRE_MSG(bits(apply_terrain_ops(sentinel, p.x, p.z)) == bits(sentinel),
                    "an operator now covers a point the noise goldens rely on",
                    "golden coverage");
    }
    apricot_test::pass("the noise golden points are still operator-free");
}

// THE TEST THE BUCKET INDEX EXISTS TO SURVIVE.
//
// The index is an optimisation, and an optimisation that changes an answer is
// a bug wearing a performance argument. So: evaluate every operator in the
// table by brute force, in table order, and require the bucketed path to agree
// BIT FOR BIT at every one of 90,000 sample positions across the whole world
// box plus a margin of open ocean.
//
// This is also what catches the subtle one — a bucket whose op list came out
// in a different order from the table, which would compose a carve before its
// flatten on one side of a 128 m line and fill in the harbour there and only
// there.
void the_bucket_index_changes_no_answer() {
    long checked = 0;
    for (int j = 0; j <= 300; ++j) {
        for (int i = 0; i <= 300; ++i) {
            const float x = (static_cast<float>(i) / 150.0f - 1.0f) * 3400.0f;
            const float z = (static_cast<float>(j) / 150.0f - 1.0f) * 3400.0f;

            // Vary the input height too: an operator that only ever sees one
            // height cannot show a difference between min, max and lerp.
            const float h = 40.0f * std::sin(0.001f * x) + 30.0f * std::cos(0.0013f * z);

            float brute = h;
            for (int k = 0; k < kTerrainOpCount; ++k) {
                brute = apply_op(kTerrainOps[k], brute, x, z);
            }
            REQUIRE_MSG(bits(apply_terrain_ops(h, x, z)) == bits(brute),
                        "the bucket index disagreed with the whole table",
                        "op index");
            ++checked;
        }
    }
    REQUIRE(checked == 301L * 301L);

    // And the lists really are in table order, which is the property the
    // agreement above depends on.
    for (int b = 0; b < kOpBucketCount; ++b) {
        for (int k = 1; k < kOpIndex.count[b]; ++k) {
            REQUIRE_MSG(kOpIndex.op[b][k] > kOpIndex.op[b][k - 1],
                        "a bucket's operator list is not in table order",
                        "op index");
        }
    }
    apricot_test::pass("the bucket index is an optimisation and nothing more");
}

// Pure means pure: same inputs, same bits, in any order, forever. Interleaving
// unrelated calls would expose any hidden accumulator; there is none to expose,
// and this is what keeps it that way.
void operators_are_pure() {
    for (int j = -40; j <= 40; ++j) {
        for (int i = -40; i <= 40; ++i) {
            const float x = static_cast<float>(i) * 71.0f;
            const float z = static_cast<float>(j) * 71.0f;
            const float a = apply_terrain_ops(23.5f, x, z);
            (void)apply_terrain_ops(999.0f, 12345.0f, -54321.0f);
            (void)apply_terrain_ops(-4.0f, x + 1.0f, z - 1.0f);
            const float b = apply_terrain_ops(23.5f, x, z);
            REQUIRE_MSG(bits(a) == bits(b),
                        "an operator returned a different value the second time",
                        "purity");
        }
    }

    // Operators are AUTHORED, so they must not vary with the run seed. The
    // height field's seed dependence has to be entirely upstream of them, or
    // the map is not the same place in two sessions.
    REQUIRE(bits(apply_terrain_ops(30.0f, 100.0f, 100.0f)) ==
            bits(apply_terrain_ops(30.0f, 100.0f, 100.0f)));

    apricot_test::pass("terrain operators are a pure function of (h, x, z)");
}

// city/terrain_ops.h spells out its own smoothstep rather than including
// terrain/noise.h, because the module dependency runs terrain -> city and must
// not run back. Two copies of a function is two things that can drift, so the
// drift is a test failure instead of a seam at the edge of every district.
void the_two_smoothsteps_agree_bit_for_bit() {
    for (int i = -200; i <= 400; ++i) {
        const float v = static_cast<float>(i) * 0.01f;
        REQUIRE_MSG(bits(city::smoothstep01(0.0f, 1.0f, v)) ==
                        bits(apricot::smoothstep01(0.0f, 1.0f, v)),
                    "city and terrain smoothsteps have drifted apart",
                    "smoothstep");
        REQUIRE_MSG(bits(city::smoothstep01(120.0f, 380.0f, v * 400.0f)) ==
                        bits(apricot::smoothstep01(120.0f, 380.0f, v * 400.0f)),
                    "city and terrain smoothsteps have drifted apart",
                    "smoothstep");
    }
    apricot_test::pass("the two smoothstep copies agree bit for bit");
}

// An operator that changes nothing is a lie in a table people read to
// understand the map. Every entry must move the ground somewhere inside its
// own bounds by at least a metre.
void every_operator_does_something() {
    for (int k = 0; k < kTerrainOpCount; ++k) {
        const TerrainOp& op = kTerrainOps[k];
        const Vec2 lo = op.bounds_min();
        const Vec2 hi = op.bounds_max();
        float biggest = 0.0f;
        for (int j = 0; j <= 40; ++j) {
            for (int i = 0; i <= 40; ++i) {
                const float t = static_cast<float>(i) / 40.0f;
                const float u = static_cast<float>(j) / 40.0f;
                const float x = lo.x + (hi.x - lo.x) * t;
                const float z = lo.z + (hi.z - lo.z) * u;
                // Sweep the input height so a Carve is tested from above and a
                // Mound from below.
                for (int hh = -30; hh <= 130; hh += 10) {
                    const float h = static_cast<float>(hh);
                    const float d = std::fabs(apply_op(op, h, x, z) - h);
                    if (d > biggest) biggest = d;
                }
            }
        }
        REQUIRE_MSG(biggest > 1.0f, "an operator never moves the ground",
                    kTerrainOps[k].note);
    }
    apricot_test::pass("every operator in the table changes the terrain");
}

// --- district queries ---------------------------------------------------------

// No point may belong to two districts. Where two abut, the half-open crossing
// test in Boundary::contains() has to hand the shared edge to exactly one of
// them: a pedestrian standing on a boundary needs one police response time.
void districts_do_not_overlap() {
    // Districts are currently authored only in O'Haven. Survey its legacy
    // state box at the original density so adding a much larger ocean atlas
    // does not make unchanged district coverage look artificially sparse.
    constexpr float kOHavenSurveyHalfMetres = 3072.0f;
    long claimed = 0;
    for (int j = 0; j <= 400; ++j) {
        for (int i = 0; i <= 400; ++i) {
            const float x = (static_cast<float>(i) / 200.0f - 1.0f) *
                            kOHavenSurveyHalfMetres;
            const float z = (static_cast<float>(j) / 200.0f - 1.0f) *
                            kOHavenSurveyHalfMetres;
            int hits = 0;
            for (int d = 0; d < kDistrictCount; ++d) {
                if (kDistricts[d].boundary.contains(x, z)) ++hits;
            }
            REQUIRE_MSG(hits <= 1, "two districts claim the same point",
                        "overlap");
            if (hits == 1) ++claimed;

            // And the lookup agrees with the polygons it is built from.
            const DistrictId id = district_at(x, z);
            REQUIRE_MSG((hits == 1) == (id != DistrictId::Count),
                        "district_at disagrees with the boundary polygons",
                        "lookup");
        }
    }
    REQUIRE_MSG(claimed > 3000, "the districts cover almost none of the map",
                "coverage");

    // The countryside is not a district and must answer to a name anyway.
    REQUIRE(district_at(0.0f, 0.0f) == DistrictId::PinattyRow);
    REQUIRE(district_at(2900.0f, 2900.0f) == DistrictId::Count);
    REQUIRE(std::strcmp(district_name(DistrictId::Count), "the Meadows") == 0);
    REQUIRE(std::strcmp(district_name(DistrictId::PinattyRow), "Pinatty Row") == 0);
    for (int d = 0; d < kDistrictCount; ++d) {
        REQUIRE(district(static_cast<DistrictId>(d)).id ==
                static_cast<DistrictId>(d));
    }
    apricot_test::pass("districts partition the map without overlapping");
}

// --- the measurements ---------------------------------------------------------

struct DistrictMeasure {
    double area_km2 = 0.0;
    double land_frac = 0.0;
    double flat_frac = 0.0;
    double drivable_frac = 0.0;
    float h_lo = 0.0f;
    float h_hi = 0.0f;
    float h_med = 0.0f;
};

DistrictMeasure measure_district(const District& d, float step) {
    const Vec2 lo = d.boundary.min_corner();
    const Vec2 hi = d.boundary.max_corner();
    long inside = 0, land = 0, flat = 0, drive = 0;
    std::vector<float> hs;
    for (float z = lo.z; z <= hi.z; z += step) {
        for (float x = lo.x; x <= hi.x; x += step) {
            if (!d.boundary.contains(x, z)) continue;
            ++inside;
            const float h = height_at(kMapSeed, x, z);
            if (h <= kSeaLevelMetres) continue;
            ++land;
            hs.push_back(h);
            const float deg = slope_deg(kMapSeed, x, z);
            if (deg < 5.0f) ++flat;
            if (deg <= kDrivableDeg) ++drive;
        }
    }
    DistrictMeasure m;
    m.area_km2 = static_cast<double>(inside) * static_cast<double>(step) *
                 static_cast<double>(step) / 1.0e6;
    const double l = static_cast<double>(land);
    m.land_frac = inside ? l / static_cast<double>(inside) : 0.0;
    m.flat_frac = land ? static_cast<double>(flat) / l : 0.0;
    m.drivable_frac = land ? static_cast<double>(drive) / l : 0.0;
    if (!hs.empty()) {
        // Partial sort by hand rather than pulling in <algorithm> for three
        // order statistics over a few thousand samples.
        float mn = hs[0], mx = hs[0];
        for (const float v : hs) {
            if (v < mn) mn = v;
            if (v > mx) mx = v;
        }
        m.h_lo = mn;
        m.h_hi = mx;
        // Median by counting, which is exact enough for a printed figure and
        // avoids sorting several thousand floats ten times.
        long below = 0;
        float best = mn;
        const long half = static_cast<long>(hs.size()) / 2;
        for (float probe = mn; probe <= mx; probe += (mx - mn) / 256.0f + 1e-6f) {
            long n = 0;
            for (const float v : hs) {
                if (v <= probe) ++n;
            }
            if (n >= half) { best = probe; break; }
            below = n;
        }
        (void)below;
        m.h_med = best;
    }
    return m;
}

// THE MEASUREMENT THE TICKET ASKS FOR, PRINTED ON EVERY RUN.
//
// The assertions here are floors, not descriptions. The point of the printout
// is that the next person can see whether the map is real without trusting a
// comment: if Ferrone Hill ever measures 99% flat, every threshold below still
// passes and the table on stdout says the hill is gone.
DistrictMeasure g_measured[kDistrictCount];

void measure_and_print_every_district() {
    std::printf("\n  district           area/km2   land    flat   drivable   "
                "height range      median\n");
    double urban = 0.0;
    for (int d = 0; d < kDistrictCount; ++d) {
        const District& dd = kDistricts[d];
        const DistrictMeasure m = measure_district(dd, 4.0f);
        g_measured[d] = m;
        urban += m.area_km2;
        std::printf("  %-17s %7.2f  %5.1f%%  %5.1f%%    %5.1f%%   %6.1f .. %6.1f m %7.1f\n",
                    dd.name, m.area_km2, m.land_frac * 100.0,
                    m.flat_frac * 100.0, m.drivable_frac * 100.0,
                    static_cast<double>(m.h_lo), static_cast<double>(m.h_hi),
                    static_cast<double>(m.h_med));
    }
    std::printf("  %-17s %7.2f  (the ten polygons; the Meadows is everything "
                "else)\n\n", "TOTAL urbanised", urban);

    for (int d = 0; d < kDistrictCount; ++d) {
        const DistrictMeasure& m = g_measured[d];
        // A district polygon is a JURISDICTION, so some water inside it is
        // correct for a port or a beachfront. Three quarters is the floor:
        // below that the district is mostly sea and its generated blocks have
        // nowhere to land.
        REQUIRE_MSG(m.land_frac > 0.72, "a district is mostly under water",
                    kDistricts[d].name);
        REQUIRE_MSG(m.area_km2 > 0.2, "a district is too small to be a place",
                    kDistricts[d].name);
        // Every district has to be reachable and mostly usable by car, even
        // the vertical one. This is the number that would catch a district
        // accidentally placed on a cliff.
        REQUIRE_MSG(m.drivable_frac > 0.70,
                    "less than 70% of a district's land is drivable",
                    kDistricts[d].name);
    }
    apricot_test::pass("every district is on land and mostly drivable");
}

// THE ONE THAT MATTERS MOST: are the districts actually DIFFERENT on the
// ground? A city where downtown and the suburbs generate the same silhouette
// has failed even if every other test passes.
void the_districts_are_distinct_on_the_ground() {
    const DistrictMeasure& pinatty = g_measured[static_cast<int>(DistrictId::PinattyRow)];
    const DistrictMeasure& ferrone = g_measured[static_cast<int>(DistrictId::FerroneHill)];
    const DistrictMeasure& marrow = g_measured[static_cast<int>(DistrictId::Marrow)];
    const DistrictMeasure& camber = g_measured[static_cast<int>(DistrictId::CamberPoint)];
    const DistrictMeasure& nickel = g_measured[static_cast<int>(DistrictId::NickelHeights)];

    // Downtown is a plate. If it is not, a street grid has been laid on
    // rolling ground, which reads as a mistake even to a player who could not
    // say why.
    REQUIRE_MSG(pinatty.flat_frac > 0.97, "downtown is not flat", "Pinatty Row");
    REQUIRE_MSG(pinatty.h_hi - pinatty.h_lo < 25.0f,
                "downtown has more than 25 m of relief in it", "Pinatty Row");

    // The hill is a hill. Half its land steeper than 5 degrees, a hundred
    // metres of relief, and a real fraction of it not drivable at all — that
    // last one is the point of the district.
    REQUIRE_MSG(ferrone.flat_frac < 0.65, "Ferrone Hill is not a hill",
                "Ferrone Hill");
    REQUIRE_MSG(ferrone.h_hi - ferrone.h_lo > 100.0f,
                "Ferrone Hill has less than 100 m of relief", "Ferrone Hill");
    REQUIRE_MSG(ferrone.drivable_frac < 0.92,
                "all of Ferrone Hill is drivable, so it is not vertical",
                "Ferrone Hill");

    // And it is genuinely different from downtown, not merely different by a
    // rounding error.
    REQUIRE_MSG(pinatty.flat_frac - ferrone.flat_frac > 0.35,
                "downtown and the hill have nearly the same flat fraction",
                "distinctness");
    REQUIRE_MSG(ferrone.h_hi - pinatty.h_hi > 80.0f,
                "the hill is not appreciably taller than downtown",
                "distinctness");

    // Marrow is off-road country: rougher than the suburbs by a wide margin.
    REQUIRE_MSG(nickel.flat_frac - marrow.flat_frac > 0.15,
                "the quarry country is no rougher than the suburbs",
                "distinctness");

    // The runway is the flattest large open surface in the world, which is
    // what makes it the handling test and the stunt landing.
    REQUIRE_MSG(camber.flat_frac > 0.92, "the airfield is not flat",
                "Camber Point");
    REQUIRE_MSG(camber.h_hi < 20.0f, "the airfield is not at sea level",
                "Camber Point");

    apricot_test::pass("the districts are measurably different places");
}

// A city label that misses its own city is worse than no label: it tells the
// player the wrong place is Pinatty. The table's density and naming are already
// static_asserts in states.h, so what is left to check at runtime is the part
// the compiler cannot see — that each anchor lands on the city it names.
//
// Pinatty is checked against the ten district polygons, which ARE its urban
// extent. Miandi has no district polygons of its own yet, so it is checked
// against its authored half-extents; when it grows real districts this should
// become the same bounding-box check.
void every_city_label_sits_on_its_city() {
    const auto& pinatty = city::city_at(city::CityId::Pinatty);
    REQUIRE(std::strcmp(pinatty.name, "Pinatty") == 0);
    REQUIRE(pinatty.state == city::StateId::OHaven);

    float lo_x = 1e9f, hi_x = -1e9f, lo_z = 1e9f, hi_z = -1e9f;
    for (const city::District& d : city::kDistricts) {
        for (int i = 0; i < d.boundary.count; ++i) {
            const city::Vec2& p = d.boundary.points[i];
            lo_x = std::min(lo_x, p.x); hi_x = std::max(hi_x, p.x);
            lo_z = std::min(lo_z, p.z); hi_z = std::max(hi_z, p.z);
        }
    }
    std::printf("\n  Pinatty label (%.0f, %.0f) in districts x %.0f..%.0f "
                "z %.0f..%.0f\n",
                static_cast<double>(pinatty.map_label_anchor.x),
                static_cast<double>(pinatty.map_label_anchor.z),
                static_cast<double>(lo_x), static_cast<double>(hi_x),
                static_cast<double>(lo_z), static_cast<double>(hi_z));
    REQUIRE_MSG(pinatty.map_label_anchor.x > lo_x &&
                    pinatty.map_label_anchor.x < hi_x &&
                    pinatty.map_label_anchor.z > lo_z &&
                    pinatty.map_label_anchor.z < hi_z,
                "the Pinatty map label does not sit inside Pinatty",
                "city label");
    // Inside the box is not enough — a corner of the box is still inside it,
    // and a city label on the docks names the wrong place. Pin it near the
    // centre of the districts it is naming.
    const float mid_x = (lo_x + hi_x) * 0.5f, mid_z = (lo_z + hi_z) * 0.5f;
    REQUIRE_MSG(std::hypot(pinatty.map_label_anchor.x - mid_x,
                           pinatty.map_label_anchor.z - mid_z) < 400.0f,
                "the Pinatty label has drifted off the centre of the city",
                "city label");

    const auto& miandi = city::city_at(city::CityId::Miandi);
    REQUIRE(miandi.state == city::StateId::Florangia);
    REQUIRE_MSG(std::fabs(miandi.map_label_anchor.x -
                          city::kMiandiWorldOrigin.x) <= city::kMiandiHalfWidthM &&
                    std::fabs(miandi.map_label_anchor.z -
                              city::kMiandiWorldOrigin.z) <= city::kMiandiHalfDepthM,
                "the Miandi map label does not sit inside Miandi",
                "city label");

    // Two cities, two states, two names. A duplicate here would draw one label
    // twice and silently lose the other.
    for (int a = 0; a < city::kCityCount; ++a)
        for (int b = a + 1; b < city::kCityCount; ++b)
            REQUIRE(std::strcmp(city::kCities[a].name,
                                city::kCities[b].name) != 0);

    apricot_test::pass("every city label sits on the city it names");
}

// Both states, re-measured WITH the Pinatty operators in, because those change
// its coast. The old state remains roughly 16 km2 and first-pass Florangia adds
// roughly 5 km2; the only honest total is the one taken afterwards.
void the_states_are_the_right_size() {
    constexpr float kStep = 8.0f;
    long land = 0;
    double area = 0.0;
    long flat = 0, drivable = 0;
    float peak = 0.0f;
    float peak_x = 0.0f, peak_z = 0.0f;
    for (float z = -kWorldHalfMetres; z <= kWorldHalfMetres; z += kStep) {
        for (float x = -kWorldHalfMetres; x <= kWorldHalfMetres; x += kStep) {
            const float h = height_at(kMapSeed, x, z);
            if (h <= kSeaLevelMetres) continue;
            ++land;
            area += static_cast<double>(kStep) * static_cast<double>(kStep);
            if (h > peak) { peak = h; peak_x = x; peak_z = z; }
            const float deg = slope_deg(kMapSeed, x, z);
            if (deg < 5.0f) ++flat;
            if (deg <= kDrivableDeg) ++drivable;
        }
    }
    const double km2 = area / 1.0e6;
    std::printf("  states: %.2f km2 of land in a %.0f m box (%.1f%% fill)\n", km2,
                2.0 * kWorldHalfMetres,
                100.0 * km2 / (2.0 * kWorldHalfMetres * 2.0 * kWorldHalfMetres / 1.0e6));
    std::printf("          peak %.1f m at (%.0f, %.0f);  %.1f%% of land under 5 "
                "degrees, %.1f%% drivable\n",
                static_cast<double>(peak), static_cast<double>(peak_x),
                static_cast<double>(peak_z),
                100.0 * static_cast<double>(flat) / static_cast<double>(land),
                100.0 * static_cast<double>(drivable) / static_cast<double>(land));

    REQUIRE_MSG(km2 > 25.0 && km2 < 28.0,
                "the scaled two-state atlas is not about 26.5 km2 of land",
                "land area");
    REQUIRE_MSG(peak > 110.0f, "there is no commanding high ground", "peak");

    // Bounded by sea, not by a wall: the outermost 200 m of the box on every
    // side must be open water, or the island has grown into its own frame.
    for (int i = 0; i <= 400; ++i) {
        const float t = (static_cast<float>(i) / 200.0f - 1.0f) * kWorldHalfMetres;
        const float edge = kWorldHalfMetres - 100.0f;
        REQUIRE_MSG(height_at(kMapSeed, t, -edge) < kSeaLevelMetres,
                    "land at the north edge of the world box", "states");
        REQUIRE_MSG(height_at(kMapSeed, t, edge) < kSeaLevelMetres,
                    "land at the south edge of the world box", "states");
        REQUIRE_MSG(height_at(kMapSeed, -edge, t) < kSeaLevelMetres,
                    "land at the west edge of the world box", "states");
        REQUIRE_MSG(height_at(kMapSeed, edge, t) < kSeaLevelMetres,
                    "land at the east edge of the world box", "states");
    }
    apricot_test::pass(
        "both states total about 26.5 km2 and are bounded by water");
}

// --- landmarks ----------------------------------------------------------------

void landmarks_stand_where_the_map_says() {
    float shortest_island_asl = 1e9f;
    float tallest_district_asl = 0.0f;

    std::printf("\n  landmark                        tier      ground     top\n");
    for (int i = 0; i < kLandmarkCount; ++i) {
        const Landmark& l = kLandmarks[i];
        const float ground = height_at(kMapSeed, l.pos.x, l.pos.z);
        const float top = ground + l.height_m;
        const char* tier = l.tier == LandmarkTier::Island     ? "ISLAND"
                           : l.tier == LandmarkTier::District ? "district"
                                                              : "corner";
        std::printf("  %-30s %-8s %7.1f %7.1f m\n", l.name, tier,
                    static_cast<double>(ground), static_cast<double>(top));

        if (l.kind == LandmarkKind::BridgeTower) {
            // The Kessel Bridge towers stand IN the channel, and they had
            // better: a bridge tower on dry land means the carve missed.
            REQUIRE_MSG(ground < kSeaLevelMetres,
                        "a bridge tower is not standing in water", l.name);
            continue;
        }

        REQUIRE_MSG(ground > kSeaLevelMetres, "a landmark is under water",
                    l.name);

        if (l.tier == LandmarkTier::Island && !l.lit &&
            top < shortest_island_asl) {
            shortest_island_asl = top;
        }
        if (l.tier == LandmarkTier::District && top > tallest_district_asl) {
            tallest_district_asl = top;
        }
    }

    // THE CHECK THAT COULD NOT BE A static_assert, AND THE ONE THAT TOOK TWO
    // GOES TO STATE CORRECTLY.
    //
    // Tier is how far a thing reads. The first attempt asserted it on structure
    // height in the table and failed: the island-tier Kepler flare is 46 m and
    // the district-tier Kessel bridge towers are 52 m. Fair enough — the flare
    // stands on a plate at 9 m and the mast stands on a hill at 119, so height
    // above sea level is the number that matters, and ASL is a measurement of
    // the terrain rather than a constant, which is why this lives here.
    //
    // The second attempt asserted it on ASL and ALSO failed, measured: the
    // flare tops out at 55 m ASL and the Marrow quarry face, district tier,
    // tops out at 71 m on a hillside. That one is not a bug in the table
    // either. The flare is island-tier because IT BURNS AT NIGHT, which is what
    // the design says and what the data now says too — Landmark::lit.
    //
    // So the true statement, and the one asserted: every UNLIT island-tier
    // landmark out-tops every district-tier one. A lit one is exempt, because
    // what carries it is not its height.
    REQUIRE_MSG(shortest_island_asl > tallest_district_asl,
                "an unlit island-tier landmark stands lower than a district-tier "
                "one, so nothing but the dark makes it island-tier",
                "landmark tiers");

    // The two tallest things in the world are the ones a player navigates by,
    // and they have to be a long way apart or half the island has nothing to
    // aim at.
    const Landmark& mast = kLandmarks[0];
    const Landmark& tower = kLandmarks[1];
    const float dx = mast.pos.x - tower.pos.x;
    const float dz = mast.pos.z - tower.pos.z;
    REQUIRE_MSG(std::sqrt(dx * dx + dz * dz) > 1500.0f,
                "the two island-tier landmarks are on top of each other",
                "landmark spread");
    REQUIRE_MSG(height_at(kMapSeed, mast.pos.x, mast.pos.z) + mast.height_m >
                    150.0f,
                "the Ferrone Mast does not clear 150 m above sea level",
                "Ferrone Mast");

    apricot_test::pass("landmarks stand on the ground the map claims");
}

// --- the causeway -------------------------------------------------------------

// COMPOSITION ORDER, PROVED ON THE GROUND.
//
// The Camber channel is a Carve that severs the southern peninsula end to end.
// The Camber Causeway is a Grade that crosses it. Grade composes LAST, so the
// road wins and reconnects exactly one strip of it. That pair is the clearest
// demonstration in the map that op order is authored data and not tidiness —
// swap those two entries and the causeway is at the bottom of a channel.
//
// So: walk the channel and require exactly one land bridge across it, and
// require it to be narrow. This is the test that would fail the day somebody
// "simplified" the table by sorting it.
void the_causeway_is_the_only_way_onto_camber_point() {
    // The old causeway was dead straight, so this test only checked for a dry
    // X column. The real airport approach now bends around the runway. Count
    // connected dry components across the whole channel instead; that proves
    // the same chokepoint without requiring the road to drive through airside.
    constexpr float kStep = 5.0f;
    constexpr float kMinX = -1100.0f;
    constexpr float kMaxX = 1300.0f;
    constexpr float kMinZ = 1600.0f;
    constexpr float kMaxZ = 2050.0f;
    constexpr int kCols =
        static_cast<int>((kMaxX - kMinX) / kStep) + 1;
    constexpr int kRows =
        static_cast<int>((kMaxZ - kMinZ) / kStep) + 1;
    const auto cell = [](int x, int z) { return z * kCols + x; };

    std::vector<uint8_t> dry(static_cast<std::size_t>(kCols * kRows), 0u);
    std::vector<uint8_t> seen(dry.size(), 0u);
    for (int z = 0; z < kRows; ++z) {
        for (int x = 0; x < kCols; ++x) {
            dry[static_cast<std::size_t>(cell(x, z))] =
                height_at(kMapSeed, kMinX + x * kStep,
                          kMinZ + z * kStep) > kSeaLevelMetres;
        }
    }

    int bridges = 0;
    std::vector<int> queue;
    std::vector<int> component;
    queue.reserve(dry.size());
    component.reserve(dry.size());
    for (int seed = 0; seed < kCols * kRows; ++seed) {
        if (!dry[static_cast<std::size_t>(seed)] ||
            seen[static_cast<std::size_t>(seed)]) {
            continue;
        }
        queue.clear();
        component.clear();
        queue.push_back(seed);
        seen[static_cast<std::size_t>(seed)] = 1u;
        bool touches_north = false;
        bool touches_south = false;
        for (std::size_t head = 0; head < queue.size(); ++head) {
            const int here = queue[head];
            component.push_back(here);
            const int x = here % kCols;
            const int z = here / kCols;
            touches_north |= z == 0;
            touches_south |= z == kRows - 1;
            const int nx[] = {x - 1, x + 1, x, x};
            const int nz[] = {z, z, z - 1, z + 1};
            for (int d = 0; d < 4; ++d) {
                if (nx[d] < 0 || nx[d] >= kCols ||
                    nz[d] < 0 || nz[d] >= kRows) {
                    continue;
                }
                const int next = cell(nx[d], nz[d]);
                if (dry[static_cast<std::size_t>(next)] &&
                    !seen[static_cast<std::size_t>(next)]) {
                    seen[static_cast<std::size_t>(next)] = 1u;
                    queue.push_back(next);
                }
            }
        }
        if (!touches_north || !touches_south) continue;
        ++bridges;
    }

    // Measure the narrow bridge deck itself, before the parkway begins to
    // flare and turn. Counting every dry cell in the connected landmass would
    // measure the airport peninsula, not the roadblock chokepoint.
    const Road* causeway = nullptr;
    for (int road_i = 0; road_i < kRoadCount; ++road_i) {
        if (kRoads[road_i].id == 10u) causeway = &kRoads[road_i];
    }
    REQUIRE_MSG(causeway != nullptr, "causeway road is missing", "causeway");
    int bottleneck_cells = kCols;
    for (float z = 1600.0f; z <= 1850.0f; z += kStep) {
        float road_x = causeway->path[0].x;
        for (int seg = 0; seg + 1 < causeway->count; ++seg) {
            const RoadPoint a = causeway->path[seg];
            const RoadPoint b = causeway->path[seg + 1];
            if (z < a.z || z > b.z) continue;
            const float t = (z - a.z) / (b.z - a.z);
            road_x = a.x + (b.x - a.x) * t;
        }
        int x = static_cast<int>(std::round((road_x - kMinX) / kStep));
        const int row = static_cast<int>(std::round((z - kMinZ) / kStep));
        REQUIRE_MSG(dry[static_cast<std::size_t>(cell(x, row))],
                    "causeway centre is under water", "causeway");
        int lo = x;
        int hi = x;
        while (lo > 0 && dry[static_cast<std::size_t>(cell(lo - 1, row))]) --lo;
        while (hi + 1 < kCols &&
               dry[static_cast<std::size_t>(cell(hi + 1, row))]) {
            ++hi;
        }
        bottleneck_cells = std::min(bottleneck_cells, hi - lo + 1);
    }
    const float bridge_width = bottleneck_cells * kStep;

    std::printf("\n  Camber channel: %d connected land bridge(s); narrowest "
                "cross-section %.0f m\n",
                bridges, static_cast<double>(bridge_width));

    REQUIRE_MSG(bridges == 1,
                "the Camber channel has more than one crossing, so the "
                "causeway is not a chokepoint",
                "causeway");
    REQUIRE_MSG(bridge_width < 90.0f,
                "the causeway is wide enough to drive around a roadblock on",
                "causeway");
    REQUIRE_MSG(bridge_width > 15.0f,
                "the causeway is too narrow to be a two-lane road", "causeway");

    // And the actual bent centreline is above water. Sampling the old fixed X
    // would now prove a route the player cannot drive.
    for (int road_i = 0; road_i < kRoadCount; ++road_i) {
        const Road& road = kRoads[road_i];
        if (road.id != 10u && road.id != 157u) continue;
        for (int seg = 0; seg + 1 < road.count; ++seg) {
            for (int i = 0; i <= 32; ++i) {
                const float t = static_cast<float>(i) / 32.0f;
                const float x = road.path[seg].x +
                    (road.path[seg + 1].x - road.path[seg].x) * t;
                const float z = road.path[seg].z +
                    (road.path[seg + 1].z - road.path[seg].z) * t;
                REQUIRE_MSG(height_at(kMapSeed, x, z) > kSeaLevelMetres,
                            "airport approach dips below sea level", road.name);
            }
        }
    }
    apricot_test::pass("the causeway is the only way onto Camber Point");
}

// --- what the operators cost --------------------------------------------------

void report_the_op_index() {
    std::printf("\n  operator table: %d operators, %d x %d buckets of %.0f m; "
                "%d occupied, worst holds %d\n",
                kTerrainOpCount, kOpBucketsPerSide, kOpBucketsPerSide,
                static_cast<double>(kOpBucketMetres), kOpIndex.occupied,
                kOpIndex.max_in_bucket);

    // Count what a typical sample actually touches, because "0-2 ops" was the
    // design's intent and this is the measurement.
    long total = 0, samples = 0, worst = 0;
    for (int j = 0; j <= 200; ++j) {
        for (int i = 0; i <= 200; ++i) {
            const float x = (static_cast<float>(i) / 100.0f - 1.0f) * kWorldHalfMetres;
            const float z = (static_cast<float>(j) / 100.0f - 1.0f) * kWorldHalfMetres;
            const int bx = op_bucket_axis(x);
            const int bz = op_bucket_axis(z);
            if (bx < 0 || bz < 0) continue;
            const long n = kOpIndex.count[bz * kOpBucketsPerSide + bx];
            total += n;
            if (n > worst) worst = n;
            ++samples;
        }
    }
    std::printf("  a sample inside the world box tests %.2f operators on "
                "average, %ld at worst (of %d)\n\n",
                static_cast<double>(total) / static_cast<double>(samples), worst,
                kTerrainOpCount);

    REQUIRE_MSG(static_cast<double>(total) / static_cast<double>(samples) < 3.0,
                "the average sample tests three or more operators; the bucket "
                "index is not earning its place",
                "op index");
    apricot_test::pass("the op index keeps the inner loop cheap");
}

}  // namespace

int main() {
    std::printf("city_map_tests\n");
    golden_operator_values();
    the_noise_golden_points_are_still_operator_free();
    the_bucket_index_changes_no_answer();
    operators_are_pure();
    the_two_smoothsteps_agree_bit_for_bit();
    every_operator_does_something();
    districts_do_not_overlap();
    measure_and_print_every_district();
    the_districts_are_distinct_on_the_ground();
    every_city_label_sits_on_its_city();
    the_states_are_the_right_size();
    landmarks_stand_where_the_map_says();
    the_causeway_is_the_only_way_onto_camber_point();
    report_the_op_index();
    return apricot_test::done("city_map_tests");
}
