// The two directions a car and a person on foot can meet.
//
// Before city/body_damage.h the city was asymmetric in a way a player can feel
// and cannot explain: the player's bumper could floor anybody in Pinatty, and
// every car in Pinatty would BRAKE for a player on foot but could not touch
// one. You could stand in the middle of a dual carriageway indefinitely.
//
// The fist is the same story and is pinned in weapon_hit_tests, beside the
// pistol whose query it shares.

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

#include "city/body_damage.h"
#include "physics/vehicle.h"
#include "road/road_graph.h"
#include "test_assert.h"
#include "traffic/crowd.h"

using namespace apricot;
namespace {

struct Road {
    RoadGraph roads;
    LaneGraph lanes;
    LaneRef east = kInvalidLane;
    Road() {
        RoadSpine spine;
        spine.id = 704;
        spine.cls = RoadClass::Street;
        spine.points = {{-600.0f, 0.0f}, {600.0f, 0.0f}};
        float height = 0.0f;
        GroundSampler ground;
        ground.ctx = &height;
        ground.fn = [](const void* value, float, float) {
            return *static_cast<const float*>(value);
        };
        roads.build({spine}, {}, ground);
        lanes.build(roads, ground);
        for (LaneRef r = 0; r < lanes.lane_count(); ++r)
            if (lanes.pose(r, 0).tangent.x > 0.9f) east = r;
        REQUIRE(lanes.valid(east));
    }
};

VehicleAgent seed_car(const Road& road, float speed) {
    VehicleAgent car;
    car.lane = road.east;
    car.lane_key = road.lanes.lane(car.lane).key;
    car.slot = 11;
    car.dist_along_m = 560.0f;
    car.last_dist_m = car.dist_along_m;
    car.speed_mps = speed;
    car.cruise_mps = speed;
    car.mode = AgentMode::Integrating;
    const LanePose pose = road.lanes.pose(car.lane, car.dist_along_m);
    car.pos = pose.position;
    car.fwd = pose.tangent;
    return car;
}

Crowd make_crowd(const Road& road, const VehicleAgent& car) {
    Crowd crowd;
    CrowdTuning tuning;
    tuning.max_peds = 0;
    crowd.build(road.lanes, 6112, {}, tuning);
    const_cast<std::vector<VehicleAgent>&>(crowd.vehicles()) = {car};
    return crowd;
}

// One run of a car at 14 m/s toward somebody standing `gap` metres ahead of
// it, in its lane. Returns the closing speed of the contact, or -1 for no
// contact at all.
struct Approach {
    bool hit = false;
    float closing = -1.0f;
    glm::vec2 travel{0.0f};
    bool police = false;
    float closest_m = 1e9f;
};

Approach drive_at_player(float gap_m, bool police_unit = false) {
    Road road;
    VehicleAgent car = seed_car(road, 14.0f);
    car.police_unit = police_unit;
    car.police_pursuit = police_unit;
    Crowd crowd = make_crowd(road, car);
    const glm::vec3 start = crowd.vehicles().front().pos;
    OnFootTrafficHazard foot;
    foot.position = {start.x + gap_m, start.z};
    foot.height_m = start.y;

    Approach out;
    for (int64_t step = 0; step < 1200 && !out.hit; ++step) {
        crowd.rebuild_buckets();
        crowd.step_vehicles(step, nullptr, &foot);
        const auto& v = crowd.vehicles().front();
        out.closest_m = std::min(out.closest_m, foot.position.x - v.pos.x);
        for (const auto& event : crowd.on_foot_player_hits()) {
            out.hit = true;
            out.closing = event.closing_speed_mps;
            out.travel = event.travel_xz;
            out.police = event.police_unit;
            REQUIRE(event.lane_key == v.lane_key && event.slot == v.slot);
        }
    }
    return out;
}

// THE PLAYER CAN BE RUN OVER — and, just as importantly, a driver with room to
// stop still stops. Both halves are here because either one alone is a
// different bug: without the first the player is invulnerable standing in a
// dual carriageway, and without the second every car in the city is a battering
// ram that ignores the hazard kernel it spends its whole life obeying.
void stepping_out_in_front_of_a_car_is_lethal_and_standing_ahead_of_one_is_not() {
    // Thirty metres of warning: the car sees the hazard, brakes, and holds
    // station a body's length short. Nobody is touched.
    const Approach patient = drive_at_player(30.0f);
    REQUIRE_MSG(!patient.hit,
                "a car with thirty metres of warning ran the player over "
                "instead of braking", "patient");
    REQUIRE_MSG(patient.closest_m > 2.0f,
                "a braking car parked itself inside the player", "patient");

    // Three metres of warning at 14 m/s. There is no stopping distance, and
    // the contact lands at very nearly the speed the car was doing.
    const Approach sudden = drive_at_player(3.0f);
    REQUIRE_MSG(sudden.hit, "a car three metres away at 14 m/s missed",
                "sudden");
    REQUIRE_MSG(sudden.closing > 10.0f,
                "an unavoidable impact reported a crawl", "sudden");
    REQUIRE(sudden.closing <= 14.0f + 1e-3f);
    REQUIRE_NEAR(std::sqrt(sudden.travel.x * sudden.travel.x +
                           sudden.travel.y * sudden.travel.y), 1.0f, 1e-3);
    REQUIRE_MSG(sudden.travel.x > 0.9f,
                "the blow does not point down the road the car was travelling",
                "direction");
    REQUIRE(!sudden.police);

    // And it costs what it costs on the SAME curve a pedestrian is hit on.
    // Thirty miles an hour kills whoever is standing in front of it.
    const float damage = vehicle_impact_damage(sudden.closing, 2.5f);
    REQUIRE_MSG(damage >= kBodyHealth,
                "being hit at over thirty miles an hour was survivable",
                "lethal");
    std::printf("      patient car stopped %.2f m short; sudden impact %.2f m/s "
                "for %.0f damage\n",
                static_cast<double>(patient.closest_m),
                static_cast<double>(sudden.closing),
                static_cast<double>(damage));
    apricot_test::pass("a car that can stop does, and one that cannot kills you");
}

// A pursuing cruiser is not exempt. Being run down by the police is a way to
// die, and carving them out would be a rule the player can feel and cannot
// explain.
void a_pursuing_cruiser_can_run_the_player_down() {
    const Approach chase = drive_at_player(3.0f, /*police_unit=*/true);
    REQUIRE_MSG(chase.hit, "a pursuing cruiser drove through the suspect",
                "police");
    REQUIRE(chase.police);
    REQUIRE(chase.closing > 10.0f);
    apricot_test::pass("a cruiser in pursuit runs the player down like anybody else");
}

// The other half of the footprint guard: a player on the PAVEMENT, beside the
// same lane, must not be reported as run over. Without this, the tests above
// pass for a footprint the size of the island.
void traffic_misses_a_player_on_the_pavement() {
    Road road;
    Crowd crowd = make_crowd(road, seed_car(road, 14.0f));
    const glm::vec3 start = crowd.vehicles().front().pos;
    OnFootTrafficHazard foot;
    foot.position = {start.x + 3.0f, start.z + 6.0f};  // well clear laterally
    foot.height_m = start.y;
    for (int64_t step = 0; step < 900; ++step) {
        crowd.rebuild_buckets();
        crowd.step_vehicles(step, nullptr, &foot);
        REQUIRE_MSG(crowd.on_foot_player_hits().empty(),
                    "a car six metres to the side ran the player over",
                    "footprint");
    }
    apricot_test::pass("standing clear of the lane is standing clear");
}

// The event is an EDGE, like every other one in the crowd: read it on the step
// it happens or lose it. A tally would charge the player for one impact on
// every step they spend lying under the car.
void the_hit_event_does_not_accumulate() {
    Road road;
    Crowd crowd = make_crowd(road, seed_car(road, 14.0f));
    const glm::vec3 start = crowd.vehicles().front().pos;
    OnFootTrafficHazard foot;
    foot.position = {start.x + 3.0f, start.z};
    foot.height_m = start.y;
    std::size_t worst = 0;
    for (int64_t step = 0; step < 900; ++step) {
        crowd.rebuild_buckets();
        crowd.step_vehicles(step, nullptr, &foot);
        worst = std::max(worst, crowd.on_foot_player_hits().size());
    }
    REQUIRE_MSG(worst <= 1, "one car reported more than one hit in a step",
                "edge");
    apricot_test::pass("the hit list is refilled every step, never appended to");
}

}  // namespace

int main() {
    stepping_out_in_front_of_a_car_is_lethal_and_standing_ahead_of_one_is_not();
    a_pursuing_cruiser_can_run_the_player_down();
    traffic_misses_a_player_on_the_pavement();
    the_hit_event_does_not_accumulate();
    return apricot_test::done("player_casualty_tests");
}
