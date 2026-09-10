// Does the kerb read as the district it is in?
//
// city/districts.h has authored `.pop = {.traffic, .ped, .parked}` per district
// since the table was written. `.traffic` and `.ped` were carried onto every
// lane; `.parked` was read by nothing at all, so Nickel Heights — the
// residential district that authors 1.2 parked against 0.8 traffic — had
// exactly as many cars at its kerb as Marrow's quarry, which authors 0.05.
// That is the difference between a street and a road, and it was free.
//
// The claims here are in two halves, because "the number is wired up" and "the
// number produces something sensible on the real map" are not the same claim
// and the first one passes on a map with no roads at all.
//
//   1. .pop.parked reaches Lane::parked_density, on the REAL Pinatty map.
//   2. The bays it produces are physically legal: clear of the traffic lane by
//      the authored margin, clear of every junction mouth, and never two cars
//      in one space.
//   3. The population near a point is a function of the point, not of how the
//      player got there — the same property the moving population has.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <vector>

#include <glm/glm.hpp>

#include "city/districts.h"
#include "city/map.h"
#include "city/spines.h"
#include "road/lane_graph.h"
#include "road/road_graph.h"
#include "traffic/ambient.h"
#include "traffic/crowd.h"

#include "test_assert.h"

using namespace apricot;
using apricot_test::pass;

namespace {

struct RealMap {
    TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    LaneGraph lanes;

    RealMap() {
        roads.build(city::map_spines(), RoadGraphParams{}, ground.sampler());
        lanes.build(roads, ground.sampler(), LaneBuildParams{});
    }
};

// ---------------------------------------------------------------------------
//  1. the authored number reaches the lane
// ---------------------------------------------------------------------------

void authored_parked_density_reaches_every_lane() {
    RealMap map;
    REQUIRE_MSG(map.lanes.lane_count() > 1000,
                "the real map did not build", "vacuity");

    // Every value the district table authors must turn up on some lane, and
    // nothing else may. A default that quietly survives — every lane at 1.0 —
    // is exactly the failure this test exists to catch, and it looks like
    // success from every other angle.
    std::map<float, std::size_t> seen;
    for (const Lane& l : map.lanes.lanes()) ++seen[l.parked_density];

    std::size_t authored_found = 0;
    for (int i = 0; i < city::kDistrictCount; ++i) {
        const float authored =
            city::kDistricts[static_cast<std::size_t>(i)].pop.parked;
        if (seen.count(authored) != 0) ++authored_found;
    }
    REQUIRE_MSG(authored_found >= 8,
                "most authored parked densities never reached a lane",
                "wiring");
    REQUIRE_MSG(seen.count(city::kMeadowsParkedDensity) != 0,
                "the countryside did not get its own parked density",
                "countryside");
    REQUIRE_MSG(seen.count(1.0f) == 0 || seen.at(1.0f) < map.lanes.lane_count() / 4,
                "a quarter of the map is still sitting on the struct default, "
                "which is what an unwired field looks like",
                "default");

    std::printf("      %zu distinct parked densities over %zu lanes:",
                seen.size(), map.lanes.lane_count());
    for (const auto& entry : seen)
        std::printf(" %.2f x%zu", static_cast<double>(entry.first), entry.second);
    std::printf("\n");
    pass(".pop.parked is carried from the district table onto every lane");
}

// ---------------------------------------------------------------------------
//  2. what the bays look like on the real map
// ---------------------------------------------------------------------------

void parked_cars_per_kilometre_tracks_the_authored_density() {
    RealMap map;
    const AmbientTuning ambient;

    // MEASURED OVER THE KERB THAT COULD HOLD A CAR, not over every road.
    //
    // The first draft of this counted every lane with a sidewalk and came out
    // NON-MONOTONIC: three densities produced zero cars per kilometre and one
    // of them was 0.70. The cause was not the density at all — those districts'
    // through-roads are Arterials, whose outer lane leaves only 0.55 m to a
    // kerbside body against the authored 1.00 m clearance, so they get no bay
    // whatever the district says. Aggregating them in made the number a
    // measurement of ROAD CLASS wearing a density's name. Splitting
    // ParkedLaneBay::lateral_m (has the road got room) from ::slots (how many
    // the district puts there) is what makes the two separable at all.
    struct Bucket {
        double usable_m = 0.0;
        double cars = 0.0;
        double eligible_lanes = 0.0;
        double total_lanes = 0.0;
    };
    std::map<float, Bucket> by_density;
    for (const Lane& l : map.lanes.lanes()) {
        if (!road_class_def(l.cls).sidewalks) continue;
        const uint32_t outermost = static_cast<uint32_t>(
            std::max<uint8_t>(1, std::max(l.lanes_at_start, l.lanes_at_end)));
        if (static_cast<uint32_t>(l.index) + 1u != outermost) continue;
        Bucket& b = by_density[l.parked_density];
        b.total_lanes += 1.0;
        const ParkedLaneBay bay = parked_lane_bay(l, ambient);
        if (bay.lateral_m <= 0.0f) continue;  // no room, whatever the district says
        b.eligible_lanes += 1.0;
        b.usable_m += static_cast<double>(bay.usable_m);
        b.cars += static_cast<double>(bay.slots);
    }
    REQUIRE_MSG(by_density.size() >= 6,
                "too few distinct densities to compare", "vacuity");

    // Enough kerb that floor(usable / pitch) is not the dominant term. Below
    // this a bucket is a handful of short streets and its rate is quantisation
    // noise, which is a property of the sample and not of the density.
    constexpr double kMinSampleM = 2000.0;

    std::printf("      density  lanes  with room  usable km   cars   cars/km\n");
    double previous = -1.0;
    std::size_t compared = 0;
    for (const auto& entry : by_density) {
        const Bucket& b = entry.second;
        const double per_km = b.usable_m > 1.0
            ? b.cars / (b.usable_m / 1000.0) : 0.0;
        std::printf("      %7.2f %6.0f %10.0f %10.2f %6.0f  %7.1f%s\n",
                    static_cast<double>(entry.first), b.total_lanes,
                    b.eligible_lanes, b.usable_m / 1000.0, b.cars, per_km,
                    b.usable_m >= kMinSampleM ? "" : "   (small sample)");
        if (b.usable_m < kMinSampleM) continue;
        REQUIRE_MSG(per_km >= previous - 1e-6,
                    "a district authored with MORE parked cars produced FEWER "
                    "per kilometre of the kerb that had room for them",
                    "monotonic");
        previous = per_km;
        ++compared;
    }
    REQUIRE_MSG(compared >= 4,
                "fewer than four densities had a big enough sample to compare",
                "vacuity");

    // The two ends of the authored range, by name, so the numbers in the
    // report are the numbers the map actually authors. NOTE: Marrow authors
    // 0.05 and not the 0.2 a reader might expect — 0.2 is Camber Point.
    const auto nickel = by_density.find(1.2f);   // Nickel Heights
    const auto marrow = by_density.find(0.05f);  // Marrow
    REQUIRE_MSG(nickel != by_density.end() && marrow != by_density.end(),
                "the busiest and emptiest authored kerbs are not both on the "
                "map",
                "range");
    const double nickel_km = nickel->second.usable_m > 1.0
        ? nickel->second.cars / (nickel->second.usable_m / 1000.0) : 0.0;
    const double marrow_km = marrow->second.usable_m > 1.0
        ? marrow->second.cars / (marrow->second.usable_m / 1000.0) : 0.0;
    REQUIRE_MSG(nickel_km > 60.0,
                "Nickel Heights authors 1.2 and its kerb is still empty",
                "busy");
    REQUIRE_MSG(nickel_km > marrow_km * 8.0,
                "the busiest and emptiest authored kerbs came out nearly the "
                "same, so the density is being ignored somewhere",
                "spread");
    std::printf("      Nickel Heights %.1f cars/km of usable kerb against "
                "Marrow's %.1f\n",
                nickel_km, marrow_km);
    pass("kerbside parking density is the density the district authored");
}

void a_parked_car_never_sits_in_the_traffic_lane_or_a_junction() {
    RealMap map;
    const AmbientTuning ambient;

    std::size_t bays = 0, cars = 0;
    float worst_clearance = 1e30f;
    float worst_setback = 1e30f;
    float worst_gap = 1e30f;
    for (LaneRef lr = 0; lr < map.lanes.lane_count(); ++lr) {
        const Lane& l = map.lanes.lane(lr);
        const ParkedLaneBay bay = parked_lane_bay(l, ambient);
        if (bay.slots == 0) continue;
        ++bays;
        REQUIRE_MSG(road_class_def(l.cls).sidewalks,
                    "a road with no kerb grew a kerbside parking bay",
                    "kerb");
        float previous = -1e30f;
        for (uint32_t slot = 0; slot < bay.slots; ++slot) {
            const ParkedSlot p = parked_slot(city::kMapSeed, l, bay, slot, ambient);
            ++cars;
            // Room between the traffic and the parked body. Nothing puts these
            // in a driver's obstacle set, so an overlap here IS a car driving
            // through a car.
            worst_clearance = std::min(
                worst_clearance, p.lateral_m - ambient.parked_half_width_m);
            worst_setback = std::min(
                worst_setback,
                std::min(p.dist_along_m, l.length_m - p.dist_along_m));
            if (previous > -1e29f)
                worst_gap = std::min(worst_gap, p.dist_along_m - previous);
            previous = p.dist_along_m;
            // And it must still be on the carriageway it is parked at the edge
            // of, not out in the road's sidewalk strip.
            REQUIRE_MSG(p.lateral_m + l.lateral_offset_m +
                                ambient.parked_half_width_m <=
                            l.width_m * 0.5f + 1e-3f,
                        "a parked car hangs off the far side of the kerb",
                        "kerb-line");
        }
    }

    REQUIRE_MSG(bays > 200 && cars > 2000,
                "almost no kerb on the whole island holds a parked car",
                "vacuity");
    REQUIRE_MSG(worst_clearance >= ambient.parked_lane_clearance_m - 1e-3f,
                "a parked car came closer to a live lane centreline than the "
                "authored clearance",
                "clearance");
    REQUIRE_MSG(worst_setback >= ambient.parked_junction_setback_m - 1e-3f,
                "a parked car sits inside a junction mouth", "junction");
    REQUIRE_MSG(worst_gap >= 5.0f,
                "two parked cars were placed inside one car length of each "
                "other, so the jitter can overlap them",
                "overlap");
    std::printf("      %zu bays, %zu parked cars: worst lane clearance %.2f m, "
                "worst junction setback %.2f m, tightest pair %.2f m\n",
                bays, cars, static_cast<double>(worst_clearance),
                static_cast<double>(worst_setback),
                static_cast<double>(worst_gap));
    pass("every parked car on the island clears the traffic, the junctions and "
         "its neighbours");
}

// ---------------------------------------------------------------------------
//  3. the same property the moving population has
// ---------------------------------------------------------------------------

void the_parked_population_does_not_care_how_you_arrived() {
    RealMap map;
    const AmbientTuning ambient;

    CrowdTuning narrow;
    narrow.parked_activate_m = 120.0f;
    CrowdTuning wide;
    wide.parked_activate_m = 260.0f;
    wide.reverse_scan_order = true;
    wide.vehicle_activate_m = 380.0f;
    wide.vehicle_retire_m = 520.0f;

    Crowd cn, cw;
    cn.build(map.lanes, city::kMapSeed, ambient, narrow);
    cw.build(map.lanes, city::kMapSeed, ambient, wide);

    // Two very different histories, meeting at one point on one step.
    const glm::vec2 meet{-260.0f, 120.0f};
    for (int64_t step = 0; step < 240; step += 8) {
        cn.refresh(step, glm::vec2{-1400.0f, -900.0f});
        cw.refresh(step, glm::vec2{900.0f, 1300.0f});
    }
    cn.refresh(240, meet);
    cw.refresh(240, meet);

    REQUIRE_MSG(!cn.ambient_parked().empty(),
                "no parked cars at the meeting point", "vacuity");
    REQUIRE_MSG(cw.ambient_parked().size() > cn.ambient_parked().size(),
                "the wider radius did not hold more parked cars, so the two "
                "runs are not actually different",
                "vacuity");

    std::size_t compared = 0;
    for (const AmbientParkedCar& a : cn.ambient_parked()) {
        const auto it = std::find_if(
            cw.ambient_parked().begin(), cw.ambient_parked().end(),
            [&](const AmbientParkedCar& b) {
                return b.lane_key == a.lane_key && b.slot == a.slot;
            });
        REQUIRE_MSG(it != cw.ambient_parked().end(),
                    "a parked car exists in one run and not the other",
                    "membership");
        REQUIRE_MSG(it->pos == a.pos && it->fwd == a.fwd && it->kind == a.kind,
                    "the same parked car came out differently in two runs",
                    "state");
        ++compared;
    }
    // Sorted, so anything downstream that sums over the list gets an answer
    // that is a property of the population.
    for (std::size_t i = 1; i < cn.ambient_parked().size(); ++i) {
        const AmbientParkedCar& a = cn.ambient_parked()[i - 1];
        const AmbientParkedCar& b = cn.ambient_parked()[i];
        REQUIRE_MSG(a.lane_key < b.lane_key ||
                        (a.lane_key == b.lane_key && a.slot < b.slot),
                    "the parked list is not sorted by identity", "order");
    }
    REQUIRE(compared > 10);
    std::printf("      %zu shared parked cars identical after opposite "
                "approaches (%zu vs %zu resident)\n",
                compared, cn.ambient_parked().size(),
                cw.ambient_parked().size());
    pass("a parked car is a pure function of its identity, so arrival "
         "direction and radius cannot reach it");
}

}  // namespace

int main() {
    std::printf("parked_density_tests\n");
    authored_parked_density_reaches_every_lane();
    parked_cars_per_kilometre_tracks_the_authored_density();
    a_parked_car_never_sits_in_the_traffic_lane_or_a_junction();
    the_parked_population_does_not_care_how_you_arrived();
    return apricot_test::done("parked_density_tests");
}
