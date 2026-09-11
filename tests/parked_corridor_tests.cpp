// CAN TRAFFIC GET PAST A PARKED CAR?
//
// It could not, and the reason was three separate faults stacked on one
// another, each of which hid the next. Each claim below is one of them, in the
// order you hit them driving down a Street.
//
//   1. The bay gate and the runtime hazard test measured DIFFERENT BODIES.
//      parked_lane_bay() laid every bay out against a nominal 0.95 m half
//      width while classify_obstruction() used the real footprint, up to
//      1.15 m. On the island that put 643 of 2500 parked cars — every parked
//      box truck — reaching into a box truck's driving corridor, so a quarter
//      of drivers met a full stop on any Street.
//
//   2. The clearance sweep then refused every escape. It pads the body by
//      0.28 m for safety, which against a legally parked car is not a margin
//      but a veto on the lane the car was already sitting in: the sweep
//      declared standing still to be the only admissible state, which is
//      exactly what being stuck is. No overtake, bypass or nudge could be
//      planned, and the recovery ladder despawned the car instead.
//
//   3. And an arc that WAS admitted merged back onto the next parked bumper,
//      where no further arc can start, or was declined outright near a
//      junction because the textbook run did not fit.
//
// The live cases run the production Crowd with a real kerbside population, not
// hand-built obstacles, because the whole failure was a disagreement between
// two real producers about the same car.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

#include <glm/glm.hpp>

#include "city/map.h"
#include "city/spines.h"
#include "core/fixed_step.h"
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

// Every body that can drive past a kerb, so the corridor claims below are
// made about the WORST pairing and not about an average one.
const std::vector<TrafficVehicleKind>& driver_kinds() {
    static const std::vector<TrafficVehicleKind> kinds = {
        TrafficVehicleKind::Sedan, TrafficVehicleKind::BoxTruck,
        TrafficVehicleKind::Ambulance, TrafficVehicleKind::Firetruck,
        TrafficVehicleKind::HalcyonSix, TrafficVehicleKind::MontroseRegentEight,
        TrafficVehicleKind::VesperVx91, TrafficVehicleKind::Police};
    return kinds;
}

// ---------------------------------------------------------------------------
//  1. the gate and the hazard test measure the same car
// ---------------------------------------------------------------------------

// widest_parked_half_width_m() is a hand-written list of the bodies
// parked_vehicle_kind() can return. Brute-force the identity space against it:
// a list like that drifts the moment either recipe is touched, and the drift
// is silent — it comes back as traffic mysteriously stopping again.
void the_widest_parked_body_is_the_widest_body_that_parks() {
    float seen = 0.0f;
    std::vector<TrafficVehicleKind> distinct;
    for (uint64_t key = 1; key <= 4000; ++key) {
        for (uint32_t slot = 0; slot < 24; ++slot) {
            const TrafficVehicleKind k = parked_vehicle_kind(key * 2654435761ull, slot);
            seen = std::max(seen, traffic_vehicle_footprint(k).half_width_m);
            if (std::find(distinct.begin(), distinct.end(), k) == distinct.end())
                distinct.push_back(k);
        }
    }
    std::printf("      %zu distinct parked bodies over 96000 identities, "
                "widest %.4f m (declared %.4f m)\n",
                distinct.size(), double(seen),
                double(widest_parked_half_width_m()));
    REQUIRE_MSG(seen > 0.0f, "no parked bodies were sampled at all", "vacuity");
    REQUIRE_NEAR(widest_parked_half_width_m(), seen, 1e-6f);
    // An emergency body at a kerb means something the police module has not
    // said. The fold is load-bearing for the number above, so pin it here.
    for (TrafficVehicleKind k : distinct) {
        REQUIRE_MSG(k != TrafficVehicleKind::Ambulance &&
                        k != TrafficVehicleKind::Firetruck &&
                        k != TrafficVehicleKind::Police,
                    "an emergency body was parked unattended at a kerb",
                    "fold");
    }
    pass("widest_parked_half_width_m() is the widest body that actually parks");
}

void a_bay_exists_only_where_the_real_body_clears_the_lane() {
    RealMap map;
    const AmbientTuning ambient;
    const float widest = widest_parked_half_width_m();

    std::size_t bays = 0, cars = 0;
    float worst_clearance = 1e30f;
    for (LaneRef lr = 0; lr < map.lanes.lane_count(); ++lr) {
        const Lane& l = map.lanes.lane(lr);
        const ParkedLaneBay bay = parked_lane_bay(l, ambient);
        if (bay.slots == 0) continue;
        ++bays;
        cars += bay.slots;
        // THE CLAIM THAT USED TO BE MADE ABOUT THE WRONG BODY. Measured with
        // the real widest footprint, not AmbientTuning::parked_half_width_m.
        worst_clearance = std::min(worst_clearance, bay.lateral_m - widest);
        // And whatever is really parked there stays on the carriageway.
        for (uint32_t slot = 0; slot < bay.slots; ++slot) {
            const ParkedSlot p = parked_slot(city::kMapSeed, l, bay, slot, ambient);
            const float body =
                traffic_vehicle_footprint(parked_vehicle_kind(l.key, slot)).half_width_m;
            REQUIRE_MSG(p.lateral_m + l.lateral_offset_m + body <=
                            l.width_m * 0.5f + 1e-3f,
                        "a parked body hangs off the far side of the kerb",
                        "kerb-line");
        }
    }
    // WHAT THE KERB STILL COSTS, stated rather than hidden. A 14 m Street is
    // genuinely about 10 cm too narrow for a box truck to pass a parked box
    // truck, so that one pairing has to go round — which is the behaviour the
    // rest of this suite proves works. Every other pairing drives past at
    // cruise, and if that ever stops being true the kerb has become a wall
    // again for a much larger share of traffic.
    const CivilianManeuverTuning civ;
    std::size_t must_go_round = 0;
    float worst_intrusion = 0.0f;
    for (LaneRef lr = 0; lr < map.lanes.lane_count(); ++lr) {
        const Lane& l = map.lanes.lane(lr);
        const ParkedLaneBay bay = parked_lane_bay(l, ambient);
        for (uint32_t slot = 0; slot < bay.slots; ++slot) {
            const float body =
                traffic_vehicle_footprint(parked_vehicle_kind(l.key, slot)).half_width_m;
            bool any = false;
            for (TrafficVehicleKind dk : driver_kinds()) {
                const float intrusion = traffic_vehicle_footprint(dk).half_width_m +
                                        body + civ.parked_corridor_margin_m -
                                        bay.lateral_m;
                if (intrusion <= 0.0f) continue;
                any = true;
                worst_intrusion = std::max(worst_intrusion, intrusion);
            }
            if (any) ++must_go_round;
        }
    }
    std::printf("      %zu bays, %zu parked cars: worst REAL clearance %.3f m "
                "against the %.2f m gate; %zu (%.1f%%) must be gone round, "
                "worst intrusion %.3f m\n",
                bays, cars, double(worst_clearance),
                double(ambient.parked_lane_clearance_m), must_go_round,
                100.0 * double(must_go_round) / double(cars ? cars : 1),
                double(worst_intrusion));
    REQUIRE_MSG(worst_intrusion < 0.25f,
                "a parked body reaches so far into the driving corridor that "
                "edging round it is no longer a nudge",
                "intrusion");
    REQUIRE_MSG(bays > 200 && cars > 2000,
                "the honest gate emptied the island's kerbs", "vacuity");
    REQUIRE_MSG(worst_clearance >= ambient.parked_lane_clearance_m - 1e-3f,
                "a bay passed the clearance gate but the body actually put "
                "there is closer to the lane than the gate allows",
                "clearance");
    pass("a kerbside bay clears the lane by the gate, measured on the body "
         "that is really parked in it");
}

// ---------------------------------------------------------------------------
//  2 + 3. the live claim: a driver gets past
// ---------------------------------------------------------------------------

// One straight Street with a real kerbside population, and one car whose body
// is the widest that drives. Returns how long it sat still; the station it
// reached is asserted by the caller.
struct StreetRun {
    float reached = 0.0f;
    float target = 0.0f;
    float longest_stall_s = 0.0f;
    bool passed = false;
    std::size_t moves = 0;
    std::size_t retired = 0;
};

StreetRun drive_past_a_parked_truck(float parked_density) {
    StreetRun out;
    RoadSpine road;
    road.id = 1;
    road.cls = RoadClass::Street;
    road.points = {{0, 0}, {600, 0}};
    road.traffic_density = 0.0f;  // nothing on the road but the car we seed
    road.ped_density = 0.0f;
    road.parked_density = parked_density;
    RoadGraph roads;
    LaneGraph lanes;
    GroundSampler ground;
    roads.build({road}, {}, ground);
    lanes.build(roads, ground);
    const LaneRef lr = lanes.nearest_lane_along({200, 2}, {1, 0}).lane;
    REQUIRE(lanes.valid(lr));

    Crowd crowd;
    CrowdTuning tuning;
    tuning.max_peds = 0;
    tuning.police.patrol_fraction = 0;
    crowd.build(lanes, 905, {}, tuning);
    crowd.refresh(0, {200, 0});

    const Lane& lane = lanes.lane(lr);
    // Stage on a parked BOX TRUCK: the widest body that parks, and the only
    // one that reaches into a box truck's corridor on a 14 m Street.
    for (const AmbientParkedCar& c : crowd.ambient_parked()) {
        if (c.lane_key != lane.key || c.kind != TrafficVehicleKind::BoxTruck) continue;
        const auto proj = lanes.project_onto(lr, {c.pos.x, c.pos.z});
        if (!proj.valid() || proj.dist_along_m < 150.0f || proj.dist_along_m > 300.0f)
            continue;
        out.target = proj.dist_along_m;
        break;
    }
    REQUIRE_MSG(out.target > 0.0f, "no parked box truck was staged", "stage");

    uint32_t slot = 0;
    for (; slot < 4096; ++slot)
        if (traffic_vehicle_kind(lane.key, slot) == TrafficVehicleKind::BoxTruck) break;
    REQUIRE(slot < 4096);

    auto& cars = const_cast<std::vector<VehicleAgent>&>(crowd.vehicles());
    VehicleAgent v;
    v.lane = lr;
    v.lane_key = lane.key;
    v.slot = slot;
    v.dist_along_m = v.last_dist_m = out.target - 60.0f;
    v.speed_mps = v.cruise_mps = 9.0f;
    v.mode = AgentMode::Integrating;
    const auto pose = lanes.pose(lr, v.dist_along_m);
    v.pos = pose.position;
    v.fwd = pose.tangent;
    cars.push_back(v);

    float stalled_s = 0.0f;
    for (int64_t step = 1; step < 7200; ++step) {  // 60 s
        crowd.set_police_context(0, glm::vec2{5000, 5000}, {});
        crowd.rebuild_buckets();
        crowd.step_vehicles(step);
        REQUIRE_MSG(crowd.stats().ai_collisions == 0,
                    "the car hit something getting past the kerb", "contact");
        if (cars.empty()) break;
        const VehicleAgent& a = cars[0];
        stalled_s = a.speed_mps < 0.3f ? stalled_s + float(kSimDt) : 0.0f;
        out.longest_stall_s = std::max(out.longest_stall_s, stalled_s);
        out.reached = a.dist_along_m;
        if (a.dist_along_m > out.target + 8.0f) { out.passed = true; break; }
    }
    out.retired = crowd.stats().retired;
    out.moves = crowd.stats().civilian_overtakes + crowd.stats().nudges;
    return out;
}

void a_box_truck_gets_past_a_parked_box_truck() {
    // Both a full kerb and a half-full one. The dense bay is where the sweep
    // veto bit (there is a parked body beside you wherever you stop) and the
    // sparse bay is where the arc used to merge back onto the next bumper.
    for (float density : {1.0f, 0.5f}) {
        const StreetRun r = drive_past_a_parked_truck(density);
        std::printf("      density %.2f: reached %.1f m (parked truck at "
                    "%.1f m), longest stall %.1f s, %zu lateral moves, "
                    "%zu retired\n",
                    double(density), double(r.reached), double(r.target),
                    double(r.longest_stall_s), r.moves, r.retired);
        REQUIRE_MSG(r.passed,
                    "a box truck never got past a parked box truck on a "
                    "Street — the kerb is not a wall",
                    "stuck");
        // It may pause and think about it. It may not sit there.
        REQUIRE_MSG(r.longest_stall_s < 25.0f,
                    "the driver stopped dead at the kerb for longer than any "
                    "driver would wait",
                    "stall");
        // Getting past by evaporating is not getting past.
        REQUIRE_MSG(r.retired == 0,
                    "the car was despawned rather than driven past the kerb",
                    "despawn");
        // And it should not have to weave down the whole street to do it.
        REQUIRE_MSG(r.moves > 0 && r.moves < 8,
                    "the driver either never moved aside or thrashed doing it",
                    "moves");
    }
    pass("a box truck waits, edges round a parked box truck and carries on");
}

// ---------------------------------------------------------------------------
//  the same claim on the authored island, where the queues are
// ---------------------------------------------------------------------------

void the_authored_city_leaves_nobody_wedged_at_a_kerb() {
    RealMap map;
    AmbientTuning ambient;
    ambient.vehicle_spacing_m = 48.0f;
    ambient.max_vehicle_slots = 16;
    CrowdTuning tuning;
    tuning.max_peds = 0;

    Crowd crowd;
    crowd.build(map.lanes, city::kMapSeed, ambient, tuning);
    crowd.refresh(0, {0.0f, 0.0f});
    REQUIRE(crowd.vehicles().size() > 100u);

    // jam_steps is the recovery ladder's own clock: it runs while a car is
    // stopped at its stand-off behind something that is not going anywhere.
    // A car that is genuinely waiting for a light never accumulates it.
    int64_t worst_jam_steps = 0;
    std::size_t peak_wedged = 0;
    const int64_t end_step = tuning.signal_period_steps * 3;
    for (int64_t step = 0; step <= end_step; ++step) {
        crowd.rebuild_buckets();
        crowd.step_vehicles(step);
        std::size_t wedged = 0;
        for (const VehicleAgent& c : crowd.vehicles()) {
            worst_jam_steps = std::max(worst_jam_steps, c.jam_steps);
            if (c.jam_steps > 1200) ++wedged;  // stopped behind it for 10 s
        }
        peak_wedged = std::max(peak_wedged, wedged);
    }
    const double worst_s = double(worst_jam_steps) / 120.0;
    std::printf("      %zu cars over %lld steps: peak wedged %zu, worst jam "
                "%.1f s, %zu overtakes, %zu nudges, %zu despawns\n",
                crowd.vehicles().size(), static_cast<long long>(end_step),
                peak_wedged, worst_s, crowd.stats().civilian_overtakes,
                crowd.stats().nudges, crowd.stats().jam_despawns);
    // Before the fix this ran to two permanently wedged box trucks and a
    // 28 s jam, with the queue behind one of them backing up through a
    // junction it had already committed to.
    REQUIRE_MSG(peak_wedged == 0,
                "a car spent more than ten seconds wedged behind something "
                "static on the authored map",
                "wedged");
    REQUIRE_MSG(worst_s < 15.0,
                "some driver's blocked-behind-something clock ran far past "
                "anything a kerb should cost",
                "jam");
    pass("the authored island's traffic gets past its own kerbs");
}

}  // namespace

int main() {
    std::printf("parked_corridor_tests\n");
    the_widest_parked_body_is_the_widest_body_that_parks();
    a_bay_exists_only_where_the_real_body_clears_the_lane();
    a_box_truck_gets_past_a_parked_box_truck();
    the_authored_city_leaves_nobody_wedged_at_a_kerb();
    return apricot_test::done("parked_corridor_tests");
}
