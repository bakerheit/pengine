// DOES A PURSUIT ACTUALLY CHASE YOU?
//
// Every other police suite here asks whether a rule fires: does a patrol
// witness a crime, does a cruiser convert, does an officer get out. All of
// them passed while a chase on the authored city was, in the player's words,
// nobody chasing him at all — because "chasing" is not a rule, it is an
// OUTCOME over a whole drive, and nothing measured the outcome.
//
// So this one drives a fleeing player down the real authored streets with a
// real Crowd behind him and measures the gap. The numbers it prints are the
// ones that matter; the assertions are floors under them, deliberately loose
// enough to survive tuning and tight enough to catch the four defects that
// made a pursuit fall apart:
//
//   * pursuers were retired as ordinary traffic at ~320 m, so no cruiser ever
//     stayed on the player — the car behind you was never the same car twice;
//   * a pursuer with no usable route picked its turns from choose_next(), the
//     WEIGHTED RANDOM draw ambient traffic uses;
//   * right-of-way at junctions could hold a stopped pursuer indefinitely
//     (a stopped car has a long ETA, a long ETA loses the compare, and losing
//     keeps it stopped);
//   * the overtake that exists to get a cruiser OUT of traffic ran at 5 m/s,
//     and a suspect at 40 mph gained 120 m inside one.

#include <algorithm>
#include <cstdio>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "city/map.h"
#include "city/road_types.h"
#include "city/spines.h"
#include "core/fixed_step.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "road/lane_graph.h"
#include "road/road_graph.h"
#include "terrain/heightmap.h"
#include "test_assert.h"
#include "traffic/crowd.h"

using namespace apricot;
namespace {

struct Chase {
    long samples = 0;
    long within60 = 0;
    double sum_nearest = 0.0;
    float worst = 0.0f;
    std::size_t deletions = 0;
    std::size_t ai_collisions = 0;
    long unit_steps = 0, stuck_steps = 0, queued_steps = 0, bypassing = 0;
    long off_road = 0;
    long reversing = 0;
    long facing_away = 0;
    float worst_yaw_rate = 0.0f;        // deg/s, any pursuer, any step
    float worst_step_dy = 0.0f;         // biggest vertical jump in one step
    float worst_chase_step_dy = 0.0f;   // ... for free-driving cruisers
    float worst_float_m = 0.0f;         // body sitting above the ground
    float worst_chase_float_m = 0.0f;   // ... while free-driving
    float worst_lane_float_m = 0.0f;    // ... while lane-following
    float worst_sink_m = 0.0f;          // body sunk into it
    float worst_chase_yaw_rate = 0.0f;  // deg/s, free-driving cruisers only
    float closest_offroad = 1e9f;  // best approach once the player is off-road
    // Scenario 5 — search mode (PENG-44). Counted only while the player is
    // stopped and no unit has had a world ray to him for at least a second.
    long search_steps = 0;        // steps in that state
    long search_not_flagged = 0;  // ... where the crowd did not report searching
    long centre_drift = 0;        // ... where the centre moved off the last-seen point
    long routed_off_post = 0;     // pursuer route targets that are neither the centre nor a post
    float nearest_to_centre_m = 1e9f;
    float live_to_centre_m = 0.0f;  // how far he got from where he was last seen
    bool los_ever_broke = false;
    bool reacquired = false;
    double sum_speed = 0.0;
    float mean() const { return float(sum_nearest / double(samples ? samples : 1)); }
};

// A suspect who is actually getting away: straightest legal continuation, no
// U-turns, no freeway. A player who mills about in one block makes any pursuit
// look competent, because the cops never have to cover ground.
// Three different starts through the authored city. One 45 s run on one street
// is a noisy estimator, and tuning police driving against a single sample is
// how you ship a number that only helps one block.
const glm::vec2 kStarts[3] = {
    {900.0f, 200.0f}, {700.0f, 60.0f}, {950.0f, 40.0f}};

LaneRef flee_next(const LaneGraph& lanes, LaneRef lane) {
    const glm::vec3 heading = lanes.pose(lane, lanes.length(lane)).tangent;
    LaneRef best = kInvalidLane;
    float best_align = -2.0f;
    for (const TurnLink& link : lanes.outgoing(lane)) {
        if (!lanes.valid(link.to)) continue;
        if (lanes.lane(link.to).cls == RoadClass::Freeway) continue;
        const glm::vec3 tangent = lanes.pose(link.to, 0.0f).tangent;
        const float align = glm::dot(
            glm::normalize(glm::vec2{heading.x, heading.z}),
            glm::normalize(glm::vec2{tangent.x, tangent.z}));
        if (align > best_align) { best_align = align; best = link.to; }
    }
    return best;
}

// The LEAST aligned legal exit: a side street, for a player who turns off.
LaneRef flee_turn_off(const LaneGraph& lanes, LaneRef lane) {
    const glm::vec3 heading = lanes.pose(lane, lanes.length(lane)).tangent;
    LaneRef best = kInvalidLane;
    float best_align = 2.0f;
    for (const TurnLink& link : lanes.outgoing(lane)) {
        if (!lanes.valid(link.to) || link.to == lanes.opposing(lane)) continue;
        if (lanes.lane(link.to).cls == RoadClass::Freeway) continue;
        const glm::vec3 tangent = lanes.pose(link.to, 0.0f).tangent;
        const float align = std::fabs(glm::dot(
            glm::normalize(glm::vec2{heading.x, heading.z}),
            glm::normalize(glm::vec2{tangent.x, tangent.z})));
        if (align < best_align) { best_align = align; best = link.to; }
    }
    return best_align < 0.5f ? best : kInvalidLane;
}

Chase run_chase(const LaneGraph& lanes, const GroundSampler& ground,
                float speed_mps, int wanted, int64_t steps, int scenario) {
    // Scenario 5 needs corners close together: the scenario-2 start is on a
    // short-block grid, the scenario-0 start is 650 m of straight road.
    const int start_index = scenario == 4 ? 0 : scenario == 5 ? 2 : scenario % 3;
    // Free-drive pursuit needs a world to drive ON — and, more importantly, a
    // world to drive INTO. Measuring it over bare terrain would let a cruiser
    // cut across city blocks that hold buildings in the real game, and would
    // flatter the whole feature. There is no authored building set available
    // to a headless suite, so this fills every patch of ground more than 13 m
    // from any lane with a solid block: a coarse but honest stand-in for the
    // built city, and enough that a cop which only knows how to aim at the
    // player gets stopped by the same walls a player would.
    TerrainCollider world{city::kMapSeed};
    int blocks = 0;
    for (float x = -400.0f; x <= 400.0f; x += 18.0f) {
        for (float z = -400.0f; z <= 400.0f; z += 18.0f) {
            const glm::vec2 here = kStarts[start_index] + glm::vec2{x, z};
            if (lanes.nearest_lane(here, 13.0f).valid()) continue;
            const float y = ground.fn ? ground.fn(ground.ctx, here.x, here.y) : 0.0f;
            world.add_static_box({{here.x - 8.0f, y - 2.0f, here.y - 8.0f},
                                  {here.x + 8.0f, y + 9.0f, here.y + 8.0f}});
            ++blocks;
        }
    }
    CrowdTuning tuning;
    // This suite measures DRIVING. The radio hold (PENG-45) would keep every
    // non-witness unit off the road for the first seconds of each scenario;
    // it has its own test in police_runtime_tests.
    tuning.police.radio_latency_s = 0.0f;
    tuning.police.default_response_s = 0.0f;
    // THE APP'S OWN AMBIENT DENSITY, not the header defaults. src/app/world.cpp
    // thins traffic to 48 m spacing and 16 slots; the defaults are 34 m and 32,
    // which is close to double and gridlocks the authored grid all by itself.
    // Measuring a chase at a density the game never runs is measuring the
    // wrong game — it made the whole city crawl at 3 m/s and every pursuit
    // look hopeless whether the police code was good or not.
    AmbientTuning ambient;
    ambient.vehicle_spacing_m = 48.0f;
    ambient.max_vehicle_slots = 16;
    ambient.ped_spacing_m = 52.0f;
    ambient.max_ped_slots = 8;
    Crowd crowd;
    crowd.build(lanes, city::kMapSeed, ambient, tuning);

    std::printf("      (scenario %d: %d block proxies)\n", scenario, blocks);
    // Scenario 4 isolates the OFF-ROAD case, so it runs on the district that
    // already works. Landing it in the jammed one conflates two problems and
    // the number tells you nothing about either.
    const glm::vec2 origin = kStarts[start_index];
    LaneRef lane = kInvalidLane;
    for (LaneRef r = 0; r < lanes.lane_count(); ++r) {
        const Lane& l = lanes.lane(r);
        if (l.cls == RoadClass::Freeway || l.length_m < 40.0f) continue;
        const glm::vec3 p = lanes.pose(r, 0.0f).position;
        if (glm::distance(glm::vec2{p.x, p.z}, origin) > 400.0f) continue;
        lane = r;
        break;
    }
    REQUIRE(lanes.valid(lane));

    Chase out;
    float station = 10.0f;
    VehicleState player;
    std::vector<std::pair<uint64_t, uint32_t>> live;
    // Scenario 5 state: after 15 s take the next two side streets (one
    // corner still leaves a clear ray down the street from the junction the
    // cruisers arrive at; two does not), drive 40 m in, stop for 15 s, then
    // double back toward the corner.
    int turns = 0;
    bool stopped = false, resumed = false;
    int64_t stop_step = -1, unseen_since = -1;
    bool ever_seen = false;
    glm::vec2 last_seen{0.0f};

    for (int64_t step = 0; step < steps; ++step) {
        const LanePose pose = lanes.pose(lane, station);
        player.position = pose.position;
        player.velocity = stopped ? glm::vec3{0.0f} : pose.tangent * speed_mps;
        // SCENARIO 4: pull off the road and stop. This is the screenshot —
        // the player sitting on the verge with a cruiser parked in its lane
        // forty metres away, going nowhere. A pursuit that only ever drives
        // the lane graph has nothing to drive to once you leave it.
        const bool off_road = scenario == 4 && step > 1200;
        if (off_road) {
            player.position = pose.position + pose.right * 12.0f;
            player.velocity = glm::vec3{0.0f};
        }
        const glm::vec2 xz{player.position.x, player.position.z};

        if (step % 30 == 0) crowd.refresh(step, xz);
        // REAL line of sight, raycast against the same world the cruisers
        // drive in — the app's own rule. Handing every unit free LOS made the
        // sight gates generous AND hid the one that matters for free driving:
        // a cruiser must not aim itself at a player it cannot see, because
        // aiming is all it does and the wall does not move.
        std::vector<VisiblePoliceIdentity> visible;
        for (const VehicleAgent& a : crowd.vehicles()) {
            if (!a.police_unit) continue;
            const glm::vec3 eye = police_officer_eye_position(a);
            const glm::vec3 to =
                glm::vec3{xz.x, player.position.y + 1.05f, xz.y} - eye;
            const float d = glm::length(to);
            if (d < 0.05f) { visible.push_back({a.lane_key, a.slot}); continue; }
            const auto hit = world.raycast(eye, to / d, d);
            if (!hit.hit || hit.distance >= d - 0.08f)
                visible.push_back({a.lane_key, a.slot});
        }
        // SCENARIO 5: from the second corner until he doubles back, every ray
        // is withdrawn. The proxy city has no interiors to hide in and its
        // streets are straight, so twenty units keep a clear ray down the
        // block; the runtime suite proves the per-unit sight mechanics, and
        // what this scenario measures is what the whole pursuit DOES once
        // sight is lost at city scale.
        if (scenario == 5 && turns >= 2 && !resumed) visible.clear();
        crowd.set_police_context(wanted, xz, visible);
        crowd.set_police_officer_context(false, false,
            {player.velocity.x, player.velocity.z}, &world);
        crowd.rebuild_buckets();
        crowd.step_vehicles(step, &player);

        if (scenario == 5) {
            if (!visible.empty()) { ever_seen = true; unseen_since = -1; }
            else if (unseen_since < 0) { unseen_since = step; }
            if (visible.empty() && ever_seen) out.los_ever_broke = true;
            // The crowd's own last-seen point: whenever it is not searching,
            // the centre IS the live position it just saw.
            if (!crowd.police_searching()) last_seen = crowd.police_search_centre();
            const bool expect_search = stopped && ever_seen && unseen_since >= 0 &&
                                       step - unseen_since >= 120;
            if (expect_search) {
                ++out.search_steps;
                if (!crowd.police_searching()) ++out.search_not_flagged;
                if (crowd.police_search_centre() != last_seen) ++out.centre_drift;
                out.live_to_centre_m = std::max(out.live_to_centre_m,
                    glm::distance(xz, crowd.police_search_centre()));
                // Every unit is routed to the centre or to one of its posts —
                // never to the live player, whom nobody can see. (A post CAN
                // coincide with where he is hiding; that is the search
                // finding him, and it counts as legitimate.)
                const glm::vec2 centre = crowd.police_search_centre();
                std::vector<glm::vec2> legal{centre};
                const auto centre_lane = lanes.nearest_lane(centre, 180.0f);
                if (centre_lane.valid()) {
                    for (uint32_t k = 0; k < 12; ++k) {
                        const LaneRef post = police_search_post(lanes, centre_lane.lane, k);
                        if (!lanes.valid(post)) break;
                        const auto p = lanes.pose(post, std::min(40.0f, lanes.length(post))).position;
                        legal.push_back({p.x, p.z});
                    }
                }
                for (const VehicleAgent& a : crowd.vehicles()) {
                    if (!a.police_unit || !a.police_pursuit) continue;
                    out.nearest_to_centre_m = std::min(out.nearest_to_centre_m,
                        glm::distance(glm::vec2{a.pos.x, a.pos.z}, centre));
                    if (a.police_last_replan_step < 0) continue;
                    bool ok = false;
                    for (const glm::vec2& l : legal)
                        ok |= glm::distance(a.police_last_target, l) < 0.5f;
                    if (!ok) ++out.routed_off_post;
                }
            }
            if (resumed && out.search_steps > 0 && !crowd.police_searching())
                out.reacquired = true;
        }

        float nearest = std::numeric_limits<float>::infinity();
        std::vector<std::pair<uint64_t, uint32_t>> now;
        for (const VehicleAgent& a : crowd.vehicles()) {
            if (!a.police_unit || !a.police_pursuit) continue;
            now.push_back({a.lane_key, a.slot});
            nearest = std::min(nearest,
                glm::distance(glm::vec2{a.pos.x, a.pos.z}, xz));
            if (step > 600) {
                ++out.unit_steps;
                out.sum_speed += double(a.speed_mps);
                if (a.speed_mps < 2.0f) ++out.stuck_steps;
                if (a.delay_seconds > 1.0f) ++out.queued_steps;
                if (a.maneuver.kind == TrafficManeuverKind::PoliceBypass)
                    ++out.bypassing;
                // A REAL CAR CANNOT SPIN ON THE SPOT. Anything past a few
                // hundred degrees a second is a pose snap, not a turn.
                {
                    // KEYED ON THE DEPARTURE, not on (lane_key, slot). One
                    // agent can retire and the next lap's agent appear at the
                    // same pair with no absent frame between them — see
                    // same_departure() in traffic/crowd.h. Matching on the pair
                    // alone reads that handover as one car spinning 180 in a
                    // single step, which is a measurement artefact and not a
                    // flip. This suite fell for it once already.
                    //
                    // AND THE DEPARTURE KEY IS NOT ENOUGH EITHER, which is how
                    // it fell for it a second time and printed 21,600 deg/s —
                    // exactly 180 a step — for a fault that was not there.
                    // `generation` is the schedule LAP, not a spawn counter: it
                    // recurs, and for these cruisers it is routinely -1 or -2,
                    // so the triple collides. The cars behind those readings had
                    // moved 15, 63, 162, even 270 metres in one 1/120 s step.
                    // Nothing on wheels does that. So the guard that actually
                    // holds is TRAVEL, not identity: a car that jumped further
                    // than it could possibly drive was placed, not turned, and
                    // its heading change means nothing. Keep both — identity
                    // catches most of it and is cheap, travel catches the rest.
                    static std::map<std::tuple<uint64_t, uint32_t, int64_t>,
                                    glm::vec2> last_fwd;
                    static std::map<std::tuple<uint64_t, uint32_t, int64_t>,
                                    glm::vec2> last_xz;
                    const auto id = std::make_tuple(a.lane_key, a.slot,
                                                    a.generation);
                    const glm::vec2 now_fwd{a.fwd.x, a.fwd.z};
                    const glm::vec2 now_xz{a.pos.x, a.pos.z};
                    if (glm::length(now_fwd) > 1e-4f) {
                        const glm::vec2 unit = glm::normalize(now_fwd);
                        const auto seen = last_fwd.find(id);
                        const auto was = last_xz.find(id);
                        // 1 m in a step is 120 m/s. Generous on purpose: the
                        // bound only has to separate driving from placing.
                        const bool placed = was != last_xz.end() &&
                            glm::distance(now_xz, was->second) > 1.0f;
                        if (seen != last_fwd.end() && !placed) {
                            const float d = glm::degrees(std::acos(std::clamp(
                                glm::dot(unit, seen->second), -1.0f, 1.0f)));
                            const float rate = d / float(kSimDt);
                            out.worst_yaw_rate = std::max(out.worst_yaw_rate, rate);
                            if (a.chase_active)
                                out.worst_chase_yaw_rate =
                                    std::max(out.worst_chase_yaw_rate, rate);

                        }
                        last_fwd[id] = unit;
                        last_xz[id] = now_xz;
                    }
                    // VERTICAL CONTINUITY, on the same terms. The free-drive
                    // hand-off seeds a VehicleState from the LANE pose and the
                    // release snaps back to it; if the lane's draped height
                    // disagrees with the surface step_vehicle actually rides
                    // on, the car pops up or drops on the seam. Same "placed
                    // not driven" guard, so a re-instantiation is not counted.
                    static std::map<std::tuple<uint64_t, uint32_t, int64_t>,
                                    float> last_y;
                    const auto had_y = last_y.find(id);
                    const auto was_xz = last_xz.find(id);
                    const bool teleported = was_xz != last_xz.end() &&
                        glm::distance(now_xz, was_xz->second) > 1.0f;
                    if (had_y != last_y.end() && !teleported) {
                        const float dy = std::fabs(a.pos.y - had_y->second);
                        out.worst_step_dy = std::max(out.worst_step_dy, dy);
                        if (a.chase_active)
                            out.worst_chase_step_dy =
                                std::max(out.worst_chase_step_dy, dy);
                    }
                    last_y[id] = a.pos.y;
                    // And is the body sitting ON what it is driving over?
                    const auto probe = world.probe_down(
                        a.pos + glm::vec3{0.0f, 4.0f, 0.0f}, 14.0f);
                    if (probe.hit) {
                        const float clearance = a.pos.y - probe.point.y;
                        out.worst_float_m = std::max(out.worst_float_m, clearance);
                        if (a.chase_active)
                            out.worst_chase_float_m =
                                std::max(out.worst_chase_float_m, clearance);
                        else
                            out.worst_lane_float_m =
                                std::max(out.worst_lane_float_m, clearance);
                        out.worst_sink_m = std::max(out.worst_sink_m, -clearance);
                    }
                }
                if (off_road)
                    out.closest_offroad = std::min(out.closest_offroad,
                        glm::distance(glm::vec2{a.pos.x, a.pos.z}, xz));
                if (a.chase_active) {
                    ++out.off_road;
                    // THE DEFECT ITSELF. A cruiser backing up is doing one leg
                    // of a turn; a cruiser that spends the chase backing up is
                    // the bug. The gap CANNOT see this — reversing toward the
                    // player shrinks the gap, which is why it measured well
                    // and looked wrong.
                    if (vehicle_forward_speed(a.chase) < -0.5f) ++out.reversing;
                    // Pointed the wrong way and going nowhere. A turn-around
                    // that works is BRIEF; one that never comes round leaves
                    // the cruiser sat here for the rest of the chase.
                    const glm::vec2 to_p = xz - glm::vec2{a.pos.x, a.pos.z};
                    if (glm::length(to_p) > 1e-3f &&
                        glm::dot(glm::normalize(glm::vec2{a.fwd.x, a.fwd.z}),
                                 glm::normalize(to_p)) < -0.25f &&
                        std::fabs(vehicle_forward_speed(a.chase)) < 5.0f)
                        ++out.facing_away;
                }
            }
        }
        // Ignore the opening seconds: the dispatcher converts one unit per
        // cadence tick, so the first pursuers are legitimately still arriving.
        if (step > 600 && std::isfinite(nearest)) {
            ++out.samples;
            out.sum_nearest += double(nearest);
            out.worst = std::max(out.worst, nearest);
            if (nearest <= 60.0f) ++out.within60;
        }
        if (step > 600)
            for (const auto& id : live)
                if (std::find(now.begin(), now.end(), id) == now.end())
                    ++out.deletions;
        live = std::move(now);

        if (!stopped) station += speed_mps * static_cast<float>(kSimDt);
        if (scenario == 5) {
            if (turns >= 2 && !stopped && !resumed && station >= 40.0f) {
                stopped = true; stop_step = step;
            }
            if (stopped && step >= stop_step + 1800) {
                // Double back toward the corner he vanished from.
                stopped = false; resumed = true;
                const LaneRef back = lanes.opposing(lane);
                if (lanes.valid(back)) {
                    const auto here = lanes.pose(lane, station).position;
                    const auto onto = lanes.project_onto(back, {here.x, here.z});
                    if (onto.valid()) { lane = back; station = onto.dist_along_m; }
                }
            }
        }
        // A REAL PLAYER DOUBLES BACK. Fleeing in a straight line forever
        // never puts a cruiser behind you at low speed, so it never exercises
        // the turn-around at all — which is why a broken one went unmeasured.
        if (scenario == 3 && step > 600 && step % 1440 == 0) {
            const LaneRef back = lanes.opposing(lane);
            if (lanes.valid(back)) {
                const auto here = lanes.pose(lane, station).position;
                const auto onto = lanes.project_onto(back, {here.x, here.z});
                if (onto.valid()) { lane = back; station = onto.dist_along_m; }
            }
        }
        if (station >= lanes.length(lane)) {
            station -= lanes.length(lane);
            LaneRef next = kInvalidLane;
            if (scenario == 5 && step > 600 && turns < 2 && !resumed) {
                next = flee_turn_off(lanes, lane);
                if (lanes.valid(next)) ++turns;
            }
            if (!lanes.valid(next)) next = flee_next(lanes, lane);
            if (!lanes.valid(next)) break;
            lane = next;
        }
    }
    out.ai_collisions = crowd.stats().ai_collisions;
    return out;
}

void a_pursuit_stays_on_a_fleeing_player() {
    TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    LaneGraph lanes;
    roads.build(city::map_spines(), {}, ground.sampler());
    lanes.build(roads, ground.sampler());

    Chase chase;
    for (int scenario = 0; scenario < 5; ++scenario) {
        const Chase one = run_chase(lanes, ground.sampler(),
                                    scenario == 2 ? 24.0f : 18.0f, 3,
                                    120 * 40, scenario);
        std::printf("      start %d%s @ %.0f m/s: mean gap %.1f m, worst %.1f m, "
                    "within 60 m %.0f%%, lost %zu, units %.1f m/s, "
                    "stopped %.0f%%, queued %.0f%%\n",
            scenario, scenario == 3 ? " (doubles back)" : scenario == 4 ? " (pulls OFF-ROAD)" : "",
            scenario == 2 ? 24.0 : 18.0,
            double(one.mean()), double(one.worst),
            100.0 * double(one.within60) / double(one.samples ? one.samples : 1),
            one.deletions,
            one.sum_speed / double(one.unit_steps ? one.unit_steps : 1),
            100.0 * double(one.stuck_steps) / double(one.unit_steps ? one.unit_steps : 1),
            100.0 * double(one.queued_steps) / double(one.unit_steps ? one.unit_steps : 1));
        if (scenario == 4)
            std::printf("        -> closest any cruiser got to the off-road player: %.1f m\n",
                double(one.closest_offroad));

        chase.samples += one.samples;
        chase.within60 += one.within60;
        chase.sum_nearest += one.sum_nearest;
        chase.worst = std::max(chase.worst, one.worst);
        chase.deletions += one.deletions;
        chase.ai_collisions += one.ai_collisions;
        chase.unit_steps += one.unit_steps;
        chase.stuck_steps += one.stuck_steps;
        chase.queued_steps += one.queued_steps;
        chase.sum_speed += one.sum_speed;
        chase.off_road += one.off_road;
        chase.reversing += one.reversing;
        chase.facing_away += one.facing_away;
        chase.worst_yaw_rate = std::max(chase.worst_yaw_rate, one.worst_yaw_rate);
        chase.worst_step_dy = std::max(chase.worst_step_dy, one.worst_step_dy);
        chase.worst_chase_step_dy =
            std::max(chase.worst_chase_step_dy, one.worst_chase_step_dy);
        chase.worst_float_m = std::max(chase.worst_float_m, one.worst_float_m);
        chase.worst_chase_float_m =
            std::max(chase.worst_chase_float_m, one.worst_chase_float_m);
        chase.worst_lane_float_m =
            std::max(chase.worst_lane_float_m, one.worst_lane_float_m);
        chase.worst_sink_m = std::max(chase.worst_sink_m, one.worst_sink_m);
        chase.worst_chase_yaw_rate =
            std::max(chase.worst_chase_yaw_rate, one.worst_chase_yaw_rate);
    }
    std::printf("      ALL: mean gap %.1f m, worst %.1f m, within 60 m %.0f%%, "
                "lost %zu, ai collisions %zu; units %.1f m/s, stopped %.0f%%, "
                "queued %.0f%%, off road %.0f%%, REVERSING %.1f%%, facing away %.1f%%\n",
        double(chase.mean()), double(chase.worst),
        100.0 * double(chase.within60) / double(chase.samples ? chase.samples : 1),
        chase.deletions, chase.ai_collisions,
        chase.sum_speed / double(chase.unit_steps ? chase.unit_steps : 1),
        100.0 * double(chase.stuck_steps) / double(chase.unit_steps ? chase.unit_steps : 1),
        100.0 * double(chase.queued_steps) / double(chase.unit_steps ? chase.unit_steps : 1),
        100.0 * double(chase.off_road) / double(chase.unit_steps ? chase.unit_steps : 1),
        100.0 * double(chase.reversing) / double(chase.unit_steps ? chase.unit_steps : 1),
        100.0 * double(chase.facing_away) / double(chase.unit_steps ? chase.unit_steps : 1));

    REQUIRE(chase.samples > 0);
    const float stopped_pct =
        100.0f * float(chase.stuck_steps) / float(chase.unit_steps);

    // THE ONE THAT MATTERS. A pursuer must not be deleted out from under the
    // chase; ordinary traffic retirement used to take every one of them.
    REQUIRE_MSG(chase.deletions == 0,
                "a pursuer was deleted mid-chase", "persistence");

    // STUCK IN TRAFFIC — the complaint this suite exists for, and the one the
    // gap alone will not catch: in a jammed district the gap barely moves
    // however well the cruisers drive, because the whole city is crawling.
    // What DOES move is how much of the chase a unit spends at a standstill.
    // Measured at 36% with no queue jump and 23% with it, so 32% sits between
    // the two and fails the moment cruisers start waiting out queues again.
    REQUIRE_MSG(stopped_pct < 28.0f,
                "pursuit units are sitting in traffic instead of chasing",
                "stuck");

    // THEY STAY ON THE ROAD — the other half of the complaint, and the one
    // no gap or speed figure catches. A lane-following agent cannot chase you
    // through a car park however well it is tuned, so this asserts that the
    // free-drive hand-off is actually happening: cruisers spend real time off
    // the lane graph, steered at the player by the physics. Measured at 8%;
    // zero means the hand-off has silently stopped engaging and the pursuit is
    // back to being traffic with a siren.
    REQUIRE_MSG(100.0f * float(chase.off_road) / float(chase.unit_steps) > 3.0f,
                "no pursuer ever left the road to cut at the player",
                "free-drive");

    // It has to stay in the mirror. These are the measured figures with
    // headroom, NOT aspirations: one of the three scenarios runs through a
    // district whose ambient traffic averages 4.5 m/s, and no police change
    // makes a cruiser outrun the medium it is driving in.
    // REVERSING INSTEAD OF TURNING AROUND. With the target behind it, a
    // controller that only knows how to aim backs up — and backing up with the
    // wheels turned AT the target swings the nose further AWAY, so the car
    // never comes round and simply keeps reversing.
    //
    // These two are printed because they are what the defect actually looks
    // like, and they are asserted as loose ceilings. Be honest about what they
    // do NOT do: at the tuned 22 m hand-off neither figure separates the fixed
    // controller from the broken one (1.5%/0.5% against 1.2%/0.7%) — the GAP
    // is what catches it there, at 69 m against 93 m. They earn their place
    // for the opposite reason: they are the only numbers that stay honest if
    // someone widens the hand-off again, where the broken version scores a
    // BETTER gap than the fixed one by reversing toward the player.
    const float reversing_pct =
        100.0f * float(chase.reversing) / float(chase.unit_steps);
    const float facing_away_pct =
        100.0f * float(chase.facing_away) / float(chase.unit_steps);
    REQUIRE_MSG(reversing_pct < 2.5f,
                "pursuers are spending the chase in reverse", "reversing");
    REQUIRE_MSG(facing_away_pct < 2.0f,
                "pursuers sit pointed away from the player instead of coming round",
                "turn-around");

    // A CAR CANNOT SPIN ON THE SPOT. The free-drive hand-off seeds a real
    // VehicleState from the lane pose, and getting that orientation backwards
    // is a clean 180 degrees in a single step — 21,600 deg/s at 120 Hz — right
    // in front of the player, on every engagement. It also poisons everything
    // downstream, because a car seeded backwards reads "the target is behind
    // me" forever and spends the engagement turning around from a spin it
    // never performed.
    std::printf("      worst yaw rate: %.0f deg/s any pursuer, %.0f deg/s free-driving\n",
        double(chase.worst_yaw_rate), double(chase.worst_chase_yaw_rate));
    std::printf("      HEIGHT: worst one-step pop %.2f m any pursuer, %.2f m free-driving;"
                " floats up to %.2f m (free-driving %.2f m, lane-following %.2f m),"
                " sinks up to %.2f m\n",
        double(chase.worst_step_dy), double(chase.worst_chase_step_dy),
        double(chase.worst_float_m), double(chase.worst_chase_float_m),
        double(chase.worst_lane_float_m), double(chase.worst_sink_m));
    // FLOATING. VehicleAgent::pos is a ROAD SURFACE point — traffic_visual
    // lifts the body onto its wheels from there — while VehicleState::position
    // is the chassis origin sitting up on its springs. Writing one into the
    // other without vehicle_rest_ride_height() left free-driving cruisers
    // hovering by exactly a ride height (0.59 m against 0.14 m for the same
    // cars on the lane path). The pop bound is the hand-off seam itself: the
    // step where a cruiser stops being a lane pose and becomes a car must not
    // move it vertically at all. Both are generous against the measured 0.09 m
    // and 0.00 m, because a cop genuinely leaves the ground over a kerb.
    std::printf("      height: free-driving floats %.2f m, hand-off pop %.2f m\n",
        double(chase.worst_chase_float_m), double(chase.worst_chase_step_dy));
    REQUIRE_MSG(chase.worst_chase_float_m < 0.45f,
                "free-driving cruisers are floating above the road", "height");
    REQUIRE_MSG(chase.worst_chase_step_dy < 0.15f,
                "the free-drive hand-off moves the car vertically", "height");

    REQUIRE_MSG(chase.worst_chase_yaw_rate < 400.0f,
                "a free-driving cruiser snapped its heading instead of turning",
                "pose-snap");

    // The other figure used to be unassertable. It read 21,600 deg/s and the
    // note here said lane-following traffic snapped 180 degrees at junctions,
    // in the junction turn commit, predating this work. Two of those three
    // were wrong.
    //
    // Most of the number was this suite measuring itself: cruisers being
    // PLACED — re-activated, or a fresh departure landing on an identity a
    // retired one had just vacated — and the reading is now guarded on travel,
    // not just on the departure key. See the measurement above.
    //
    // What was left was real, and it was not in src/traffic/ at all. Both
    // causes were lane geometry: offset_polyline() folded the centreline back
    // on itself at corners tighter than the lane is wide, and pose() returned a
    // piecewise-constant tangent, so heading was a step function that jumped by
    // the whole authored deflection at a shape point. Fixing those took this
    // figure from 9,556 deg/s to 1,869 with the measurement held constant.
    //
    // So it is assertable now, and the bound is set where a regression to
    // either cause is caught rather than where today's number happens to sit.
    // What keeps it above a few hundred is a third defect and not these two: a
    // degree-2 seam admits up to 14 degrees of heading change in one step. When
    // that lands, tighten this.
    REQUIRE_MSG(chase.worst_yaw_rate < 2500.0f,
                "lane-following traffic snapped its heading instead of turning",
                "pose-snap");

    REQUIRE_MSG(chase.worst < 280.0f,
                "the nearest pursuer fell out of the chase", "gap");
    REQUIRE_MSG(chase.mean() < 100.0f,
                "the pursuit sits too far back to read as a chase", "gap");
    REQUIRE_MSG(100.0f * float(chase.within60) / float(chase.samples) > 36.0f,
                "a cruiser is rarely close enough to matter", "pressure");
    // Pushing through right of way is licensed; demolishing the city is not.
    REQUIRE_MSG(chase.ai_collisions < 60u,
                "pursuit driving is wrecking ambient traffic", "safety");

    // SCENARIO 5 — SEARCH MODE (PENG-44). Flee straight, turn off onto a side
    // street, stop 60 m in, sit for fifteen seconds, then double back. What
    // must happen while nobody can see him: the crowd says it is searching,
    // the wanted centre sits exactly where he was last seen and does not
    // creep after him, no pursuer is routed to where he actually is, and at
    // least one unit reaches the corner. What must happen when he comes back
    // into view: the search ends.
    const Chase search = run_chase(lanes, ground.sampler(), 18.0f, 3, 120 * 105, 5);
    std::printf("      search: %ld searching steps (%ld unflagged, %ld drifted, "
                "%ld routed off post), nearest unit %.1f m from the centre, "
                "player %.1f m from it, LOS broke %d, reacquired %d\n",
        search.search_steps, search.search_not_flagged, search.centre_drift,
        search.routed_off_post, double(search.nearest_to_centre_m),
        double(search.live_to_centre_m), search.los_ever_broke, search.reacquired);
    REQUIRE_MSG(search.search_steps > 0, "the player was never unseen while stopped", "search");
    REQUIRE_MSG(search.live_to_centre_m > 20.0f,
                "the player never got away from the point he was last seen at", "search");
    REQUIRE_MSG(search.search_not_flagged == 0,
                "the crowd was not searching while nobody could see the player", "search");
    REQUIRE_MSG(search.centre_drift == 0,
                "the wanted centre moved while nobody could see the player", "search");
    REQUIRE_MSG(search.routed_off_post == 0,
                "a pursuer was routed somewhere other than the centre or a post", "search");
    REQUIRE_MSG(search.nearest_to_centre_m < 30.0f,
                "no unit ever reached the corner the player vanished from", "search");
    REQUIRE_MSG(search.reacquired, "the search never ended once he came back into view", "search");

    apricot_test::pass("a dispatched pursuit keeps station on a player fleeing the authored city");
}

}  // namespace

int main() {
    a_pursuit_stays_on_a_fleeing_player();
    return apricot_test::done("police_chase_tests");
}
