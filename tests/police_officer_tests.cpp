#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>
#include <vector>

#include "city/police_officer.h"
#include "core/fixed_step.h"
#include "game/character.h"
#include "physics/vehicle.h"
#include "road/road_graph.h"
#include "terrain/heightmap.h"
#include "test_assert.h"
#include "traffic/crowd.h"

using namespace apricot;
namespace {

struct Road {
    RoadGraph roads;
    LaneGraph lanes;
    LaneRef east = kInvalidLane;
    explicit Road(float height = 0.0f) {
        RoadSpine spine;
        spine.id = 981;
        spine.cls = RoadClass::Street;
        spine.points = {{-600.0f, 0.0f}, {600.0f, 0.0f}};
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

VehicleAgent seed_car(const Road& road, float speed = 6.0f) {
    VehicleAgent car;
    car.lane = road.east;
    car.lane_key = road.lanes.lane(car.lane).key;
    car.slot = 17;
    car.dist_along_m = 580.0f;
    car.last_dist_m = car.dist_along_m;
    car.speed_mps = speed;
    car.cruise_mps = 11.0f;
    car.mode = AgentMode::Integrating;
    car.police_unit = true;
    car.police_pursuit = true;
    const LanePose pose = road.lanes.pose(car.lane, car.dist_along_m);
    car.pos = pose.position;
    car.fwd = pose.tangent;
    car.officer.pos = car.officer.previous_pos = car.pos;
    return car;
}

Crowd make_crowd(const Road& road, const VehicleAgent& car) {
    Crowd crowd;
    CrowdTuning tuning;
    tuning.max_peds = 0;
    crowd.build(road.lanes, 905, {}, tuning);
    const_cast<std::vector<VehicleAgent>&>(crowd.vehicles()) = {car};
    return crowd;
}

void tick(Crowd& crowd, int64_t step, int wanted, glm::vec2 target,
          bool on_foot = true, float speed = 0.0f,
          const TerrainCollider* world = nullptr, bool armed = false) {
    std::vector<VisiblePoliceIdentity> visible;
    for (const auto& car : crowd.vehicles())
        if (car.police_unit) visible.push_back({car.lane_key, car.slot});
    crowd.set_police_context(wanted, target, visible);
    crowd.set_police_officer_context(on_foot, armed, {speed, 0.0f}, world);
    crowd.rebuild_buckets();
    crowd.step_vehicles(step);
}

void phase_machine_finishes_transitions_before_driving() {
    PoliceOfficerState officer;
    PoliceOfficerStepInput input;
    input.engaged = input.target_on_foot = input.safe_road_position = true;
    input.target_distance_m = 12.0f;
    input.vehicle_speed_mps = 8.0f;
    input.door_clear = true;
    step_police_officer_phase(officer, input);
    REQUIRE(officer.phase == PoliceOfficerPhase::Braking);
    for (int i = 0; i < 60; ++i) step_police_officer_phase(officer, input);
    REQUIRE(!officer.transition.active());
    input.vehicle_speed_mps = 0.0f;
    for (int i = 0; i < 23; ++i) step_police_officer_phase(officer, input);
    REQUIRE(!officer.transition.active());
    step_police_officer_phase(officer, input);
    REQUIRE(officer.phase == PoliceOfficerPhase::Exiting);
    for (int i = 0; i < 140; ++i) step_police_officer_phase(officer, input);
    const uint32_t tick_before_block = officer.transition.tick;
    input.door_clear = false;
    for (int i = 0; i < 90; ++i) step_police_officer_phase(officer, input);
    REQUIRE(officer.transition.tick == tick_before_block);
    input.door_clear = true;
    input.engaged = false;  // stand down with the person halfway through the door
    while (officer.transition.active()) {
        REQUIRE(!police_officer_driving_allowed(officer));
        step_police_officer_phase(officer, input);
    }
    REQUIRE(officer.phase == PoliceOfficerPhase::Returning);
    input.at_door = true;
    step_police_officer_phase(officer, input);
    REQUIRE(officer.phase == PoliceOfficerPhase::Entering);
    for (uint32_t i = 0; i + 1 < kVehicleTransitionTicks; ++i) {
        step_police_officer_phase(officer, input);
        REQUIRE(!police_officer_driving_allowed(officer));
    }
    step_police_officer_phase(officer, input);
    REQUIRE(police_officer_driving_allowed(officer));
    REQUIRE(police_officer_door_open(officer) == 0.0f);
    apricot_test::pass("moving cruisers stay occupied, blocked doors pause, and stand-down completes exit and entry");
}

void real_crowd_exits_walks_around_cruiser_and_returns() {
    Road road;
    const auto original = seed_car(road);
    Crowd crowd = make_crowd(road, original);
    glm::vec2 target{original.pos.x + 14.0f, original.pos.z};
    int64_t step = 0;
    bool exited = false;
    float parked_distance = 0.0f;
    int transition_ticks = 0;
    for (; step < 1500; ++step) {
        tick(crowd, step, 1, target);
        const auto& car = crowd.vehicles().front();
        if (car.officer.transition.active()) {
            ++transition_ticks;
            REQUIRE(car.speed_mps <= 0.08f);
        }
        if (car.officer.phase == PoliceOfficerPhase::Pursuing) {
            exited = true;
            parked_distance = car.dist_along_m;
            break;
        }
    }
    REQUIRE(exited);
    REQUIRE(transition_ticks >= static_cast<int>(kVehicleTransitionTicks));
    const glm::vec3 parked = crowd.vehicles().front().pos;
    // Driver exits north; target south forces a real route around the car.
    target = {parked.x, parked.z + 8.0f};
    for (int i = 0; i < 750; ++i) {
        tick(crowd, ++step, 1, target);
        const auto& car = crowd.vehicles().front();
        REQUIRE(car.speed_mps == 0.0f);
        REQUIRE(std::fabs(car.dist_along_m - parked_distance) < 0.001f);
        const glm::vec3 d = car.officer.pos - car.pos;
        REQUIRE(std::fabs(d.x) > 2.8f || std::fabs(d.z) > 1.35f);
        REQUIRE(glm::distance(car.officer.previous_pos, car.officer.pos) < 0.05f);
    }
    REQUIRE(crowd.vehicles().front().officer.pos.z > parked.z + 4.0f);
    REQUIRE(crowd.vehicles().front().officer.distance_walked_m > 8.0f);
    REQUIRE(glm::distance(glm::vec2{crowd.vehicles().front().officer.pos.x,
                                   crowd.vehicles().front().officer.pos.z}, target) < 2.0f);
    VehicleAgent taken;
    REQUIRE(!crowd.take_vehicle(original.lane_key, original.slot, taken));
    bool returned = false;
    bool entered = false;
    for (int i = 0; i < 1800; ++i) {
        tick(crowd, ++step, 0, target, false, 12.0f);
        const auto& car = crowd.vehicles().front();
        entered |= car.officer.phase == PoliceOfficerPhase::Entering;
        if (car.officer.phase == PoliceOfficerPhase::Seated) {
            returned = true;
            REQUIRE(police_officer_door_open(car.officer) == 0.0f);
            break;
        }
        REQUIRE(car.speed_mps == 0.0f);
        REQUIRE(std::fabs(car.dist_along_m - parked_distance) < 0.001f);
    }
    REQUIRE(returned && entered);
    for (int i = 0; i < 60; ++i) tick(crowd, ++step, 0, target, false, 12.0f);
    REQUIRE(crowd.vehicles().front().speed_mps > 0.1f);
    apricot_test::pass("actual Crowd officer brakes, exits, routes around cruiser, returns, enters, and resumes patrol");
}

void streamed_officer_lifecycle_is_scan_order_independent() {
    Road road;
    Crowd forward, reverse;
    CrowdTuning tuning;
    tuning.max_peds = 0;
    tuning.vehicle_activate_m = 65.0f;
    tuning.vehicle_retire_m = 130.0f;
    tuning.police.patrol_fraction = 1.0f;
    AmbientTuning ambient;
    ambient.vehicle_spacing_m = 22.0f;
    forward.build(road.lanes, 714, ambient, tuning);
    tuning.reverse_scan_order = true;
    reverse.build(road.lanes, 714, ambient, tuning);
    bool walked = false, entering = false;
    for (int64_t step = 0; step < 2600; ++step) {
        if (step % 8 == 0) {
            forward.refresh(step, {0.0f, 0.0f});
            reverse.refresh(step, {0.0f, 0.0f});
        }
        const int wanted = step < 1200 ? 1 : 0;
        tick(forward, step, wanted, {0.0f, 0.0f});
        tick(reverse, step, wanted, {0.0f, 0.0f});
        REQUIRE(forward.population_hash() == reverse.population_hash());
        for (const auto& car : forward.vehicles()) {
            walked |= police_officer_on_foot(car.officer);
            entering |= car.officer.phase == PoliceOfficerPhase::Entering;
        }
    }
    REQUIRE(walked && entering);
    apricot_test::pass("streamed patrol exits and return transitions keep bit-identical state under reverse scans");
}

void world_geometry_blocks_exits_and_walks() {
    Road road(50.0f);
    const auto car = seed_car(road, 0.0f);
    Crowd crowd = make_crowd(road, car);
    TerrainCollider world(905);
    world.add_static_ground_rect({0.0f, 0.0f}, 50.0f, {700.0f, 100.0f}, 0.0f);
    // Obstruction spans the entire driver's side door/capsule sweep.
    world.add_static_box({car.pos + glm::vec3{-2.0f, 0.0f, -2.1f},
                          car.pos + glm::vec3{2.0f, 2.0f, -0.9f}});
    for (int64_t step = 0; step < 600; ++step)
        tick(crowd, step, 1, {car.pos.x + 10.0f, car.pos.z}, true, 0.0f, &world);
    REQUIRE(crowd.vehicles().front().officer.phase == PoliceOfficerPhase::Braking);
    REQUIRE(!crowd.vehicles().front().officer.transition.active());
    world.clear_static_boxes();
    int64_t step = 600;
    while (step < 1200 && !police_officer_on_foot(crowd.vehicles().front().officer))
        tick(crowd, step++, 1, {car.pos.x + 10.0f, car.pos.z}, true, 0.0f, &world);
    REQUIRE(police_officer_on_foot(crowd.vehicles().front().officer));
    const glm::vec3 start = crowd.vehicles().front().officer.pos;
    world.add_static_box({start + glm::vec3{1.0f, 0.0f, -30.0f},
                          start + glm::vec3{1.4f, 3.0f, 30.0f}});
    for (int i = 0; i < 500; ++i) {
        tick(crowd, step++, 1, {start.x + 10.0f, start.z}, true, 0.0f, &world);
        const auto& officer = crowd.vehicles().front().officer;
        REQUIRE(officer.pos.x < start.x + 1.0f - 0.30f);
        REQUIRE(std::fabs(officer.pos.y - 50.0f) < 0.001f);
        REQUIRE(character_position_clear(world, officer.pos, {}));
    }
    apricot_test::pass("live world supports officer feet and blocks door sweeps and on-foot wall crossings");
}

void police_contacts_are_pre_impulse_and_do_not_delete_officers() {
    Road road;
    auto car = seed_car(road, 0.0f);
    car.police_pursuit = false;
    Crowd crowd = make_crowd(road, car);
    VehicleState player;
    player.position = car.pos + glm::vec3{-4.8f, 0.0f, 0.0f};
    player.orientation = glm::angleAxis(-1.57079633f, glm::vec3{0.0f, 1.0f, 0.0f});
    player.velocity = {1.4f, 0.0f, 0.0f};
    REQUIRE(crowd.resolve_player_collision(player));
    REQUIRE(crowd.police_player_contacts().size() == 1u);
    const auto contact = crowd.police_player_contacts().front();
    REQUIRE(contact.lane_key == car.lane_key && contact.slot == car.slot);
    REQUIRE(std::fabs(contact.player_velocity.x - 1.4f) < 0.001f);
    REQUIRE(glm::length(contact.police_velocity) < 0.001f);
    REQUIRE(contact.normal.x < -0.9f);
    crowd.set_police_context(1, glm::vec2{player.position.x, player.position.z}, {});
    REQUIRE(crowd.police_pursuit_count() == 0u);
    REQUIRE(crowd.report_police_vehicle_hit({contact.lane_key, contact.slot}));
    REQUIRE(crowd.vehicles().front().police_pursuit);
    crowd.rebuild_buckets();
    crowd.step_vehicles(0);
    REQUIRE(!crowd.vehicles().front().police_route.empty());
    player.position = car.pos + glm::vec3{0.0f, 0.0f, 40.0f};
    REQUIRE(!crowd.resolve_player_collision(player));
    REQUIRE(crowd.police_player_contacts().empty());
    apricot_test::pass("actual rear hit exposes pre-impulse contact, directly engages struck cop, and clears on separation");
}

void struck_unit_replaces_a_distant_responder_at_budget() {
    Road road;
    auto struck = seed_car(road, 0.0f);
    struck.police_pursuit = false;
    Crowd crowd = make_crowd(road, struck);
    auto& cars = const_cast<std::vector<VehicleAgent>&>(crowd.vehicles());
    for (uint32_t slot = 18; slot <= 20; ++slot) {
        auto responder = struck;
        responder.slot = slot;
        responder.police_pursuit = true;
        responder.pos.x += float(slot - 17) * 30.0f;
        cars.push_back(responder);
    }
    crowd.set_police_context(1, glm::vec2{struck.pos.x, struck.pos.z}, {});
    REQUIRE(crowd.police_pursuit_count() == 3u);
    REQUIRE(crowd.report_police_vehicle_hit({struck.lane_key, struck.slot}));
    REQUIRE(crowd.police_pursuit_count() == 3u);
    REQUIRE(cars.front().police_pursuit);
    REQUIRE(!cars.back().police_pursuit);
    REQUIRE(crowd.report_police_vehicle_hit({struck.lane_key, struck.slot}));
    REQUIRE(crowd.police_pursuit_count() == 3u);
    REQUIRE(!crowd.report_police_vehicle_hit({123, 456}));
    apricot_test::pass("struck officer takes a response slot from the farthest cruiser without exceeding budget");
}

void armed_suspect_gets_standoff_and_real_cadenced_shots() {
    Road road;
    auto car = seed_car(road, 0.0f);
    car.officer.phase = PoliceOfficerPhase::Pursuing;
    car.officer.pos = car.officer.previous_pos = car.pos;
    Crowd crowd = make_crowd(road, car);
    const glm::vec2 target{car.pos.x + 20.0f, car.pos.z};
    OnFootTrafficHazard foot;
    foot.position = target;
    foot.height_m = car.pos.y;
    const VisiblePoliceIdentity id{car.lane_key, car.slot};
    int shots = 0;
    int first_step = -1;
    for (int64_t step = 0; step < 230; ++step) {
        const bool visible = step >= 30;
        crowd.set_police_context(2, target, visible
            ? std::vector<VisiblePoliceIdentity>{id}
            : std::vector<VisiblePoliceIdentity>{});
        crowd.set_police_officer_context(true, true, {0.0f, 0.0f});
        crowd.rebuild_buckets();
        crowd.step_vehicles(step, nullptr, &foot);
        if (!visible) REQUIRE(crowd.police_shots().empty());
        for (const auto& shot : crowd.police_shots()) {
            if (first_step < 0) first_step = static_cast<int>(step);
            REQUIRE(shot.lane_key == id.lane_key && shot.slot == id.slot);
            ++shots;
        }
        REQUIRE(crowd.vehicles().front().officer.armed);
    }
    REQUIRE(first_step >= 30 + static_cast<int>(kPoliceShotReactionSteps));
    REQUIRE(shots == 2);
    const auto held_position = crowd.vehicles().front().officer.pos;
    REQUIRE(glm::distance(glm::vec2{held_position.x, held_position.z}, target) >=
            kPoliceArmedStandOffM - 0.1f);

    for (int64_t step = 230; step < 260; ++step) {
        crowd.set_police_context(2, target, {id});
        crowd.set_police_officer_context(true, false, {0.0f, 0.0f});
        crowd.rebuild_buckets();
        crowd.step_vehicles(step, nullptr, &foot);
        REQUIRE(crowd.police_shots().empty());
        REQUIRE(!crowd.vehicles().front().officer.armed);
    }
    apricot_test::pass("armed suspect makes officers hold range and fire only with LOS; holstering stops fire");
}

// The bug this pins: an officer is not a pedestrian agent, so a pistol shot
// resolved against the crowd's people missed him entirely and the player could
// empty a magazine into a cop standing a metre away with no effect at all.
void a_shot_officer_goes_down_and_stops_responding() {
    Road road;
    auto car = seed_car(road, 0.0f);
    car.officer.phase = PoliceOfficerPhase::Pursuing;
    car.officer.pos = car.officer.previous_pos = car.pos;
    Crowd crowd = make_crowd(road, car);
    const glm::vec2 target{car.pos.x + 20.0f, car.pos.z};
    OnFootTrafficHazard foot;
    foot.position = target;
    foot.height_m = car.pos.y;
    const VisiblePoliceIdentity id{car.lane_key, car.slot};
    const auto drive = [&](int64_t step) {
        crowd.set_police_context(2, target, {id});
        crowd.set_police_officer_context(true, true, {0.0f, 0.0f});
        crowd.rebuild_buckets();
        crowd.step_vehicles(step, nullptr, &foot);
    };
    // Let the real officer arm and take up his standoff first, so the shot
    // below is fired at a live responder rather than a hand-placed pose.
    for (int64_t step = 0; step < 120; ++step) drive(step);
    REQUIRE(crowd.vehicles().front().officer.armed);
    REQUIRE(crowd.player_in_police_view());

    const auto aim = [&]() {
        const glm::vec3 muzzle{target.x, foot.height_m + 1.35f, target.y};
        const glm::vec3 chest =
            crowd.vehicles().front().officer.pos + glm::vec3{0.0f, 1.15f, 0.0f};
        const glm::vec3 travel = chest - muzzle;
        const float distance = glm::length(travel);
        REQUIRE(distance > 1.0f);
        return std::pair<glm::vec3, glm::vec3>{muzzle, travel / distance};
    };

    // Two rounds wound; the officer keeps working. This is the half of the
    // contract that would pass just as happily if the shot did nothing, so the
    // health readings are checked as well as the hit flags.
    int64_t step = 120;
    for (int round = 0; round < 2; ++round, ++step) {
        const auto [muzzle, direction] = aim();
        const PedShotHit hit = crowd.shoot_ped(muzzle, direction, 60.0f, step);
        REQUIRE(hit.hit && hit.officer && !hit.officer_downed);
        REQUIRE(hit.lane_key == id.lane_key && hit.slot == id.slot);
        drive(step);
        const auto& officer = crowd.vehicles().front().officer;
        REQUIRE(!police_officer_downed(officer));
        REQUIRE(officer.health < kPoliceOfficerHealth && officer.health > 0.0f);
    }

    const auto [muzzle, direction] = aim();
    const PedShotHit killing = crowd.shoot_ped(muzzle, direction, 60.0f, step);
    REQUIRE(killing.hit && killing.officer && killing.officer_downed);
    // The general kill flag and the officer-specific one report the same edge.
    REQUIRE(killing.killed);
    drive(step++);
    {
        const auto& officer = crowd.vehicles().front().officer;
        REQUIRE(police_officer_downed(officer));
        REQUIRE(officer.dead);
        REQUIRE(officer.health == 0.0f);
        REQUIRE(!officer.armed);
        REQUIRE(officer.impact_from_bullet);
    }

    // DEAD MEANS DEAD, and this loop is the whole reason the officer carries a
    // health pool rather than a knockdown timer. It used to run for twelve
    // seconds and then assert that the same officer stood back up at full
    // health, which is what the old behaviour did: the player could not do
    // anything to an officer except delay him. Run it well past that window —
    // no shots, no witnessing, no further hits, and a body that stays where it
    // fell instead of resuming the walk it was on.
    const glm::vec3 fell = crowd.vehicles().front().officer.pos;
    const int64_t downed_first = step;
    for (; step < downed_first + 2400; ++step) {
        drive(step);
        const auto& officer = crowd.vehicles().front().officer;
        REQUIRE(officer.dead);
        REQUIRE(officer.health == 0.0f);
        REQUIRE(crowd.police_shots().empty());
        REQUIRE(!officer.armed);
        REQUIRE(glm::distance(officer.pos, fell) < 1e-4f);
        REQUIRE(!crowd.player_in_police_view());
        REQUIRE(!crowd.raycast_ped(muzzle, direction, 60.0f).hit);
    }
    // Still the same unit, still holding its identity, and still not a target:
    // a second round into a body must not re-charge the player for the kill.
    REQUIRE(crowd.vehicles().front().lane_key == id.lane_key);
    const PedShotHit again = crowd.shoot_ped(muzzle, direction, 60.0f, step);
    REQUIRE(!again.hit && !again.killed && !again.officer_downed);
    apricot_test::pass("a real officer takes pistol rounds, dies, and does not get back up");
}

}  // namespace

int main() {
    phase_machine_finishes_transitions_before_driving();
    real_crowd_exits_walks_around_cruiser_and_returns();
    streamed_officer_lifecycle_is_scan_order_independent();
    world_geometry_blocks_exits_and_walks();
    police_contacts_are_pre_impulse_and_do_not_delete_officers();
    struck_unit_replaces_a_distant_responder_at_budget();
    armed_suspect_gets_standoff_and_real_cadenced_shots();
    a_shot_officer_goes_down_and_stops_responding();
    return 0;
}
