// How strong the police are allowed to be.
//
// Asked for on 2026-09-13: police slammed into the player at speeds no car in
// the game could match. Two different things made that true, and this suite
// pins both. The police CARS out-ran most of the roster — every Municipal 91
// cruiser beat all but a handful of civilian cars on top speed, which no V8
// police sedan of 1991 did. And nothing bounded how fast a pursuer was
// closing when it made contact, on any of the three paths that drive at the
// suspect.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <vector>

#include <glm/gtc/quaternion.hpp>

#include "app/driving_mechanics.h"
#include "app/player_car_catalog.h"
#include "app/vehicle_model_tuning.h"
#include "city/police_ai.h"
#include "city/traffic_ai.h"
#include "core/fixed_step.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "road/lane_graph.h"
#include "test_assert.h"
#include "traffic/crowd.h"

using namespace apricot;

namespace {

constexpr float kDt = 1.f / 120.f;
constexpr float kHundredKmh = 100.f / 3.6f;

// "Not more overpowered than 30% of cars": at least this share of the
// civilian roster must match or beat every police car — on top speed and on
// the launch, each on its own.
constexpr float kShareThatMatchesPolice = .30f;

bool is_police_car(PlayerCarId id) {
    return is_municipal_cruiser_91(id) || id == PlayerCarId::LegacyCar5NextPolice;
}

struct Performance {
    float top_speed_mps = 0.f;
    float zero_to_100_s = std::numeric_limits<float>::infinity();
};

// Flat out on flat ground for up to a minute, the real tuning through the real
// step. Stops early once a whole three seconds adds under 0.02 m/s: every car
// here is at its redline or its drag limit by then, and nine styles of the
// full minute would cost the gate half a minute for nothing.
Performance measure(PlayerCarId id, DrivingMechanicsStyle style) {
    TerrainCollider ground(0xC4A5u);
    ground.add_static_ground_rect({0.f, 0.f}, 200.f, {6000.f, 6000.f},
                                  0.f, Surface::Rock);
    const VehicleTuning tuning = player_model_tuning(style, id);
    VehicleState car = spawn_vehicle(tuning, ground, 0.f, 0.f, 0.f);
    car.position.y = 200.f + static_ride_height(tuning);
    InputFrame input;
    input.throttle = 1.f;
    Performance out;
    float three_seconds_ago = 0.f;
    for (int step = 0; step < 60 * 120; ++step) {
        car = step_vehicle(car, tuning, input, ground, kDt);
        const float speed = std::abs(vehicle_speed(car));
        REQUIRE(std::isfinite(speed));
        out.top_speed_mps = std::max(out.top_speed_mps, speed);
        if (speed >= kHundredKmh && !std::isfinite(out.zero_to_100_s))
            out.zero_to_100_s = static_cast<float>(step + 1) * kDt;
        if ((step + 1) % (3 * 120) == 0) {
            if (step + 1 >= 12 * 120 &&
                out.top_speed_mps - three_seconds_ago < .02f) break;
            three_seconds_ago = out.top_speed_mps;
        }
    }
    return out;
}

// Police follow the player's driving style (App hands the free-driving pursuit
// the same style it drives), so the rule has to hold in every one of them,
// not just the default.
void police_cars_are_not_in_the_top_thirty_percent() {
    float slowest_police_top = std::numeric_limits<float>::infinity();
    std::size_t unmeasurable_styles = 0;
    for (std::size_t s = 0; s < kDrivingMechanicsStyleCount; ++s) {
        const auto style = static_cast<DrivingMechanicsStyle>(s);
        struct Row { PlayerCarId id; Performance p; };
        std::vector<Row> roster;
        std::size_t civilians = 0;
        Performance all_rounder;
        for (const auto& car : kPlayerCars) {
            // A fitted-equipment variant (the plow trucks) is its base truck
            // with a deliberately heavy work tune. Ranking it as another
            // civilian would count one slow body twice and raise the bar the
            // patrol cars must clear without adding a car that could race them.
            if (player_car_body_id(car.id) != car.id) continue;
            roster.push_back({car.id, measure(car.id, style)});
            if (!is_police_car(car.id)) ++civilians;
            if (car.id == PlayerCarId::LegacyCar5) all_rounder = roster.back().p;
        }
        // A STYLE THE ROSTER CANNOT LAUNCH IN HAS NOTHING TO RANK. Measured on
        // 2026-09-13, MUSCLE flat out from a standstill tops every car out at
        // 16-18 m/s, Car 5 included: first gear at that style's 6400 rpm
        // redline, because the automatic never shifts out of it. That is a
        // gearbox problem, not a police one, and ranking a field where every
        // car ties would pass vacuously — so it is reported and skipped, and
        // more than one such style fails the suite rather than hiding in it.
        if (!std::isfinite(all_rounder.zero_to_100_s)) {
            std::printf("    %s: SKIPPED, Car 5 never reaches 100 km/h flat out "
                        "(top %.1f m/s)\n",
                        driving_mechanics_name(style), all_rounder.top_speed_mps);
            ++unmeasurable_styles;
            continue;
        }
        const auto need = static_cast<std::size_t>(std::ceil(
            kShareThatMatchesPolice * static_cast<float>(civilians)));
        std::printf("    %s: %zu of %zu civilian cars must match each police car\n",
                    driving_mechanics_name(style), need, civilians);
        for (const Row& police : roster) {
            if (!is_police_car(police.id)) continue;
            std::size_t as_fast = 0, as_quick = 0;
            for (const Row& civilian : roster) {
                if (is_police_car(civilian.id)) continue;
                if (civilian.p.top_speed_mps >= police.p.top_speed_mps) ++as_fast;
                if (civilian.p.zero_to_100_s <= police.p.zero_to_100_s) ++as_quick;
            }
            const char* model = player_car_definition(police.id).model;
            std::printf("      %-22s top %5.1f m/s (%zu as fast)  0-100 %4.2f s "
                        "(%zu as quick; Car 5 %4.2f s)\n",
                        model, police.p.top_speed_mps, as_fast,
                        police.p.zero_to_100_s, as_quick,
                        all_rounder.zero_to_100_s);
            REQUIRE_MSG(as_fast >= need,
                        "a police car out-runs more than 70% of the roster", model);
            REQUIRE_MSG(as_quick >= need,
                        "a police car out-launches more than 70% of the roster", model);
            // A 1991 police sedan was a big V8 on tall gearing: quick for a
            // sedan, never a sports car off the line.
            REQUIRE_MSG(police.p.zero_to_100_s >= all_rounder.zero_to_100_s,
                        "a 1991 police sedan launches harder than the Car 5 all-rounder",
                        model);
            slowest_police_top = std::min(slowest_police_top, police.p.top_speed_mps);
        }
    }
    REQUIRE_MSG(unmeasurable_styles <= 1,
                "more than one driving style cannot launch flat out", "styles");
    // The lane-following pursuer is not stepped as a car at all, so nothing
    // physical stops its catch-up target. It must never ask for more than the
    // slowest police car can actually do.
    REQUIRE(police_pursuit_cruise_mps(40.f, 200.f, 5) <= slowest_police_top);
    apricot_test::pass("no police car out-runs or out-launches 70% of the roster, in any style");
}

void contact_speed_kernel() {
    const PoliceTuning t;
    // At contact: the suspect plus the allowance, exactly.
    REQUIRE_NEAR(police_contact_speed_mps(12.f, 0.f, t),
                 12.f + t.contact_closing_mps, 1e-5f);
    // Further back it only opens up...
    float previous = police_contact_speed_mps(12.f, 0.f, t);
    for (float gap : {1.f, 5.f, 20.f, 80.f}) {
        const float now = police_contact_speed_mps(12.f, gap, t);
        REQUIRE(now > previous);
        previous = now;
    }
    // ...and by exactly as much as braking at contact_brake_mps2 sheds.
    const float closing = police_contact_speed_mps(12.f, 30.f, t) - 12.f;
    REQUIRE_NEAR(closing * closing - 2.f * t.contact_brake_mps2 * 30.f,
                 t.contact_closing_mps * t.contact_closing_mps, 1e-2f);
    // Overlapping already is contact, and a suspect coming the other way
    // counts as stopped: the cap never asks a car to reverse.
    REQUIRE_NEAR(police_contact_speed_mps(12.f, -3.f, t),
                 12.f + t.contact_closing_mps, 1e-5f);
    REQUIRE_NEAR(police_contact_speed_mps(-9.f, 0.f, t), t.contact_closing_mps, 1e-5f);
    // Garbage in fails to the tightest answer, not to no cap.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    REQUIRE_NEAR(police_contact_speed_mps(12.f, nan, t),
                 12.f + t.contact_closing_mps, 1e-5f);
    REQUIRE_NEAR(police_contact_speed_mps(nan, 0.f, t), t.contact_closing_mps, 1e-5f);
    // The approach is planned on a deceleration every driver can deliver.
    for (auto kind : {DriverProfileKind::Cautious, DriverProfileKind::Normal,
                      DriverProfileKind::Impatient, DriverProfileKind::AggressiveLite})
        REQUIRE(make_driver_profile(kind).brake >= t.contact_brake_mps2);
    apricot_test::pass("contact speed is the allowance at contact and a braking curve behind it");
}

void contact_limit_on_the_free_drive_command() {
    PursuitCmd floor_it;
    floor_it.throttle = 1.f;
    floor_it.steer = .4f;
    // Under the limit the command is the command.
    PursuitCmd c = police_limit_contact_closing(floor_it, .9f, 10.f, 14.f);
    REQUIRE(c.throttle == 1.f && c.brake == 0.f && c.steer == .4f);
    // Over it the throttle lifts and the brake scales with the surplus, while
    // the car keeps steering at the target.
    c = police_limit_contact_closing(floor_it, .9f, 14.5f, 14.f);
    REQUIRE(c.throttle == 0.f && c.brake > 0.f && c.brake < 1.f && c.steer == .4f);
    REQUIRE(police_limit_contact_closing(floor_it, .9f, 30.f, 14.f).brake == 1.f);
    // A suspect behind the car is a turn-around, not a ram.
    PursuitCmd turning;
    turning.throttle = 1.f;
    turning.steer = 1.f;
    REQUIRE(police_limit_contact_closing(turning, -.5f, 30.f, 14.f).throttle == 1.f);
    apricot_test::pass("the free-drive command lifts and brakes only when closing too fast");
}

struct ContactRun {
    float worst_closing_mps = 0.f;
    float worst_first_touch_mps = 0.f;  // only steps that START a contact
    float closest_m = std::numeric_limits<float>::infinity();
    unsigned contact_steps = 0;
    bool rammed = false;
    bool free_drove = false;
};

// The road and the slab under it sit where the tuning rigs put their ground,
// 200 m up, so the only thing under a free-driving cruiser is the slab.
constexpr float kRigHeightM = 200.f;

// The slam, set up on purpose: a pursuing cruiser doing 30 m/s sixty metres
// behind a suspect doing 12, at three stars, where the player-hazard brake is
// switched off for pursuers. The real Crowd drives it; the real player solver
// reports the closing speed, which is the number body damage reads. With a
// world the unit may also free-drive the last 22 m as the 91-C.
ContactRun run_contact(bool free_drive_world, float contact_closing_mps) {
    RoadGraph roads;
    LaneGraph lanes;
    RoadSpine road;
    road.id = 1;
    road.cls = RoadClass::Arterial;
    road.points = {{0.f, 0.f}, {900.f, 0.f}};
    GroundSampler ground;
    ground.fn = [](const void*, float, float) { return kRigHeightM; };
    roads.build({road}, {}, ground);
    lanes.build(roads, ground);
    const LaneRef lane = lanes.nearest_lane_along({200.f, 2.f}, {1.f, 0.f}).lane;
    REQUIRE(lanes.valid(lane));

    CrowdTuning tuning;
    tuning.max_peds = 0;
    tuning.police.patrol_fraction = 0.f;
    tuning.police.contact_closing_mps = contact_closing_mps;
    Crowd crowd;
    crowd.build(lanes, 905, {}, tuning);
    TerrainCollider world(0xC4A5u);
    world.add_static_ground_rect({450.f, 0.f}, kRigHeightM, {2000.f, 2000.f},
                                 0.f, Surface::Rock);
    crowd.set_police_vehicle_tuning(player_model_tuning(
        DrivingMechanicsStyle::ClassicGta, PlayerCarId::MunicipalCruiser91C));

    VehicleAgent cop;
    cop.lane = lane;
    cop.lane_key = lanes.lane(lane).key;
    cop.slot = 1;
    cop.dist_along_m = cop.last_dist_m = 100.f;
    cop.speed_mps = cop.cruise_mps = 30.f;
    cop.mode = AgentMode::Integrating;
    cop.police_unit = cop.police_pursuit = true;
    const LanePose start = lanes.pose(lane, 100.f);
    cop.pos = start.position;
    cop.fwd = start.tangent;
    const_cast<std::vector<VehicleAgent>&>(crowd.vehicles()) = {cop};
    std::vector<VisiblePoliceIdentity> seen;
    seen.push_back({cop.lane_key, cop.slot});

    constexpr float kSuspectMps = 12.f;
    float station = 160.f;
    VehicleState player;
    ContactRun out;
    bool was_touching = false;
    for (int64_t step = 0;
         step < 120 * 30 && station < lanes.length(lane) - 30.f; ++step) {
        const LanePose here = lanes.pose(lane, station);
        player.position = here.position;
        player.orientation = glm::angleAxis(
            std::atan2(-here.tangent.x, -here.tangent.z), glm::vec3{0.f, 1.f, 0.f});
        player.velocity = here.tangent * kSuspectMps;
        crowd.set_police_context(3, {player.position.x, player.position.z}, seen);
        crowd.set_police_officer_context(false, false,
            {player.velocity.x, player.velocity.z},
            free_drive_world ? &world : nullptr);
        crowd.rebuild_buckets();
        crowd.step_vehicles(step, &player);
        for (const auto& v : crowd.vehicles()) {
            out.rammed |= v.maneuver.kind == TrafficManeuverKind::PoliceRam;
            out.free_drove |= v.chase_active;
            out.closest_m = std::min(out.closest_m, glm::distance(
                glm::vec2{v.pos.x, v.pos.z},
                glm::vec2{player.position.x, player.position.z}));
        }
        VehicleState probe = player;
        probe.car_contact_speed = 0.f;
        const bool touching = crowd.resolve_player_collision(probe);
        if (touching) {
            ++out.contact_steps;
            out.worst_closing_mps = std::max(out.worst_closing_mps,
                                             probe.car_contact_speed);
            if (!was_touching)
                out.worst_first_touch_mps = std::max(out.worst_first_touch_mps,
                                                     probe.car_contact_speed);
        }
        was_touching = touching;
        station += kSuspectMps * static_cast<float>(kSimDt);
    }
    return out;
}

void a_pursuer_arrives_at_contact_speed() {
    const PoliceTuning defaults;
    for (bool free_drive : {false, true}) {
        const char* mode = free_drive ? "free-drive" : "lane and ram";
        const ContactRun capped = run_contact(free_drive, defaults.contact_closing_mps);
        const ContactRun uncapped = run_contact(free_drive, 1000.f);
        std::printf("      %-12s capped: worst closing %5.2f m/s (first touch %5.2f) over"
                    " %u contact steps%s%s; uncapped: %5.2f m/s (first touch %5.2f)\n",
                    mode, capped.worst_closing_mps, capped.worst_first_touch_mps,
                    capped.contact_steps, capped.rammed ? ", rammed" : "",
                    capped.free_drove ? ", free-drove" : "",
                    uncapped.worst_closing_mps, uncapped.worst_first_touch_mps);
        // The negative control. A rig that cannot reproduce the slam with the
        // cap switched off proves nothing about the cap.
        REQUIRE_MSG(uncapped.worst_closing_mps > 10.f,
                    "the rig never reproduced the slam", mode);
        REQUIRE_MSG(capped.contact_steps > 0,
                    "the cap stopped the pursuer reaching the suspect at all", mode);
        // Measured on 2026-09-13: 5.00 m/s on the lane and ram paths, 5.39
        // free-driving (a real car brakes a step behind its plan), against
        // 24.9 and 21.4 with the cap off. A metre a second of slack covers the
        // car; anything past it is the slam coming back.
        REQUIRE_MSG(capped.worst_closing_mps <= defaults.contact_closing_mps + 1.0f,
                    "a pursuer hit harder than contact_closing_mps", mode);
        if (free_drive)
            REQUIRE_MSG(capped.free_drove, "the free-drive case never free-drove", mode);
    }
    apricot_test::pass("a pursuer still reaches the suspect, at contact_closing_mps");
}

}  // namespace

int main() {
    std::puts("police_performance_tests");
    contact_speed_kernel();
    contact_limit_on_the_free_drive_command();
    police_cars_are_not_in_the_top_thirty_percent();
    a_pursuer_arrives_at_contact_speed();
}
