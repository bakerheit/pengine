// Civilian overtaking and lane changes (PENG-47), headless.
//
// The police lateral moves have a suite; this is the same rig driven by
// ordinary traffic. Every case runs the production Crowd on a synthetic road,
// with the obstruction staged the way the police bypass test stages it (a
// dead engine), and asserts zero AI collisions on every step.

#include <array>

#include "maneuver_fixture.h"

using namespace apricot;
using apricot_test::pass;
using maneuver_fixture::Fixture;

namespace {

glm::vec2 xz(glm::vec3 p) { return {p.x, p.z}; }

VehicleAgent wreck(Fixture& f, float station, uint32_t slot, LaneRef ref = kInvalidLane) {
    VehicleAgent v = f.car(station, slot, false, ref);
    v.speed_mps = v.cruise_mps = 0;
    v.mechanical.engine_failed = true;
    return v;
}

LaneRef outer_forward_lane(const Fixture& f);

// A Street: one lane each way, so the only way past is the oncoming lane.
void street_overtake_with_clear_oncoming() {
    Fixture f(RoadClass::Street);
    f.cars() = {f.car(200, 1), wreck(f, 221, 2)};
    const Lane& lane = f.lanes.lane(f.lane);
    bool planned = false, completed = false;
    float widest = 0, biggest_step = 0, standoff = 0;
    glm::vec3 previous = f.cars()[0].pos;
    for (int64_t step = 0; step < 4800; ++step) {
        f.tick(step, {400, 2}, 0);
        const auto& v = f.cars()[0];
        biggest_step = std::max(biggest_step, glm::distance(previous, v.pos));
        previous = v.pos;
        REQUIRE(f.crowd.stats().ai_collisions == 0);
        if (!planned && v.speed_mps < 0.05f)
            standoff = std::max(standoff, 221.0f - v.dist_along_m);
        const auto here = f.lanes.project_onto(f.lane, xz(v.pos));
        if (here.valid() && v.maneuver.active())
            widest = std::max(widest, std::fabs(here.lateral_m));
        if (v.maneuver.kind == TrafficManeuverKind::CivilianOvertake) planned = true;
        if (planned && !v.maneuver.active() && v.dist_along_m > 240) { completed = true; break; }
    }
    std::printf("      standoff %.1fm, widest %.2fm, largest step %.3fm\n",
        double(standoff), double(widest), double(biggest_step));
    REQUIRE_MSG(planned, "no civilian overtake was ever planned", "overtake");
    REQUIRE_MSG(completed, "the overtake never completed past the wreck", "overtake");
    // Stopped with room to pull out, not on the wreck's bumper.
    REQUIRE(standoff > 8.0f);
    // Left the lane, stayed on the carriageway, and did not teleport.
    REQUIRE(widest > 2.0f);
    REQUIRE(widest < lane.width_m * 0.5f);
    REQUIRE(biggest_step < 0.25f);
    REQUIRE(f.crowd.stats().civilian_overtakes == 1);
    REQUIRE(f.cars()[0].lane == f.lane);  // an overtake ends on its own lane
    pass("a civilian waits, then borrows the oncoming lane around a wreck and returns");
}

void shaken_driver_waits_longer_before_passing() {
    for (RoadClass cls : {RoadClass::Street, RoadClass::Arterial}) {
        Fixture ordinary(cls), shaken(cls);
        LaneRef ordinary_lane = ordinary.lane;
        LaneRef shaken_lane = shaken.lane;
        if (cls == RoadClass::Arterial) {
            ordinary_lane = outer_forward_lane(ordinary);
            shaken_lane = outer_forward_lane(shaken);
        }
        auto first = ordinary.car(200, 1, false, ordinary_lane);
        auto second = shaken.car(200, 1, false, shaken_lane);
        first.profile = second.profile = make_driver_profile(DriverProfileKind::Impatient);
        second.impact_caution_s = 12.0f;
        ordinary.cars() = {first, wreck(ordinary, 221, 2, ordinary_lane)};
        shaken.cars() = {second, wreck(shaken, 221, 2, shaken_lane)};
        int64_t ordinary_plan = -1, shaken_plan = -1;
        for (int64_t step = 0; step < 2400; ++step) {
            ordinary.tick(step, {400, 2}, 0);
            shaken.tick(step, {400, 2}, 0);
            const auto kind = cls == RoadClass::Street
                ? TrafficManeuverKind::CivilianOvertake : TrafficManeuverKind::LaneChange;
            if (ordinary_plan < 0 && ordinary.cars()[0].maneuver.kind == kind)
                ordinary_plan = step;
            if (shaken_plan < 0 && shaken.cars()[0].maneuver.kind == kind)
                shaken_plan = step;
            REQUIRE(ordinary.crowd.stats().ai_collisions == 0);
            REQUIRE(shaken.crowd.stats().ai_collisions == 0);
            if (ordinary_plan >= 0 && shaken_plan >= 0) break;
        }
        REQUIRE(ordinary_plan >= 0 && shaken_plan > ordinary_plan);
    }
    pass("impact caution delays both oncoming passes and lane changes without disabling them");
}

void ordinary_profiles_choose_lane_changes_at_different_times() {
    const std::array<DriverProfileKind, 3> kinds{
        DriverProfileKind::Impatient, DriverProfileKind::Normal,
        DriverProfileKind::Cautious};
    std::array<int64_t, 3> first_plan{};
    for (std::size_t n = 0; n < kinds.size(); ++n) {
        Fixture f(RoadClass::Arterial);
        const LaneRef outer = outer_forward_lane(f);
        auto driver = f.car(200, 1, false, outer);
        driver.profile = make_driver_profile(kinds[n]);
        f.cars() = {driver, wreck(f, 221, 2, outer)};
        first_plan[n] = -1;
        for (int64_t step = 0; step < 1800; ++step) {
            f.tick(step, {400, 2}, 0);
            REQUIRE(f.crowd.stats().ai_collisions == 0);
            if (f.cars()[0].maneuver.kind == TrafficManeuverKind::LaneChange) {
                first_plan[n] = step;
                break;
            }
        }
        REQUIRE(first_plan[n] >= 0);
    }
    REQUIRE(first_plan[0] < first_plan[1]);
    REQUIRE(first_plan[1] < first_plan[2]);
    pass("ordinary impatient, normal, and cautious drivers change lanes at different times");
}

// The TTC gate: an oncoming car that is far away by distance but close by
// time refuses the pass; once it is gone the same driver passes.
void street_overtake_refuses_oncoming_at_50m_closing_24() {
    Fixture f(RoadClass::Street);
    const LaneRef opposing = f.lanes.opposing(f.lane);
    REQUIRE(f.lanes.valid(opposing));
    auto oncoming = f.car(0, 3, false, opposing);
    oncoming.speed_mps = oncoming.cruise_mps = 19.0f;
    f.cars() = {f.car(200, 1), wreck(f, 221, 2), oncoming};
    // Pin the oncoming car 50 m ahead of the civilian every step: a wall of
    // traffic that never actually arrives. front_gap 45 m clears the 28 m run,
    // so the only thing that can refuse is the 4 s time-to-collision.
    auto pin = [&]() {
        auto& c = f.cars();
        const auto ahead = f.lanes.pose(f.lane, c[0].dist_along_m + 50.0f).position;
        const auto on = f.lanes.project_onto(opposing, xz(ahead));
        REQUIRE(on.valid());
        c[2].dist_along_m = c[2].last_dist_m = on.dist_along_m;
        const auto p = f.lanes.pose(opposing, on.dist_along_m);
        c[2].pos = p.position; c[2].fwd = p.tangent;
        c[2].speed_mps = 19.0f;
    };
    for (int64_t step = 0; step < 3600; ++step) {
        pin();
        f.tick(step, {400, 2}, 0);
        REQUIRE(f.cars()[0].maneuver.kind != TrafficManeuverKind::CivilianOvertake);
    }
    REQUIRE(f.cars()[0].speed_mps < 0.1f);  // it waited, it did not give up
    f.cars().pop_back();
    bool planned = false, completed = false;
    for (int64_t step = 3600; step < 7200; ++step) {
        f.tick(step, {400, 2}, 0);
        REQUIRE(f.crowd.stats().ai_collisions == 0);
        const auto& v = f.cars()[0];
        if (v.maneuver.kind == TrafficManeuverKind::CivilianOvertake) planned = true;
        if (planned && !v.maneuver.active() && v.dist_along_m > 240) { completed = true; break; }
    }
    REQUIRE_MSG(completed, "the pass never happened once the oncoming lane cleared", "ttc");
    pass("oncoming traffic four seconds away refuses the pass; a clear lane allows it");
}

// Two Arterials crossing: a signalled junction. A car stopped at the red, and
// a car queued behind it, are a queue — nothing passes them, and the standoff
// does not open behind them.
void no_overtake_behind_ai_car_at_red() {
    RoadGraph roads; LaneGraph lanes;
    RoadSpine a, b;
    a.id = 1; a.cls = RoadClass::Arterial; a.points = {{0, 0}, {600, 0}};
    b.id = 2; b.cls = RoadClass::Arterial; b.points = {{300, -300}, {300, 300}};
    GroundSampler ground;
    roads.build({a, b}, {}, ground); lanes.build(roads, ground);
    const LaneRef approach = lanes.nearest_lane_along({150, 3}, {1, 0}).lane;
    REQUIRE(lanes.valid(approach));
    const Lane& lane = lanes.lane(approach);
    REQUIRE_MSG(lanes.approach_control(approach) == JunctionControl::Signal,
                "an arterial crossing should be signalled", "fixture");
    Crowd crowd;
    CrowdTuning tuning; tuning.max_peds = 0; tuning.police.patrol_fraction = 0;
    crowd.build(lanes, 905, {}, tuning);
    const float gate = lane.length_m -
        traffic_junction_clearance(lanes, lane.junction_to, tuning) - 1.0f;
    auto car = [&](float station, uint32_t slot, float speed) {
        VehicleAgent v;
        v.lane = approach; v.lane_key = lane.key; v.slot = slot;
        v.dist_along_m = v.last_dist_m = station;
        v.speed_mps = v.cruise_mps = speed;
        v.mode = AgentMode::Integrating;
        const auto p = lanes.pose(v.lane, station); v.pos = p.position; v.fwd = p.tangent;
        return v;
    };
    auto& cars = const_cast<std::vector<VehicleAgent>&>(crowd.vehicles());
    cars = {car(gate - 45.0f, 1, 8.0f), car(gate - 7.0f, 2, 0.0f)};
    float max_wait = 0;
    for (int64_t step = 0; step < 2 * tuning.signal_period_steps; ++step) {
        crowd.set_police_context(0, {600, 3});
        crowd.rebuild_buckets(); crowd.step_vehicles(step);
        for (const auto& v : crowd.vehicles()) {
            REQUIRE(v.maneuver.kind != TrafficManeuverKind::CivilianOvertake);
            REQUIRE(v.maneuver.kind != TrafficManeuverKind::LaneChange);
            max_wait = std::max(max_wait, v.obstruction_wait_s);
        }
        REQUIRE(crowd.stats().ai_collisions == 0);
    }
    std::printf("      longest obstruction wait behind the queue %.2fs\n", double(max_wait));
    REQUIRE(max_wait < 0.5f);
    REQUIRE(crowd.stats().jam_despawns == 0 && crowd.vehicles().size() == 2);
    REQUIRE(crowd.stats().civilian_overtakes == 0 && crowd.stats().lane_changes == 0);
    pass("a queue at a signal is never overtaken and never counts as an obstruction");
}

LaneRef outer_forward_lane(const Fixture& f) {
    LaneRef outer = f.lane;
    for (auto ref : f.lanes.lanes_of_edge(f.lanes.lane(f.lane).edge))
        if (f.lanes.lane(ref).forward == f.lanes.lane(f.lane).forward &&
            f.lanes.lane(ref).index > f.lanes.lane(outer).index) outer = ref;
    return outer;
}

// An Arterial: two lanes each way. Behind a wreck in the outer lane the car
// changes into the inner one and is re-homed there.
void arterial_lane_change_behind_stalled_car() {
    Fixture f;
    const LaneRef outer = outer_forward_lane(f);
    const LaneRef inner = f.lanes.neighbour(outer, -1);
    REQUIRE(f.lanes.valid(inner) && inner != outer);
    f.cars() = {f.car(200, 1, false, outer), wreck(f, 221, 2, outer)};
    bool planned = false, rehomed = false, passed = false;
    float biggest_step = 0;
    glm::vec3 previous = f.cars()[0].pos;
    for (int64_t step = 0; step < 3600; ++step) {
        f.tick(step, {400, 2}, 0);
        const auto& v = f.cars()[0];
        biggest_step = std::max(biggest_step, glm::distance(previous, v.pos));
        previous = v.pos;
        REQUIRE(f.crowd.stats().ai_collisions == 0);
        if (v.maneuver.kind == TrafficManeuverKind::LaneChange && v.maneuver.active()) {
            planned = true;
            REQUIRE(v.lane == outer);  // still homed on the origin lane mid-arc
            REQUIRE(v.maneuver.destination == inner);
        }
        if (planned && !v.maneuver.active() && v.lane == inner) rehomed = true;
        if (rehomed && v.dist_along_m > 240) { passed = true; break; }
    }
    std::printf("      lane change: planned %d, rehomed %d, passed %d, largest step %.3fm\n",
        planned, rehomed, passed, double(biggest_step));
    REQUIRE_MSG(planned, "no lane change was ever planned", "lane-change");
    REQUIRE_MSG(rehomed, "the car never re-homed onto the inner lane", "lane-change");
    REQUIRE_MSG(passed, "the car never passed the wreck", "lane-change");
    REQUIRE(biggest_step < 0.25f);
    REQUIRE(f.crowd.stats().lane_changes == 1);
    REQUIRE(f.crowd.stats().civilian_overtakes == 0);
    pass("behind a wreck on a multi-lane road the car changes lane and is re-homed");
}

// The determinism rule, on both moves: two crowds seeded in opposite vector
// order are bit-identical every step, including the digest.
void civilian_maneuvers_are_order_independent() {
    for (int scenario = 0; scenario < 2; ++scenario) {
        const RoadClass cls = scenario == 0 ? RoadClass::Street : RoadClass::Arterial;
        Fixture a(cls), b(cls);
        LaneRef ref = a.lane;
        if (scenario == 1) ref = outer_forward_lane(a);
        auto driver = a.car(200, 1, false, ref), blocker = wreck(a, 221, 2, ref);
        a.cars() = {driver, blocker}; b.cars() = {blocker, driver};
        bool moved = false;
        for (int64_t step = 0; step < 4800; ++step) {
            a.tick(step, {400, 2}, 0); b.tick(step, {400, 2}, 0);
            const auto& x = a.cars()[0]; const auto& y = b.cars()[1];
            REQUIRE(x.pos == y.pos && x.fwd == y.fwd && x.speed_mps == y.speed_mps);
            REQUIRE(x.lane == y.lane && x.maneuver.kind == y.maneuver.kind);
            // The digest folds the vector in order and this fixture is
            // deliberately out of identity order, so compare the state the
            // digest would see, field by field.
            REQUIRE(x.dist_along_m == y.dist_along_m &&
                    x.maneuver.progress_m == y.maneuver.progress_m &&
                    x.obstruction_wait_s == y.obstruction_wait_s &&
                    x.lane_change_cooldown_s == y.lane_change_cooldown_s &&
                    x.maneuver_decisions == y.maneuver_decisions);
            REQUIRE(a.crowd.stats().ai_collisions == 0 && b.crowd.stats().ai_collisions == 0);
            moved |= x.maneuver.active();
            if (moved && !x.maneuver.active() && x.dist_along_m > 240) break;
        }
        REQUIRE(moved);
    }
    pass("overtake and lane change are independent of update order");
}

// Free-flowing traffic plans nothing: a leader at cruise is not an
// obstruction, so no wait accrues and the planner is never entered.
void free_flowing_traffic_plans_nothing() {
    Fixture f(RoadClass::Street);
    auto leader = f.car(215, 2);
    f.cars() = {f.car(200, 1), leader};
    for (int64_t step = 0; step < 1200; ++step) {
        f.tick(step, {400, 2}, 0);
        for (const auto& v : f.cars()) {
            REQUIRE(v.obstruction_wait_s == 0.0f);
            REQUIRE(v.maneuver.kind == TrafficManeuverKind::None);
        }
    }
    pass("a leader at cruise is not an obstruction");
}

// AMBIENT PARKED CARS AS OBSTACLES (PENG-49). One car parked reaching into
// the lane: the driver stops short of it or overtakes it, and never overlaps
// it. The same car sitting kerb-clear is passed at cruise with no maneuver.
bool boxes_overlap(glm::vec3 apos, glm::vec3 afwd, float aw, float al,
                   glm::vec3 bpos, glm::vec3 bfwd, float bw, float bl) {
    const glm::vec2 af = glm::normalize(xz(afwd)), bf = glm::normalize(xz(bfwd));
    const glm::vec2 ar{-af.y, af.x}, br{-bf.y, bf.x};
    const glm::vec2 delta = xz(bpos - apos);
    for (glm::vec2 axis : {af, ar, bf, br}) {
        const float radius = al * std::fabs(glm::dot(af, axis)) + aw * std::fabs(glm::dot(ar, axis)) +
                             bl * std::fabs(glm::dot(bf, axis)) + bw * std::fabs(glm::dot(br, axis));
        if (std::fabs(glm::dot(delta, axis)) >= radius) return false;
    }
    return true;
}

void badly_parked_car_stops_or_is_overtaken() {
    for (bool intruding : {true, false}) {
        Fixture f(RoadClass::Street);
        const Lane& lane = f.lanes.lane(f.lane);
        REQUIRE(lane.parked_density > 0.0f);
        auto& parked = const_cast<std::vector<AmbientParkedCar>&>(f.crowd.ambient_parked());
        AmbientParkedCar car;
        car.lane_key = lane.key; car.slot = 0;
        const auto pose = f.lanes.pose(f.lane, 230.0f, intruding ? 1.6f : 2.3f);
        car.pos = pose.position; car.fwd = pose.tangent;
        car.kind = TrafficVehicleKind::Sedan;
        parked = {car};
        const auto pfp = traffic_vehicle_footprint(car.kind);
        f.cars() = {f.car(200, 1)};
        bool overlapped = false, stopped_short = false, overtook = false, dipped = false;
        for (int64_t step = 0; step < 4800; ++step) {
            f.tick(step, {400, 2}, 0);
            const auto& v = f.cars()[0];
            const auto vfp = traffic_vehicle_footprint(traffic_vehicle_kind(v));
            overlapped |= boxes_overlap(v.pos, v.fwd, vfp.half_width_m, vfp.half_length_m,
                                        car.pos, car.fwd, pfp.half_width_m, pfp.half_length_m);
            dipped |= v.speed_mps < v.cruise_mps - 0.1f;
            if (v.speed_mps < 0.1f && v.dist_along_m < 225.0f) stopped_short = true;
            if (v.maneuver.kind == TrafficManeuverKind::CivilianOvertake) overtook = true;
            if (v.dist_along_m > 250.0f) break;
        }
        const auto& v = f.cars()[0];
        REQUIRE_MSG(!overlapped, "the driver drove through a parked car", "parked");
        if (intruding) {
            REQUIRE_MSG(stopped_short || overtook,
                        "an intruding parked car was neither stopped for nor passed", "parked");
            REQUIRE(v.dist_along_m > 250.0f || v.speed_mps < 0.1f);
        } else {
            REQUIRE_MSG(!dipped, "a kerb-clear parked car slowed the traffic", "parked");
            REQUIRE(!overtook && v.dist_along_m > 250.0f);
        }
    }
    pass("a parked car in the lane is stopped for or overtaken; a kerb-clear one is passed at cruise");
}

// JAM RECOVERY AND EVAPORATION (PENG-50). The pass is refused for good (a
// wreck ahead AND a wreck in the oncoming lane), so the ladder runs: nothing
// on it is eligible, the car gives up, and — far from the player — it is
// retired after the despawn ceiling. Near the player it never is; behind a
// signal queue it never is; and the departure it was never comes back.
Fixture wedged_street() {
    Fixture f(RoadClass::Street);
    const LaneRef opposing = f.lanes.opposing(f.lane);
    REQUIRE(f.lanes.valid(opposing));
    // A dead car across our lane, and a dead car facing us 5 m beyond it in
    // the oncoming lane: no 28 m run is ever clear, no shoulder is ever wide.
    auto blocker = wreck(f, 221, 2);
    auto onto = f.lanes.project_onto(opposing, xz(f.lanes.pose(f.lane, 226).position));
    REQUIRE(onto.valid());
    auto oncoming = wreck(f, onto.dist_along_m, 3, opposing);
    f.cars() = {f.car(200, 1), blocker, oncoming};
    return f;
}

void wedged_car_beyond_60m_retires_after_the_ladder() {
    Fixture f = wedged_street();
    const uint64_t key = f.cars()[0].lane_key;
    bool gave_up = false;
    int64_t gone_at = -1;
    for (int64_t step = 0; step < 120 * 60; ++step) {
        f.tick(step, {400, 2}, 0);   // no player in the step: "far" by the sentinel
        REQUIRE(f.crowd.stats().ai_collisions == 0);
        bool present = false;
        for (const auto& v : f.cars()) {
            if (v.lane_key != key || v.slot != 1) continue;
            present = true;
            REQUIRE(v.maneuver.kind != TrafficManeuverKind::CivilianOvertake);
            gave_up |= v.recovery_action == RecoveryAction::GiveUp;
        }
        if (!present) { gone_at = step; break; }
    }
    std::printf("      wedged car retired at step %lld\n", static_cast<long long>(gone_at));
    REQUIRE_MSG(gave_up, "the ladder never gave up on a car it could not free", "ladder");
    REQUIRE_MSG(gone_at > 0, "the wedged car never evaporated", "ladder");
    REQUIRE(gone_at > 120 * 30);  // never before the despawn ceiling
    REQUIRE(f.crowd.stats().jam_despawns == 1);
    REQUIRE(f.crowd.retired_identity_count() == 1);
    REQUIRE(f.cars().size() == 2);
    pass("a car the ladder cannot free evaporates after the ceiling, far from the player");
}

void wedged_car_near_the_player_never_retires() {
    Fixture f = wedged_street();
    VehicleState player;
    player.position = f.lanes.pose(f.lane, 170).position;   // 30 m behind the wedge
    player.velocity = glm::vec3{0.0f};
    for (int64_t step = 0; step < 120 * 100; ++step) {
        f.tick_with_player(step, player, 0);
        REQUIRE(f.cars().size() == 3);
    }
    REQUIRE(f.crowd.stats().jam_despawns == 0);
    pass("a wedged car within 60 m of the player is never retired");
}

void retired_departure_never_returns() {
    Fixture f = wedged_street();
    const auto car = f.cars()[0];
    for (int64_t step = 0; step < 120 * 60 && f.cars().size() == 3; ++step) f.tick(step, {400, 2}, 0);
    REQUIRE(f.cars().size() == 2);
    // Let the ambient schedule run: this departure must stay gone.
    for (int64_t step = 120 * 60; step < 120 * 90; ++step) {
        if (step % 8 == 0) f.crowd.refresh(step, {200, 2});
        f.tick(step, {200, 2}, 0);
        for (const auto& v : f.cars())
            REQUIRE(!same_departure(v.lane_key, v.slot, v.generation,
                                    car.lane_key, car.slot, car.generation));
    }
    // Ambient cars retire at the ring too once refresh runs; ours is among them.
    REQUIRE(f.crowd.retired_identity_count() >= 1);
    pass("a retired departure never comes back through the schedule");
}

// Partial blocker: an ambient parked car reaching 0.5 m into the lane, with
// the oncoming lane blocked so the full pass is refused. The ladder's Nudge
// edges past it on our own half of the road.
void nudge_edges_past_a_partial_blocker() {
    Fixture f(RoadClass::Street);
    const Lane& lane = f.lanes.lane(f.lane);
    const LaneRef opposing = f.lanes.opposing(f.lane);
    auto& parked = const_cast<std::vector<AmbientParkedCar>&>(f.crowd.ambient_parked());
    AmbientParkedCar car;
    car.lane_key = lane.key; car.slot = 0;
    const auto pose = f.lanes.pose(f.lane, 230.0f, 1.5f);
    car.pos = pose.position; car.fwd = pose.tangent; car.kind = TrafficVehicleKind::Sedan;
    parked = {car};
    auto onto = f.lanes.project_onto(opposing, xz(f.lanes.pose(f.lane, 235).position));
    REQUIRE(onto.valid());
    f.cars() = {f.car(200, 1), wreck(f, onto.dist_along_m, 3, opposing)};
    const auto pfp = traffic_vehicle_footprint(car.kind);
    bool nudged = false, passed = false, overlapped = false;
    for (int64_t step = 0; step < 120 * 40; ++step) {
        f.tick(step, {400, 2}, 0);
        const auto& v = f.cars()[0];
        REQUIRE(f.crowd.stats().ai_collisions == 0);
        const auto vfp = traffic_vehicle_footprint(traffic_vehicle_kind(v));
        overlapped |= boxes_overlap(v.pos, v.fwd, vfp.half_width_m, vfp.half_length_m,
                                    car.pos, car.fwd, pfp.half_width_m, pfp.half_length_m);
        REQUIRE(v.maneuver.kind != TrafficManeuverKind::CivilianOvertake);
        if (v.maneuver.kind == TrafficManeuverKind::Nudge) nudged = true;
        if (nudged && !v.maneuver.active() && v.dist_along_m > 240.0f) { passed = true; break; }
    }
    REQUIRE_MSG(nudged, "no nudge was ever planned", "nudge");
    REQUIRE_MSG(passed, "the nudge never got past the blocker", "nudge");
    REQUIRE(!overlapped);
    REQUIRE(f.crowd.stats().nudges == 1);
    pass("a partial blocker is edged past on our own half of the road when the pass is refused");
}

// HONK AT THE PLAYER (PENG-51), the sim side: the player's car stopped four
// metres ahead of a rolling driver. The debounced one-shot fires exactly
// once within three seconds, not again inside the interval, and two
// identical crowds fire on the same step.
void driver_honks_once_at_a_stopped_player() {
    Fixture a(RoadClass::Street), b(RoadClass::Street);
    a.cars() = {a.car(200, 1)}; b.cars() = {b.car(200, 1)};
    VehicleState player;
    player.position = a.lanes.pose(a.lane, 212.0f).position;
    player.velocity = glm::vec3{0.0f};
    int64_t first = -1, second = -1;
    for (int64_t step = 0; step < 120 * 8; ++step) {
        a.tick_with_player(step, player, 0); b.tick_with_player(step, player, 0);
        REQUIRE(a.cars()[0].honk_player_fire == b.cars()[0].honk_player_fire);
        if (a.cars()[0].honk_player_fire) {
            if (first < 0) first = step; else if (second < 0) second = step;
        }
    }
    std::printf("      honk at step %lld, next at %lld\n",
        static_cast<long long>(first), static_cast<long long>(second));
    REQUIRE_MSG(first >= 0 && first < 360, "the driver never honked at the stopped player", "honk");
    REQUIRE_MSG(second < 0 || second - first >= 120 * 4,
                "the driver honked again inside the interval", "honk");
    pass("a driver blocked by the player's car honks once, on the same step in both crowds");
}

}  // namespace

int main() {
    std::puts("civilian_maneuver_tests");
    driver_honks_once_at_a_stopped_player();
    badly_parked_car_stops_or_is_overtaken();
    nudge_edges_past_a_partial_blocker();
    wedged_car_beyond_60m_retires_after_the_ladder();
    wedged_car_near_the_player_never_retires();
    retired_departure_never_returns();
    free_flowing_traffic_plans_nothing();
    street_overtake_with_clear_oncoming();
    shaken_driver_waits_longer_before_passing();
    ordinary_profiles_choose_lane_changes_at_different_times();
    street_overtake_refuses_oncoming_at_50m_closing_24();
    no_overtake_behind_ai_car_at_red();
    arterial_lane_change_behind_stalled_car();
    civilian_maneuvers_are_order_independent();
    return apricot_test::done("civilian_maneuver_tests");
}
