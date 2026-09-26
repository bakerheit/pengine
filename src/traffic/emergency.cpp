#include "traffic/crowd.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <numeric>
#include <tuple>

#include "city/city_rng.h"
#include "core/fixed_step.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"

namespace apricot {
namespace {
glm::vec2 xz(glm::vec3 p) { return {p.x, p.z}; }

bool footprints_overlap(const LanePose& a, float aw, float al,
                        glm::vec3 pos, glm::vec3 forward, float bw, float bl) {
    if (std::isfinite(pos.y) && std::fabs(a.position.y - pos.y) > 2.2f) return false;
    const glm::vec2 af = glm::normalize(xz(a.tangent));
    const glm::vec2 bf = glm::normalize(xz(forward));
    const glm::vec2 ar{-af.y, af.x}, br{-bf.y, bf.x};
    const glm::vec2 delta = xz(pos - a.position);
    for (glm::vec2 axis : {af, ar, bf, br}) {
        const float radius = al * std::fabs(glm::dot(af, axis)) +
            aw * std::fabs(glm::dot(ar, axis)) +
            bl * std::fabs(glm::dot(bf, axis)) + bw * std::fabs(glm::dot(br, axis));
        if (std::fabs(glm::dot(delta, axis)) >= radius) return false;
    }
    return true;
}

// HOW LONG AN ARC MAY SIT AT ZERO BEFORE IT IS ABANDONED. One second at the
// 120 Hz sim step. Short on purpose: a blocked arc costs nothing to drop and
// re-plan a moment later — the planner reassesses at 10 Hz and simply will not
// re-admit a move that is still blocked — whereas holding one costs the car
// every recovery clock it owns (see VehicleAgent::maneuver_stall_steps). It
// also has to stay well inside CrowdTuning::intersection_escape_after_steps,
// because a car wedged mid-junction has to be back under ordinary lane rules
// in time for that escape to fire.
constexpr int64_t kManeuverStallLimitSteps = 120;

// THE SHORTEST EDGE-AROUND WORTH PLANNING. Two body lengths: enough to leave
// the lane, clear a parked body and come back. The recovery nudge falls back
// to this when the junction ahead leaves no room for its preferred 16 m,
// because the alternative is not a safer car, it is a permanently wedged one.
constexpr float kNudgeRunFloorM = 10.0f;

void append(TrafficManeuver& m, const LanePose& a, const LanePose& b) {
    auto& curve = m.curves[m.count++];
    curve = traffic_steering_curve(a, b);
    m.length_m += curve.length_m;
}

// Out, past, and back: three curves that leave the lane centre by `offset_m`,
// hold it for the middle of the run and return to the centre at the end.
// destination is the lane itself — the car never changes lane key. This is
// the police bypass's geometry, lifted so a civilian can use it.
TrafficManeuver lateral_pass_arc(const LaneGraph& graph, LaneRef lane,
                                 const LanePose& start, float from_m,
                                 float run_m, float offset_m, float speed_mps,
                                 TrafficManeuverKind kind) {
    TrafficManeuver move;
    move.kind = kind;
    move.destination = lane;
    move.end_station_m = from_m + run_m;
    move.speed_limit_mps = speed_mps;
    const float transition = std::min(13.0f, run_m * .33f);
    const auto out = graph.pose(lane, from_m + transition, offset_m);
    const auto pass = graph.pose(lane, move.end_station_m - transition, offset_m);
    append(move, start, out);
    append(move, out, pass);
    append(move, pass, graph.pose(lane, move.end_station_m));
    return move;
}

// One S-curve from the car's real pose onto lane `to` at station `end_m`.
// destination is the new lane: completion re-homes the car there.
TrafficManeuver lane_shift_arc(const LaneGraph& graph, const LanePose& start,
                               LaneRef to, float end_m, float speed_mps) {
    TrafficManeuver move;
    move.kind = TrafficManeuverKind::LaneChange;
    move.destination = to;
    move.end_station_m = end_m;
    move.speed_limit_mps = speed_mps;
    append(move, start, graph.pose(to, end_m));
    return move;
}
}

TrafficTurnCurve traffic_steering_curve(const LanePose& a, const LanePose& b) {
    TrafficTurnCurve curve;
    curve.p0 = a.position;
    curve.p3 = b.position;
    const float chord = glm::distance(a.position, b.position);
    const float handle = chord * (glm::dot(a.tangent, b.tangent) < -0.5f ? 0.9f : 0.4f);
    curve.p1 = a.position + a.tangent * handle;
    curve.p2 = b.position - b.tangent * handle;
    glm::vec3 prev = curve.p0;
    for (std::size_t n = 1; n < curve.arc_m.size(); ++n) {
        const float t = float(n) / float(curve.arc_m.size() - 1), u = 1.f - t;
        const glm::vec3 p = u*u*u*curve.p0 + 3*u*u*t*curve.p1 +
                            3*u*t*t*curve.p2 + t*t*t*curve.p3;
        curve.arc_m[n] = curve.arc_m[n-1] + glm::distance(prev, p);
        prev = p;
    }
    curve.length_m = curve.arc_m.back();
    return curve;
}

LanePose traffic_maneuver_pose(const TrafficManeuver& move, float d) {
    for (uint8_t n = 0; n < move.count; ++n) {
        if (d <= move.curves[n].length_m || n + 1 == move.count)
            return traffic_turn_pose(move.curves[n], d);
        d -= move.curves[n].length_m;
    }
    return {};
}

bool Crowd::emergency_path_clear(uint32_t own, const TrafficManeuver& move,
                                 float from, float distance, bool check_world,
                                 bool ignore_player) const {
    const auto footprint = traffic_vehicle_footprint(traffic_vehicle_kind(vehicles_[own]));
    // The safety pad. Named because the scenery rule below has to reference
    // the same number: what that rule forgives is exactly a conflict the pad
    // invented, and forgiving more than the pad would be forgiving a real one.
    const float kSweepPadM = 0.28f;
    const float width = footprint.half_width_m + kSweepPadM;
    const float length = footprint.half_length_m + 0.45f;
    const float end = std::min(move.length_m, from + distance);
    const int samples = std::max(1, int(std::ceil((end - from) / 0.85f)));
    std::vector<uint32_t> nearby;  // reused by every sample's world check
    for (int s = 0; s <= samples; ++s) {
        const float d = from + (end - from) * float(s) / float(samples);
        const LanePose pose = traffic_maneuver_pose(move, d);
        const float seconds = (d - from) / std::max(2.0f, move.speed_limit_mps);
        // SCENERY THAT MERELY CLIPS THE LANE CANNOT VETO THE ARC THAT GOES
        // ROUND IT.
        //
        // `width` pads the body by kSweepPadM, and that pad is a margin for
        // things that MOVE. Against a car parked legally at a kerb it is not a
        // margin at all: every Street bay stands a body 1.10 m from the lane
        // centre, and a padded box truck driving that centre needs 1.43 m. So
        // this sweep declared the lane a car was already legally sitting in to
        // be blocked, and then refused every arc out of it — overtake, bypass,
        // nudge — on the authority of the one body the arc existed to escape.
        // Standing still became the only admissible state, which is precisely
        // what "stuck behind a parked car" is. Measured on the island: 25.7%
        // of parked cars did this to a box truck, and the recovery ladder
        // despawned the car 35 s later rather than ever planning a nudge.
        //
        // Two conditions, and BOTH are needed — the first draft had only the
        // spirit of the second and admitted a pull-aside straight into a
        // blocked shoulder (tests/emergency_traffic_tests.cpp pins that):
        //
        //   (a) the conflict must already exist at the BASELINE — the pose
        //       this car would hold at the same station driving its lane
        //       normally. That is what makes it not the arc's doing. Two
        //       cheaper-looking versions of this were tried and both failed:
        //       comparing lateral distance to the lane centre needs a fudge
        //       factor to survive a centimetre of drift at the arc's first
        //       sample, and ANY fudge, down to 2 cm, let through arcs that
        //       wedged cars inside junctions for 19 s; comparing against the
        //       car's CURRENT lateral offset instead forbids the return leg,
        //       because coming back to the lane always closes on the kerb.
        //   (b) it must only be CLIPPING the corridor — reaching into the
        //       car's true footprint by less than the pad. A body standing in
        //       the lane still vetoes everything, which is right: that is a
        //       road you cannot drive, not a kerb you squeeze past.
        //
        // Anything that can move keeps the full padded test, which is why this
        // reads `other.scenery` and never `other.speed < eps`: a car stopped
        // in a queue starts again, and the pad is what keeps an arc out of it.
        bool baseline_ready = false;
        LanePose baseline{};
        auto baseline_pose = [&]() -> const LanePose& {
            if (!baseline_ready) {
                baseline_ready = true;
                const LaneRef mine = vehicles_[own].lane;
                const LaneProjection at = graph_->valid(mine)
                    ? graph_->project_onto(mine, xz(pose.position))
                    : LaneProjection{};
                baseline = at.valid() ? graph_->pose(mine, at.dist_along_m) : pose;
            }
            return baseline;
        };
        auto scenery_excused = [&](const EmergencyBody& other) {
            if (!other.scenery) return false;
            const LaneRef mine = vehicles_[own].lane;
            if (!graph_->valid(mine)) return false;
            const LaneProjection body = graph_->project_onto(mine, xz(other.pos));
            if (!body.valid()) return false;
            if (!footprints_overlap(baseline_pose(), width, length, other.pos,
                                    other.fwd, other.half_width,
                                    other.half_length)) return false;   // (a)
            return footprint.half_width_m + other.half_width -
                   std::fabs(body.lateral_m) < kSweepPadM;              // (b)
        };
        auto blocks = [&](const EmergencyBody& other, bool predict) {
            // Check the current footprint too: assuming that a lead will keep
            // moving is how a maneuver gets admitted into a stopped queue.
            if (footprints_overlap(pose, width, length, other.pos, other.fwd,
                                   other.half_width, other.half_length))
                return !scenery_excused(other);
            if (!predict || other.speed < 0.01f) return false;
            if (glm::dot(emergency_frozen_[own].fwd, other.fwd) < -0.2f) {
                const float travel = other.speed * (seconds + 0.8f);
                if (footprints_overlap(pose, width, length,
                        other.pos + other.fwd * (travel * .5f), other.fwd,
                        other.half_width + .2f, other.half_length + travel*.5f)) return true;
            }
            LanePose future;
            if (other.move.active()) {
                future = traffic_maneuver_pose(other.move,
                    other.move.progress_m + other.speed * (seconds + 0.5f));
            } else {
                future.position = other.pos + other.fwd * other.speed * (seconds + 0.5f);
                future.tangent = other.fwd;
            }
            return footprints_overlap(pose, width, length, future.position,
                future.tangent, other.half_width + 0.2f, other.half_length + 0.5f);
        };
        for (uint32_t n = 0; n < emergency_frozen_.size(); ++n)
            if (n != own && blocks(emergency_frozen_[n], true)) return false;
        for (std::size_t o = 0; o < emergency_obstacles_.size(); ++o) {
            // A ram is aimed at the player. Treating his car as an obstacle
            // would reject every arc that actually reaches him, which is how a
            // "ram" quietly degrades into a cruiser politely holding station.
            if (ignore_player && o == emergency_player_obstacle_) continue;
            if (blocks(emergency_obstacles_[o], true)) return false;
        }
        // New plans are reserved in stable identity order. This prevents two
        // cars deciding to merge into the same empty patch on the same step.
        for (uint32_t n : emergency_reservations_) {
            if (n == own) continue;
            const auto& reserved = emergency_frozen_[n].move;
            for (float r = reserved.progress_m; r <= reserved.length_m; r += 1.5f) {
                const auto other = traffic_maneuver_pose(reserved, r);
                const auto body = traffic_vehicle_footprint(traffic_vehicle_kind(vehicles_[n]));
                if (footprints_overlap(pose, width, length, other.position,
                        other.tangent, body.half_width_m, body.half_length_m)) return false;
            }
        }
        if (!check_world) continue;
        const auto& lane = graph_->lane(vehicles_[own].lane);
        const auto projection = graph_->project_onto(vehicles_[own].lane, xz(pose.position));
        // No unbounded shortcut through a block, across a median or off a bridge.
        const float shoulder = lane.sidewalks ? 0.0f : 1.4f;
        if (!projection.valid() || std::fabs(projection.lateral_m + lane.lateral_offset_m)
                + width > lane.width_m * 0.5f + shoulder + 0.05f) return false;
        if (!police_officer_world_) continue;
        for (float along : {-length, 0.0f, length}) {
            for (float side : {-width, width}) {
                const glm::vec3 point = pose.position + pose.tangent * along + pose.right * side;
                const auto hit = police_officer_world_->probe_down(
                    point + glm::vec3{0, 0.45f, 0}, 1.0f,
                    TerrainCollider::ProbeVehicles::Exclude);
                if (!hit.hit || hit.prop || hit.normal.y < 0.88f ||
                    std::fabs(hit.point.y - point.y) > 0.24f) return false;
            }
        }
        // THE BOXES AROUND THIS SAMPLE, NOT THE CITY'S. This loop used to read
        // every box on the island, 0.85 m apart along every arc a cruiser
        // stuck in a queue tried ten times a second, and in a five-star chase
        // that was single steps of 17-22 ms (terrain_collider.h has the
        // numbers). The reach bounds the padded footprint's corners in any
        // box's frame — root two of the swept half-extents, rounded up — so
        // every box the test below could reject on is in the list;
        // collider_broad_phase_tests checks this formula against that test.
        const float reach = 1.5f * (length * glm::length(xz(pose.tangent)) +
                                    width * glm::length(xz(pose.right))) + 0.05f;
        police_officer_world_->boxes_near(xz(pose.position) - reach,
                                          xz(pose.position) + reach, nearby);
        const auto& boxes = police_officer_world_->static_boxes();
        for (const uint32_t slot : nearby) {
            const StaticBox& box = boxes[slot];
            if (!box.enabled || box.bounds.max.y < pose.position.y + 0.25f ||
                box.bounds.min.y > pose.position.y + 1.9f) continue;
            const auto& b = box.collision_bounds();
            const glm::vec3 p = box.local_point(pose.position);
            const glm::vec3 f = box.local_direction(pose.tangent);
            const glm::vec3 r = box.local_direction(pose.right);
            const float ex = std::fabs(f.x)*length + std::fabs(r.x)*width;
            const float ez = std::fabs(f.z)*length + std::fabs(r.z)*width;
            if (p.x + ex > b.min.x && p.x - ex < b.max.x &&
                p.z + ez > b.min.z && p.z - ez < b.max.z) return false;
        }
    }
    return true;
}

void Crowd::prepare_emergency_maneuvers(int64_t step, const VehicleState* player,
                                       const OnFootTrafficHazard* on_foot) {
    emergency_frozen_.clear();
    emergency_obstacles_.clear();
    emergency_reservations_.clear();
    std::vector<uint32_t> responders;
    bool needed = false;
    for (uint32_t i = 0; i < vehicles_.size(); ++i) {
        const auto& v = vehicles_[i];
        if (v.police_pursuit && police_officer_driving_allowed(v.officer) &&
            !vehicle_engine_failed(v.mechanical) &&
            !(v.speed_mps < .1f && police_target_speed_mps_ < .5f &&
              glm::distance(xz(v.pos), police_target_xz_) < 14.0f)) responders.push_back(i);
        needed |= v.maneuver.active() || std::fabs(v.roadside_offset_m) > 0.01f ||
                  v.emergency_yield != EmergencyYield::None ||
                  civilian_plan_candidate(i);
    }
    if (!needed && responders.empty()) return;
    emergency_frozen_.reserve(vehicles_.size());
    for (const auto& v : vehicles_) {
        const auto body = traffic_vehicle_footprint(traffic_vehicle_kind(v));
        emergency_frozen_.push_back({v.pos, v.fwd, v.speed_mps,
            body.half_width_m, body.half_length_m, v.maneuver});
        if (v.police_unit && police_officer_on_foot(v.officer))
            emergency_obstacles_.push_back({v.officer.pos, {1,0,0}, 0, 0.5f, 0.5f, {}});
    }
    for (const auto& p : peds_)
        emergency_obstacles_.push_back({p.pos, {1,0,0}, 0, 0.65f, 0.65f, {}});
    for (const auto& p : parked_vehicle_positions_)
        emergency_obstacles_.push_back({p, {1,0,0}, 0, 2.5f, 2.5f, {}, true});
    emergency_player_obstacle_ = static_cast<std::size_t>(-1);
    if (player) {
        emergency_player_obstacle_ = emergency_obstacles_.size();
        emergency_obstacles_.push_back({player->position, vehicle_forward(*player),
            std::max(0.f, vehicle_forward_speed(*player)), kPlayerHalfWidthM, kPlayerHalfLengthM, {}});
    }
    if (on_foot) emergency_obstacles_.push_back({
        {on_foot->position.x, on_foot->height_m, on_foot->position.y}, {1,0,0}, 0, .6f, .6f, {}});
    // Ambient parked cars near anyone who might plan an arc this step. Static
    // bodies with their real footprints; the sweep tests them like any other
    // obstacle. Gathered per vehicle from the lane-keyed list, so the cost is
    // bounded by who is resident, not by how many cars the island parks.
    if (tuning_.civilian.ambient_parked_obstacles && !ambient_parked_.empty()) {
        std::vector<uint32_t> near;
        for (const auto& v : vehicles_) {
            if (v.chase_active || !graph_->valid(v.lane)) continue;
            gather_parked_near(v.lane, v.pos, 70.0f, near);
        }
        std::sort(near.begin(), near.end());
        near.erase(std::unique(near.begin(), near.end()), near.end());
        for (uint32_t index : near) {
            const AmbientParkedCar& car = ambient_parked_[index];
            const auto fp = traffic_vehicle_footprint(car.kind);
            emergency_obstacles_.push_back({car.pos, car.fwd, 0.0f,
                                            fp.half_width_m, fp.half_length_m,
                                            {}, true});
        }
    }

    std::vector<uint32_t> order(vehicles_.size());
    std::iota(order.begin(), order.end(), 0u);
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        return std::tie(vehicles_[a].lane_key, vehicles_[a].slot) <
               std::tie(vehicles_[b].lane_key, vehicles_[b].slot);
    });
    EmergencyYieldTuning yield;
    yield.detect_range = tuning_.emergency_response_radius_m;
    for (uint32_t i : order) {
        auto& v = vehicles_[i];
        if (!graph_->valid(v.lane)) continue;
        const auto& lane = graph_->lane(v.lane);
        const bool committed = v.turn_from_lane != kInvalidLane ||
                               v.committed_junction != UINT32_MAX;
        EmergencyYield now = EmergencyYield::None;
        if (!v.police_pursuit && !v.snowplow_unit) {
            for (uint32_t cop : responders) {
                const auto& police = vehicles_[cop];
                if (!graph_->valid(police.lane) ||
                    std::fabs(police.pos.y - v.pos.y) > 2.2f) continue;
                // A stopped cruiser still requesting passage must not drop
                // the response and trap itself behind traffic again.
                const auto f = xz(police.fwd);
                const auto response = emergency_yield_classify(xz(v.pos), xz(v.fwd),
                    committed, false, {xz(police.pos), f * std::max(3.1f, police.speed_mps)}, yield);
                if (response != EmergencyYield::None) now = response;
            }
        }
        const float stagger = 1.0f + float(splitmix64_mix(v.lane_key ^ v.slot) % 101u) * .01f;
        emergency_yield_tick(v.emergency_yield, v.emergency_resume_s, now, stagger, float(kSimDt));
        // A cruiser that has left the road is not planning lane maneuvers.
        if (v.chase_active) continue;
        if (v.maneuver.active() || committed || vehicle_engine_failed(v.mechanical) ||
            v.collision_recovery_seconds > 0 ||
            (std::fabs(v.roadside_offset_m) < .01f &&
             glm::length(v.collision_offset_xz) > 0.15f)) continue;
        // Expensive swept checks run at 10 Hz per driver, with a stable phase.
        if ((uint64_t(step) + splitmix64_mix(v.lane_key ^ v.slot)) % 12u != 0u) continue;
        const float clear_from = junction_clearance(lane.junction_from) + 4.0f;
        const float clear_to = lane.length_m - junction_clearance(lane.junction_to) - 4.0f;
        // THE JUNCTION APPROACH IS WHERE QUEUES ARE, AND IT WAS THE ONE PLACE
        // NOBODY WAS ALLOWED TO MOVE ASIDE.
        //
        // This window keeps ordinary lane changes out of a junction, which is
        // right — but a queue only ever forms at a stop line, so refusing to
        // plan there meant a cruiser could not pass the queue AND the civilian
        // in front of it could not yield out of the way, at exactly the moment
        // a siren was behind it. Measured, that is where a pursuit died: units
        // averaged 8.7 m/s against a suspect doing 18 and sat under 2 m/s for
        // 29% of the chase. The two emergency cases are exempt; ordinary
        // traffic keeps the window it always had.
        const bool engaged = v.police_pursuit && police_wanted_level_ > 0 &&
                             police_officer_driving_allowed(v.officer);
        const bool queue_blocked = i < leader_gap_.size() &&
            leader_gap_[i] < 34.0f && leader_speed_[i] < 3.0f && v.speed_mps < 6.0f;
        // Strictly a DISPATCHED CRUISER. Letting a yielding civilian plan in
        // here too was tried and measured: it changed nothing, and it let a
        // car pull onto the kerb with no room to merge back — which strands it
        // there, because MergeBack needs the same room it just gave up.
        const bool emergency_case = engaged &&
            (queue_blocked || std::fabs(v.roadside_offset_m) > .01f);
        // The stop gate itself. Emergency plans may run right up to it; the
        // ordinary window stops a full car length short of the clearance zone.
        const float gate = lane.length_m - junction_clearance(lane.junction_to) - 1.0f;
        const float plan_to = emergency_case ? std::max(clear_to, gate) : clear_to;
        if (v.dist_along_m < clear_from ||
            (v.dist_along_m > clear_to && !emergency_case)) continue;
        const LanePose start{v.pos, v.fwd, {-v.fwd.z, 0, v.fwd.x}};
        auto submit = [&](TrafficManeuver& move) {
            if (!move.count || move.length_m < 1.0f ||
                !emergency_path_clear(i, move, 0, move.length_m, true,
                    move.kind == TrafficManeuverKind::PoliceRam)) return false;
            v.maneuver = move;
            // The arc starts at the actual post-impact pose, so its anchor
            // now owns that displacement. Adding it again would cause a pop.
            v.collision_offset_xz = {0,0};
            v.collision_yaw_rad = 0;
            emergency_frozen_[i].move = move;
            v.mode = AgentMode::Integrating;
            emergency_reservations_.push_back(i);
            return true;
        };
        auto shift = [&](TrafficManeuverKind kind, float forward, float offset, float speed) {
            TrafficManeuver move;
            move.kind = kind; move.destination = v.lane;
            move.end_station_m = v.dist_along_m + forward;
            move.end_offset_m = offset; move.speed_limit_mps = speed;
            if (move.end_station_m > plan_to) return false;
            // Leave enough road to merge back later. Parking right against
            // the next junction gate would strand this driver on the kerb.
            if (kind == TrafficManeuverKind::PullAside &&
                move.end_station_m + 14.0f > clear_to) return false;
            append(move, start, graph_->pose(v.lane, move.end_station_m, offset));
            return submit(move);
        };
        if (std::fabs(v.roadside_offset_m) > 0.01f) {
            if (v.emergency_yield == EmergencyYield::None &&
                (!v.police_pursuit || v.maneuver.kind != TrafficManeuverKind::PoliceRoadside ||
                 police_target_speed_mps_ > 2.0f || glm::distance(xz(v.pos), police_target_xz_) > 35.0f) &&
                (!v.police_unit || police_officer_driving_allowed(v.officer)))
                shift(TrafficManeuverKind::MergeBack,
                      engaged ? std::min(14.0f, std::max(4.0f, gate - v.dist_along_m))
                              : 14.0f,
                      0.0f, engaged ? 9.0f : 4.0f);
            continue;
        }
        if (v.emergency_yield != EmergencyYield::None) {
            const float width = traffic_vehicle_footprint(traffic_vehicle_kind(v)).half_width_m;
            const float offset = lane.width_m * .5f - lane.lateral_offset_m - width - .35f +
                (lane.sidewalks ? 0.0f : 1.1f);
            if (offset > 0.5f)
                shift(TrafficManeuverKind::PullAside, std::clamp(v.speed_mps * 1.5f, 8.0f, 16.0f),
                      offset, 3.5f);
            continue;
        }
        if (!v.police_pursuit && !v.snowplow_unit &&
            !(v.police_unit && !police_officer_driving_allowed(v.officer))) {
            plan_civilian_maneuver(i, player, start, clear_to, submit);
            continue;
        }
        if (!v.police_pursuit || !police_officer_driving_allowed(v.officer)) continue;

        // QUEUE JUMP — the move that was missing, and the reason a chase died
        // in traffic rather than on the road.
        //
        // PoliceBypass passes ONE stopped car mid-block: it needs 28 m of run
        // and has to finish before the junction clearance. A cruiser at the
        // tail of a queue at a red has neither — there is no 28 m, and every
        // metre it has left is inside the zone the bypass may not end in. So
        // the one manoeuvre that could free it was unplannable at the only
        // place it was ever needed, and the unit simply waited out the light
        // with its siren on.
        //
        // This runs the outside of the queue instead, and deliberately
        // finishes at the JUNCTION MOUTH at zero offset: ending beside the
        // queue would park the cruiser there (a non-zero roadside offset holds
        // a car at a standstill until it can merge, and it cannot merge inside
        // the zone), so the arc comes back to the centre in front of the queue,
        // pointed through the junction it is already allowed to run.
        if (police_wanted_level_ > 0 && i < leader_gap_.size() &&
            leader_gap_[i] < 34.0f && leader_speed_[i] < 3.0f &&
            v.speed_mps < 6.0f) {
            const float finish = std::min(lane.length_m - 0.5f,
                                          v.dist_along_m + 50.0f);
            if (finish > v.dist_along_m + 8.0f) {
                const float mid = std::min(v.dist_along_m + 8.0f, finish - 4.0f);
                // Oncoming side first on a two-way road: that is the side a
                // queue leaves free. The swept clearance test still checks it
                // for head-on traffic and still refuses to leave the tarmac.
                for (float offset : {-2.8f, -4.0f, 2.8f, 4.0f, -5.2f, 5.2f}) {
                    TrafficManeuver move;
                    move.kind = TrafficManeuverKind::PoliceBypass;
                    move.destination = v.lane;
                    move.end_station_m = finish;
                    move.end_offset_m = 0.0f;
                    move.speed_limit_mps = 9.0f;
                    const auto out = graph_->pose(v.lane, mid, offset);
                    append(move, start, out);
                    append(move, out, graph_->pose(v.lane, finish));
                    if (submit(move)) break;
                }
            }
            if (v.maneuver.active()) continue;
        }

        // The turnaround, the overtake and the ram all aim at the CENTRE —
        // live while in view, the last-seen point while searching.
        const glm::vec2 delta = police_search_centre_ - xz(v.pos);
        const float range = glm::length(delta), ahead = glm::dot(delta, xz(v.fwd));
        if (range > 100.0f) continue;

        // RAM. The chase's only deliberate contact, and the reason a pursuit
        // is something to shake rather than something to outwait at the next
        // junction. The arc leaves the lane centre for the target's OWN lateral
        // offset at his station and returns to centre beyond it, so a miss
        // rejoins traffic instead of parking the cruiser on the crown of the
        // road. The clearance test still vetoes kerbs, medians and every other
        // body — the player is the one obstacle it is allowed to ignore, and
        // only for this kind — so a ram can only be planned where the road
        // genuinely allows one.
        if (police_should_ram(police_wanted_level_ > 0 && !police_searching_,
                              police_target_on_foot_,
                              police_wanted_level_, ahead, range, v.speed_mps,
                              tuning_.police)) {
            // The cheap gate above runs first on purpose: this projection is
            // the only per-cop lane query here and most ticks refuse before it.
            const auto aim = graph_->project_onto(v.lane, police_target_xz_);
            const float contact = aim.dist_along_m;
            const float recover = std::min(contact + 14.0f, clear_to);
            const float room = lane.width_m * .5f - lane.lateral_offset_m - 1.2f;
            if (aim.valid() && contact > v.dist_along_m + 2.0f &&
                contact <= clear_to && recover > contact + 1.0f && room > 0.0f) {
                const float offset = std::clamp(aim.lateral_m, -room, room);
                TrafficManeuver move;
                move.kind = TrafficManeuverKind::PoliceRam;
                move.destination = v.lane;
                move.end_station_m = recover;
                move.end_offset_m = 0.0f;
                // Carry a closing speed into the hit — a ram planned at the
                // 4 m/s the other police arcs use is a nudge, not a threat —
                // but only as much as a pursuer may arrive with. This used to
                // keep the cruiser's own speed as well, so a unit already doing
                // 38 m/s rammed a suspect doing 10 at 28.
                move.speed_limit_mps = police_target_speed_mps_ +
                    tuning_.police.contact_closing_mps;
                const auto hit = graph_->pose(v.lane, contact, offset);
                append(move, start, hit);
                append(move, hit, graph_->pose(v.lane, recover));
                if (submit(move)) continue;
            }
        }
        if (ahead < -12.0f && lane.cls != RoadClass::Freeway && !lane.one_way &&
            v.speed_mps < 8.0f) {
            LaneRef opposite = graph_->opposing(v.lane);
            // Use the far lane on a wide road, giving the cruiser a real turn radius.
            if (!graph_->valid(opposite)) continue;
            for (LaneRef lr : graph_->lanes_of_edge(lane.edge))
                if (graph_->lane(lr).forward != lane.forward &&
                    graph_->lane(lr).index > graph_->lane(opposite).index) opposite = lr;
            if (graph_->valid(opposite)) {
                const auto end = graph_->project_onto(opposite, xz(v.pos));
                const auto finish = graph_->pose(opposite, end.dist_along_m);
                if (glm::distance(start.position, finish.position) >= 6.0f) {
                    TrafficManeuver move;
                    move.kind = TrafficManeuverKind::PoliceTurnaround;
                    move.destination = opposite; move.end_station_m = end.dist_along_m;
                    // A U-turn taken at 3.5 m/s took over ten seconds, and the
                    // suspect gained two hundred metres inside it. The swept
                    // clearance test is what keeps this safe, not the crawl.
                    move.speed_limit_mps = 7.0f;
                    append(move, start, finish);
                    if (submit(move)) continue;
                }
            }
        }
        // A complete out/pass/return corridor is checked before leaving the
        // lane, including oncoming motion. Never enter a median to pass.
        if (ahead > 15.0f && lane.cls != RoadClass::Freeway &&
            i < leader_gap_.size() && leader_gap_[i] < 32.0f &&
            leader_speed_[i] < 4.0f && v.speed_mps < 8.0f) {
            const float forward = police_target_speed_mps_ < 1.0f
                ? std::min(44.0f, ahead - 9.0f) : 44.0f;
            const float end = v.dist_along_m + forward;
            if (forward >= 28.0f && end <= clear_to) {
                for (float offset : {-1.35f, -2.4f, -3.4f, 1.35f, 2.4f, 3.4f}) {
                    TrafficManeuver move;
                    move.kind = TrafficManeuverKind::PoliceBypass;
                    move.destination = v.lane; move.end_station_m = end;
                    // AN OVERTAKE, NOT A PARKING MANOEUVRE. At the authored
                    // 5 m/s this 44 m arc took nine seconds, and a suspect
                    // doing 40 mph gained a hundred and twenty metres inside
                    // it — so the one move that exists to get a cruiser out of
                    // traffic was itself the biggest single loss of ground in
                    // the chase. The swept clearance re-check runs every step
                    // at the new speed and still stops the car dead if the
                    // corridor closes, so this buys pace, not recklessness.
                    // Scaled to the SUSPECT, with the authored 5 m/s kept as
                    // the floor: a bypass around a stalled car to reach a
                    // suspect who has already stopped must still end behind
                    // him, and 9 m/s overshot the stopping point.
                    move.speed_limit_mps = std::clamp(
                        police_target_speed_mps_ * 0.9f, 5.0f, 16.0f);
                    const float transition = std::min(13.0f, forward * .33f);
                    const auto out = graph_->pose(v.lane, v.dist_along_m + transition, offset);
                    const auto pass = graph_->pose(v.lane, end - transition, offset);
                    append(move, start, out); append(move, out, pass);
                    append(move, pass, graph_->pose(v.lane, end));
                    if (submit(move)) break;
                }
            }
        }
        if (v.maneuver.active()) continue;
        // Pull toward a stopped roadside suspect, leaving a car-length gap.
        const auto target = graph_->project_onto(v.lane, police_target_xz_);
        if (police_target_speed_mps_ < 1.0f && target.valid() && ahead > 16.0f &&
            ahead < 45.0f && target.lateral_m > 1.0f) {
            const float offset = std::min(target.lateral_m,
                lane.width_m*.5f - lane.lateral_offset_m - 1.5f);
            if (offset > .5f) shift(TrafficManeuverKind::PoliceRoadside,
                std::min(24.0f, ahead - 9.0f), offset, 4.0f);
        }
    }
}

bool Crowd::step_emergency_maneuver(uint32_t index, float dt) {
    auto& v = vehicles_[index];
    // A REJOIN ARC AND A DISMOUNT DEADLOCK EACH OTHER. An officer who is not
    // seated forces this maneuver's target speed to zero, while the phase
    // machine will not open his door until the car is out of a maneuver
    // (safe_road_position). Neither side can move, and the cruiser sits frozen
    // mid-arc with the officer stuck in Braking for the rest of the chase.
    // The arc is cosmetic; the arrest is not. Drop it.
    if (v.maneuver.kind == TrafficManeuverKind::MergeBack && v.police_unit &&
        !police_officer_driving_allowed(v.officer)) {
        v.maneuver = {};
        v.roadside_offset_m = 0.0f;
        return false;
    }
    if (v.maneuver.count == 0 && std::fabs(v.roadside_offset_m) < .01f) return false;
    v.mode = AgentMode::Integrating;
    if (v.maneuver.count == 0) { v.speed_mps = 0; v.maneuver_steer_rad = 0; return true; }
    auto& move = v.maneuver;
    float target = move.speed_limit_mps;
    if (move.kind == TrafficManeuverKind::PullAside || move.kind == TrafficManeuverKind::PoliceRoadside)
        target = std::min(target, std::sqrt(std::max(0.f, 2.f * v.profile.brake *
            (move.length_m - move.progress_m))));
    const float braking = v.speed_mps*v.speed_mps / (2.f*std::max(1.f, v.profile.brake));
    const bool ram = move.kind == TrafficManeuverKind::PoliceRam;
    // The arc's limit is one flat number and the car only brakes toward it, so
    // a ram planned fast could still land fast. Plan the last stretch on the
    // same contact cap the lane path uses.
    if (ram && !police_searching_) {
        const glm::vec2 to_suspect = police_target_xz_ - xz(v.pos);
        const float suspect_ahead = glm::dot(to_suspect, xz(v.fwd));
        if (suspect_ahead > 0.0f)
            target = std::min(target, police_contact_speed_mps(
                glm::dot(police_target_velocity_, xz(v.fwd)),
                suspect_ahead - tuning_.car_length_m, tuning_.police));
    }
    if (!emergency_path_clear(index, move, move.progress_m,
            std::max(2.0f, braking + 1.0f), false, ram)) target = 0;
    if (vehicle_engine_failed(v.mechanical) ||
        (v.police_unit && !police_officer_driving_allowed(v.officer))) target = 0;
    const float rate = target > v.speed_mps ? std::max(1.f, v.profile.accel) : std::max(1.f, v.profile.brake);
    v.speed_mps = std::max(0.f, v.speed_mps + std::clamp(target-v.speed_mps, -rate*dt, rate*dt));
    float travel = std::min(move.length_m - move.progress_m, v.speed_mps * dt);
    if (!emergency_path_clear(index, move, move.progress_m, travel, false, ram)) {
        travel = 0; v.speed_mps = 0;
    }
    // AN ARC THAT CANNOT ADVANCE IS ABANDONED. See maneuver_stall_steps: a car
    // on a maneuver has left the follow law, the recovery ladder and the
    // intersection escape behind, so a permanently vetoed arc is a permanent
    // wedge with no clock left running. The same argument the MergeBack
    // dismount deadlock above makes, generalised — and the snap back to the
    // lane that dropping the arc costs is what that case already pays.
    if (travel < 1e-4f && v.speed_mps < 0.05f) {
        if (++v.maneuver_stall_steps > kManeuverStallLimitSteps) {
            v.maneuver = {};
            v.maneuver_steer_rad = 0;
            v.maneuver_stall_steps = 0;
            v.roadside_offset_m = 0.0f;
            ++stats_.maneuvers_abandoned;
            return false;
        }
    } else {
        v.maneuver_stall_steps = 0;
    }
    move.progress_m += travel;
    const auto pose = traffic_maneuver_pose(move, move.progress_m);
    const auto ahead = traffic_maneuver_pose(move, std::min(move.length_m, move.progress_m + 1.8f));
    v.maneuver_steer_rad = std::clamp(std::atan2(
        pose.tangent.z*ahead.tangent.x - pose.tangent.x*ahead.tangent.z,
        glm::dot(pose.tangent, ahead.tangent)), -.55f, .55f);
    v.pos = pose.position + glm::vec3{v.collision_offset_xz.x, 0, v.collision_offset_xz.y};
    v.fwd = pose.tangent;
    const auto station = graph_->project_onto(v.lane, xz(pose.position));
    if (station.valid()) v.dist_along_m = v.last_dist_m = station.dist_along_m;
    if (move.progress_m >= move.length_m - .001f) {
        const bool rehomed = move.destination != v.lane;
        v.lane = move.destination; v.dist_along_m = v.last_dist_m = move.end_station_m;
        v.roadside_offset_m = move.end_offset_m;
        if (rehomed) {
            // The junction memory belonged to the old lane. A stop dwell
            // served there, a blocked-exit clock, a committed movement — none
            // of it describes the lane the car is now on.
            v.stop_junction = 0xFFFFFFFFu; v.stop_wait_steps = 0;
            v.stop_arrival_step = -1; v.stop_completed = false;
            v.blocked_exit_steps = 0;
            v.committed_junction = 0xFFFFFFFFu;
            v.committed_approach_lane = v.committed_exit_lane = kInvalidLane;
            v.turn_from_lane = kInvalidLane;
            if (graph_->valid(v.lane))
                v.cruise_mps = std::min(v.cruise_mps, graph_->lane(v.lane).speed_limit_mps);
        }
        const auto kind = move.kind;
        move = {}; move.kind = std::fabs(v.roadside_offset_m) > .01f ? kind : TrafficManeuverKind::None;
        v.maneuver_steer_rad = 0;
        if (std::fabs(v.roadside_offset_m) > .01f) v.speed_mps = 0;
        v.police_route.clear(); v.police_route_index = 0; v.police_last_replan_step = -1;
    }
    return true;
}

float Crowd::emergency_obstacle_speed(uint32_t index) const {
    if (emergency_frozen_.empty()) return std::numeric_limits<float>::infinity();
    const auto& ego = emergency_frozen_[index];
    float limit = std::numeric_limits<float>::infinity();
    const LanePose pose{ego.pos, ego.fwd, {-ego.fwd.z, 0, ego.fwd.x}};
    // Lane buckets cannot represent a vehicle crossing the centre line or
    // merging from the shoulder. Guard ordinary drivers with actual poses.
    for (uint32_t i = 0; i < emergency_frozen_.size(); ++i) {
        const auto& other = emergency_frozen_[i];
        if (i == index || !other.move.active() || std::fabs(ego.pos.y-other.pos.y)>2.2f) continue;
        for (float t : {0.0f, 0.6f, 1.2f}) {
            const auto p = traffic_maneuver_pose(other.move, other.move.progress_m + other.speed*t);
            const glm::vec3 delta = p.position - ego.pos;
            const float along = glm::dot(delta, ego.fwd);
            const float side = std::fabs(glm::dot(delta, pose.right));
            if (along < -ego.half_length || side > ego.half_width + other.half_length + .5f) continue;
            LanePose predicted = pose;
            predicted.position += ego.fwd * std::max(0.f, along);
            if (!footprints_overlap(predicted, ego.half_width+.35f, ego.half_length,
                    p.position, p.tangent, other.half_width, other.half_length)) continue;
            const float room = std::max(0.f, along - ego.half_length - other.half_length - 1.f);
            limit = std::min(limit, std::min(room*.6f,
                std::sqrt(2.f*std::max(1.f, vehicles_[index].profile.brake)*room)));
        }
    }
    return limit;
}

// ---------------------------------------------------------------------------
// Civilian lateral moves (PENG-47).
// ---------------------------------------------------------------------------

bool Crowd::civilian_plan_candidate(uint32_t index) const {
    const VehicleAgent& v = vehicles_[index];
    return !v.chase_active && !v.police_pursuit && !v.snowplow_unit &&
           (v.obstruction_wait_s > 0.0f || v.jam_steps > 0);
}

float Crowd::lateral_arc_run(uint32_t index, float preferred_run,
                             float run_min, float run_max) const {
    const VehicleAgent& v = vehicles_[index];
    if (!graph_ || !graph_->valid(v.lane)) return preferred_run;
    const CivilianManeuverTuning& civ = tuning_.civilian;
    const float standoff = civ.standoff_m;
    const float my_half_w =
        traffic_vehicle_footprint(traffic_vehicle_kind(v)).half_width_m;
    // Scenery on MY lane, inside the window a run could end in. Only bodies
    // that reach into the driving corridor matter: an arc is free to finish
    // alongside a kerb-clear car, because driving past one is what the lane is
    // for. One projection per body, then the candidate runs are tested against
    // the short list — the other way round is forty projections per candidate.
    std::vector<float> blockers;
    const float reach = run_max + standoff + 2.0f * tuning_.traffic_half_length_m;
    for (const EmergencyBody& b : emergency_obstacles_) {
        if (!b.scenery) continue;
        if (std::isfinite(b.pos.y) && std::fabs(b.pos.y - v.pos.y) > 2.5f) continue;
        const LaneProjection at = graph_->project_onto(v.lane, xz(b.pos));
        if (!at.valid()) continue;
        const float ahead = at.dist_along_m - v.dist_along_m;
        if (ahead < -standoff || ahead > reach) continue;
        if (std::fabs(at.lateral_m) >=
            my_half_w + b.half_width + civ.parked_corridor_margin_m) continue;
        blockers.push_back(at.dist_along_m);
    }
    if (blockers.empty()) return preferred_run;
    auto fits = [&](float run) {
        const float end = v.dist_along_m + run;
        for (float s : blockers)
            if (std::fabs(end - s) < standoff) return false;
        return true;
    };
    if (fits(preferred_run)) return preferred_run;
    // Longer first — finishing PAST a body beats stopping short of it — then
    // shorter. Half a metre is finer than the 0.85 m the clearance sweep
    // samples at, so no fitting end station is stepped over.
    for (float d = 0.5f; d <= run_max - run_min + 0.5f; d += 0.5f) {
        if (preferred_run + d <= run_max && fits(preferred_run + d))
            return preferred_run + d;
        if (preferred_run - d >= run_min && fits(preferred_run - d))
            return preferred_run - d;
    }
    // NOTHING FITS — TAKE THE PREFERRED RUN ANYWAY. Refusing to plan looks
    // like the careful choice and is the opposite: a kerb dense enough that no
    // end station clears every body is exactly the kerb a car most needs to
    // get off, and declining leaves it stopped there for good. Measured, that
    // refusal WAS the wedge: two box trucks on the real map sat 28 s apiece
    // with a queue backing through a junction behind them, and every one of
    // their nudges had been declined here. A merge-back that lands close is
    // recoverable — the driver re-plans from it — and the clearance sweep
    // still has to pass whatever this returns.
    return preferred_run;
}

void Crowd::gather_parked_near(LaneRef lane, glm::vec3 pos, float radius_m,
                               std::vector<uint32_t>& out) const {
    if (!graph_ || !graph_->valid(lane) || ambient_parked_.empty()) return;
    const float r2 = radius_m * radius_m;
    const LaneRef candidates[4] = {lane, graph_->neighbour(lane, +1),
                                   graph_->neighbour(lane, -1), graph_->opposing(lane)};
    for (LaneRef lr : candidates) {
        if (!graph_->valid(lr) || lr >= parked_bays_.size() || parked_bays_[lr].slots == 0)
            continue;
        const uint64_t key = graph_->lane(lr).key;
        // ambient_parked_ is sorted on (lane_key, slot): one lane is one run.
        auto first = std::lower_bound(ambient_parked_.begin(), ambient_parked_.end(), key,
            [](const AmbientParkedCar& c, uint64_t k) { return c.lane_key < k; });
        for (; first != ambient_parked_.end() && first->lane_key == key; ++first) {
            const float dx = first->pos.x - pos.x, dz = first->pos.z - pos.z;
            if (dx * dx + dz * dz > r2) continue;
            out.push_back(static_cast<uint32_t>(first - ambient_parked_.begin()));
        }
    }
}

Crowd::ObstructionView Crowd::classify_obstruction(uint32_t index,
                                                   const VehicleState* player) const {
    ObstructionView out;
    const VehicleAgent& v = vehicles_[index];
    if (!graph_ || !graph_->valid(v.lane)) return out;
    const CivilianManeuverTuning& civ = tuning_.civilian;
    const float car_length = tuning_.car_length_m;
    const float half_length = tuning_.traffic_half_length_m;
    const float corridor = tuning_.traffic_half_width_m * 2.0f + 0.35f;

    // The AI leader, from the frozen buckets.
    if (index < leader_index_.size() && leader_index_[index] != 0xFFFFFFFFu &&
        std::isfinite(leader_gap_[index])) {
        const uint32_t j = leader_index_[index];
        out.present = true;
        out.ai = true;
        out.index = j;
        out.gap_m = leader_gap_[index] - car_length;
        out.speed_mps = std::max(0.0f, leader_speed_[index]);
        out.pos_xz = xz(vehicles_[j].pos);
        out.half_width_m = traffic_vehicle_footprint(traffic_vehicle_kind(vehicles_[j])).half_width_m;
        const JunctionSnapshot& lead = junction_frozen_[j];
        const bool stopped = out.speed_mps < civ.stationary_mps;
        // Is the leader at, or queued up to, a control? Its own stop memory,
        // a committed movement, or simply being inside the approach window
        // of a signal, stop or yield all say "queue", and a queue is never
        // passed — the car at its head is waiting for a light, not stuck.
        bool queued = lead.stop_junction != 0xFFFFFFFFu ||
                      lead.committed_junction != 0xFFFFFFFFu || lead.active_turn;
        if (!queued && graph_->valid(lead.lane)) {
            const Lane& lane = graph_->lane(lead.lane);
            const float slack = lane.length_m -
                junction_clearance(lane.junction_to) - lead.dist_along_m;
            queued = lane.approach_control != JunctionControl::None &&
                     slack < civ.queue_window_m;
        }
        out.queue = queued && !lead.engine_failed;
        // Stationary and passable: a dead engine is stuck immediately; a
        // healthy car stopped mid-block on open road for a while is stuck
        // behind something we cannot see from here.
        out.stationary = stopped &&
            (lead.engine_failed ||
             (!queued && lead.delay_seconds >= civ.leader_stalled_s));
    }

    // Bodies that are not in any lane bucket: the player's car and cars the
    // player left. Projected onto this lane; only what is AHEAD and inside
    // the driving corridor counts.
    const glm::vec2 fwd_xz{v.fwd.x, v.fwd.z};
    const float reach = civ.trigger_gap_m + car_length + 4.0f;
    auto consider = [&](glm::vec3 pos, glm::vec2 vel_xz, bool is_player, float half_w) {
        if (std::isfinite(pos.y) && std::fabs(pos.y - v.pos.y) > 2.5f) return;
        const float dx = pos.x - v.pos.x, dz = pos.z - v.pos.z;
        if (dx * dx + dz * dz > reach * reach) return;
        const LaneProjection proj = graph_->project_onto(v.lane, {pos.x, pos.z});
        if (!proj.valid()) return;
        const float ahead = proj.dist_along_m - v.dist_along_m;
        if (ahead <= half_length || std::fabs(proj.lateral_m) >= corridor) return;
        const float gap = ahead - car_length;
        if (gap >= out.gap_m) return;
        out.present = true;
        out.ai = false;
        out.queue = false;
        out.player = is_player;
        out.index = 0xFFFFFFFFu;
        out.gap_m = gap;
        out.speed_mps = std::max(0.0f, glm::dot(vel_xz, fwd_xz));
        out.stationary = out.speed_mps < civ.stationary_mps;
        out.pos_xz = {pos.x, pos.z};
        out.half_width_m = half_w;
    };
    if (player) consider(player->position, {player->velocity.x, player->velocity.z},
                         true, kPlayerHalfWidthM);
    for (const glm::vec3& p : parked_vehicle_positions_) consider(p, {0.0f, 0.0f}, false, 2.5f);
    // Ambient parked cars, with their real footprints: a body is an
    // obstruction only when it reaches into MY corridor by more than the
    // margin. A kerb-clear bay on a Street sits 2.25 m out against a 2.13 m
    // combined half-width, and is driven past at cruise.
    if (civ.ambient_parked_obstacles && !ambient_parked_.empty()) {
        const float my_half_w = traffic_vehicle_footprint(traffic_vehicle_kind(v)).half_width_m;
        std::vector<uint32_t> near;
        gather_parked_near(v.lane, v.pos, civ.parked_hazard_range_m, near);
        for (uint32_t parked_index : near) {
            const AmbientParkedCar& car = ambient_parked_[parked_index];
            if (std::fabs(car.pos.y - v.pos.y) > 2.5f) continue;
            const LaneProjection proj = graph_->project_onto(v.lane, {car.pos.x, car.pos.z});
            if (!proj.valid()) continue;
            const float ahead = proj.dist_along_m - v.dist_along_m;
            if (ahead <= half_length) continue;
            const auto fp = traffic_vehicle_footprint(car.kind);
            if (std::fabs(proj.lateral_m) >=
                my_half_w + fp.half_width_m + civ.parked_corridor_margin_m) continue;
            const float gap = ahead - half_length - fp.half_length_m;
            if (gap >= out.gap_m) continue;
            out.present = true;
            out.ai = false;
            out.queue = false;
            out.player = false;
            out.index = 0xFFFFFFFFu;
            out.gap_m = gap;
            out.speed_mps = 0.0f;
            out.stationary = true;
            out.pos_xz = {car.pos.x, car.pos.z};
            out.half_width_m = fp.half_width_m;
        }
    }
    // Whatever it is, if it sits inside this lane's own approach window to a
    // control it is part of the queue at that control — the player stopped
    // at a red is not a wreck to be passed.
    if (out.present && !out.ai) {
        const Lane& mine = graph_->lane(v.lane);
        const float slack = mine.length_m - junction_clearance(mine.junction_to) -
                            (v.dist_along_m + out.gap_m + car_length);
        if (mine.approach_control != JunctionControl::None && slack < civ.queue_window_m) {
            out.queue = true;
            out.stationary = false;
        }
    }
    return out;
}

OvertakeLaneView Crowd::opposing_lane_view(uint32_t index, LaneRef opposing,
                                           float pass_speed_mps) const {
    OvertakeLaneView view;
    const VehicleAgent& v = vehicles_[index];
    if (!graph_->valid(opposing) || opposing >= lane_buckets_.size()) return view;
    const LaneProjection mine = graph_->project_onto(opposing, xz(v.pos));
    if (!mine.valid()) return view;
    // The opposing lane runs the other way: a car at a smaller station than
    // mine is still coming toward me; one at a larger station has passed.
    const float s = mine.dist_along_m;
    const float car_length = tuning_.car_length_m;
    for (const BucketEntry& e : lane_buckets_[opposing]) {
        if (e.agent == index) continue;
        const VehicleAgent& o = vehicles_[e.agent];
        if (e.dist < s) {
            const float gap = s - e.dist - car_length;
            if (gap < view.front_gap) {
                view.front_gap = gap;
                view.front_closing = pass_speed_mps + std::max(0.0f, o.speed_mps);
            }
        } else {
            const float gap = e.dist - s - car_length;
            if (gap < view.rear_gap) {
                view.rear_gap = gap;
                view.rear_closing = 0.0f;
            }
        }
    }
    return view;
}

OvertakeLaneView Crowd::neighbour_lane_view(uint32_t index, LaneRef target,
                                            float& station_on_target) const {
    OvertakeLaneView view;
    station_on_target = 0.0f;
    const VehicleAgent& v = vehicles_[index];
    if (!graph_->valid(target) || target >= lane_buckets_.size()) return view;
    const LaneProjection mine = graph_->project_onto(target, xz(v.pos));
    if (!mine.valid()) return view;
    station_on_target = mine.dist_along_m;
    const float car_length = tuning_.car_length_m;
    for (const BucketEntry& e : lane_buckets_[target]) {
        if (e.agent == index) continue;
        const VehicleAgent& o = vehicles_[e.agent];
        if (e.dist > station_on_target) {
            const float gap = e.dist - station_on_target - car_length;
            if (gap < view.front_gap) {
                view.front_gap = gap;
                view.front_closing = std::max(0.0f, v.speed_mps - o.speed_mps);
            }
        } else {
            const float gap = station_on_target - e.dist - car_length;
            if (gap < view.rear_gap) {
                view.rear_gap = gap;
                view.rear_closing = std::max(0.0f, o.speed_mps - v.speed_mps);
            }
        }
    }
    return view;
}

void Crowd::plan_civilian_maneuver(uint32_t index, const VehicleState* player,
                                   const LanePose& start, float clear_to,
                                   const std::function<bool(TrafficManeuver&)>& submit) {
    VehicleAgent& v = vehicles_[index];
    if (v.obstruction_wait_s <= 0.0f && v.jam_steps <= 0) {
        v.recovery_action = RecoveryAction::None;
        return;
    }
    const ObstructionView ob = classify_obstruction(index, player);
    if (!ob.present || ob.queue) { v.recovery_action = RecoveryAction::None; return; }
    if (plan_civilian_pass(index, ob, start, clear_to, submit)) {
        v.recovery_action = RecoveryAction::None;
        return;
    }
    plan_civilian_recovery(index, ob, player, start, clear_to, submit);
}

bool Crowd::plan_civilian_pass(uint32_t index, const ObstructionView& ob,
                               const LanePose& start, float clear_to,
                               const std::function<bool(TrafficManeuver&)>& submit) {
    VehicleAgent& v = vehicles_[index];
    const CivilianManeuverTuning& civ = tuning_.civilian;
    if (v.obstruction_wait_s <= 0.0f) return false;
    const Lane& lane = graph_->lane(v.lane);
    // A keyed hesitation, fresh per decision: a row of identical drivers
    // behind one wreck does not pull out on the same step.
    Rng roll{phantom_key(map_seed_, v.lane_key, v.slot,
                         kChannelOvertakeHesitation ^
                             (v.maneuver_decisions * 0x85EBCA6Bu))};
    const float waited = traffic_pass_wait_credit(
        v.obstruction_wait_s - civ.hesitation_max_s * roll.next_float(),
        v.impact_caution_s);
    const float half_length = traffic_vehicle_footprint(traffic_vehicle_kind(v)).half_length_m;
    const bool taper = lane.lanes_at_start != lane.lanes_at_end;

    // (b) A lane change on a multi-lane road. Outboard first: the fixed try
    // order is the keep-right bias, and it is deterministic without a roll.
    if (!taper && v.lane_change_cooldown_s <= 0.0f &&
        waited >= traffic_lane_change_wait(civ.lane_change_wait_s, v.profile)) {
        // The speed a lane will let me SUSTAIN: my cruise with nobody ahead,
        // otherwise no more than the leader's own speed (a follow law says
        // what I may do this second; over the run it is the leader that sets
        // the pace), and never past the gap's own follow speed.
        auto sustain = [&](float gap, float lead_speed) {
            if (!std::isfinite(gap)) return v.cruise_mps;
            return std::min({v.cruise_mps, std::max(0.0f, lead_speed),
                             traffic_follow_speed_for_gap(gap, v.profile)});
        };
        for (int delta : {+1, -1}) {
            const LaneRef target = graph_->neighbour(v.lane, delta);
            if (!graph_->valid(target)) continue;
            const Lane& tl = graph_->lane(target);
            if (tl.lanes_at_start != tl.lanes_at_end) continue;
            float station = 0.0f;
            const OvertakeLaneView view = neighbour_lane_view(index, target, station);
            const float run = std::clamp(tuning_.overtake.shift_seconds * v.speed_mps,
                                         civ.lane_change_run_min_m, civ.lane_change_run_max_m);
            const float end = station + run;
            if (end > tl.length_m - junction_clearance(tl.junction_to) - 4.0f) continue;
            // Safety: the kernel's rear TTC term is the MOBIL new-follower
            // criterion; the front terms cover the whole run.
            if (!overtake_gap_acceptable(view, run, tuning_.overtake)) continue;
            // Incentive: would I actually be driving faster over there?
            float lead_speed = 0.0f;
            if (std::isfinite(view.front_gap))
                lead_speed = std::max(0.0f, v.speed_mps - view.front_closing);
            const float gain = sustain(view.front_gap, lead_speed) -
                               sustain(ob.gap_m, ob.speed_mps);
            const float caution = std::clamp(v.impact_caution_s / 12.0f, 0.0f, 1.0f);
            if (gain <= traffic_lane_change_gain(civ.lane_change_gain_mps,
                                                 v.profile) + caution) continue;
            const float speed = std::max(6.0f, std::min(v.cruise_mps, tl.speed_limit_mps));
            TrafficManeuver move = lane_shift_arc(*graph_, start, target, end, speed);
            if (submit(move)) {
                v.lane_change_cooldown_s = civ.lane_change_cooldown_s;
                v.obstruction_wait_s = 0.0f;
                ++v.maneuver_decisions;
                ++stats_.lane_changes;
                return true;
            }
        }
    }

    // (a) A single-lane overtake: borrow the oncoming lane around something
    // STATIONARY. Never on a one-way, a freeway, a taper, or where a
    // same-direction neighbour exists (that is a lane change, above), and
    // only for a driver whose patience has run out — never a Cautious one.
    if (taper || lane.one_way || lane.cls == RoadClass::Freeway || !ob.stationary) return false;
    if (graph_->valid(graph_->neighbour(v.lane, +1)) ||
        graph_->valid(graph_->neighbour(v.lane, -1))) return false;
    const LaneRef opposing = graph_->opposing(v.lane);
    if (!graph_->valid(opposing)) return false;
    if (!traffic_profile_may_pass_jam(v.profile, waited)) return false;
    // The return curve is the last third of the run and must not begin
    // until the body is past the obstruction: gap, its length, my length
    // and a margin, all divided by the two thirds that are out-and-past.
    const float run = lateral_arc_run(index,
        std::clamp(1.5f * (ob.gap_m + 2.0f * half_length + 5.0f + 2.0f),
                   civ.overtake_run_min_m, civ.overtake_run_max_m),
        civ.overtake_run_min_m, civ.overtake_run_max_m);
    if (run <= 0.0f) return false;
    if (v.dist_along_m + run > clear_to) return false;
    const float pass_speed = std::clamp(
        0.9f * std::min(v.cruise_mps, lane.speed_limit_mps), 5.0f, 16.0f);
    if (!overtake_gap_acceptable(opposing_lane_view(index, opposing, pass_speed),
                                 run, tuning_.overtake)) return false;
    // Toward the road centreline, whichever side of travel that is.
    const float side = lane.lateral_offset_m >= 0.0f ? -1.0f : 1.0f;
    for (float offset : {1.35f, 2.4f, 3.4f, 4.5f}) {
        TrafficManeuver move = lateral_pass_arc(*graph_, v.lane, start, v.dist_along_m,
            run, side * offset, pass_speed, TrafficManeuverKind::CivilianOvertake);
        if (submit(move)) {
            v.obstruction_wait_s = 0.0f;
            ++v.maneuver_decisions;
            ++stats_.civilian_overtakes;
            return true;
        }
    }
    return false;
}

// THE LADDER (PENG-50). A car the pass could not free — the oncoming lane is
// never clear, the road has no neighbour lane — has three honest options in
// this tranche: edge round the obstruction on its own half of the road
// (Nudge), keep waiting, or, far from the player and only after the whole
// ladder has run dry, stop existing (GiveUp -> retire). Reverse and the
// three-point turn are structurally ineligible here (rear_gap 0, one-way):
// a lane agent has no drivable reverse, and the kernels that would drive one
// emit body controls only a free-driving cruiser has. Filling those two
// fields later re-enables them without touching the kernel.
void Crowd::plan_civilian_recovery(uint32_t index, const ObstructionView& ob,
                                   const VehicleState* player, const LanePose& start,
                                   float clear_to,
                                   const std::function<bool(TrafficManeuver&)>& submit) {
    VehicleAgent& v = vehicles_[index];
    const CivilianManeuverTuning& civ = tuning_.civilian;
    if (v.jam_steps <= 0 ||
        static_cast<float>(v.jam_steps) * static_cast<float>(kSimDt) < v.profile.patience_seconds) {
        v.recovery_action = RecoveryAction::None;
        return;
    }
    const Lane& lane = graph_->lane(v.lane);
    const auto my = traffic_vehicle_footprint(traffic_vehicle_kind(v));
    RecoveryView rv;
    rv.blocker = !ob.stationary ? RecoveryBlocker::AiJam
               : ob.player      ? RecoveryBlocker::Player
               : ob.ai          ? (junction_frozen_[ob.index].engine_failed
                                       ? RecoveryBlocker::StaticWreck
                                       : RecoveryBlocker::AiJam)
                                : RecoveryBlocker::StaticWreck;
    rv.front_gap = ob.gap_m;
    rv.rear_gap = 0.0f;
    rv.road_bidirectional = false;
    rv.oncoming_clear = false;
    rv.reroute_available = false;
    rv.dist_from_player = player
        ? glm::distance(xz(v.pos), glm::vec2{player->position.x, player->position.z})
        : -1.0f;
    // Where the obstruction sits across the lane, and what is left beside it
    // on OUR half of the road. mid is the lane centre's distance from the
    // road centreline: the band the nudge may use runs from there to the
    // kerb, minus the body.
    const LaneProjection at = graph_->project_onto(v.lane, ob.pos_xz);
    const float mid = std::fabs(lane.lateral_offset_m);
    // 0.45 m of margin: the clearance sweep pads the body by 0.28 m, and a
    // corridor fitted to less than that is one the sweep vetoes.
    const NudgePlan nudge = at.valid()
        ? nudge_pick_target(at.lateral_m, ob.half_width_m, my.half_width_m, 0.45f, mid)
        : NudgePlan{};
    rv.shoulder_clear = nudge.viable ? nudge.corridor : 0.0f;

    const float elapsed = static_cast<float>(v.jam_steps - v.recovery_action_start_steps) * static_cast<float>(kSimDt);
    const RecoveryAction next = recovery_plan(rv, v.recovery_action, elapsed, tuning_.recovery);
    if (next != v.recovery_action) {
        v.recovery_action = next;
        v.recovery_action_start_steps = v.jam_steps;
    }
    switch (next) {
        case RecoveryAction::Nudge: {
            // Same shape as the pass: the return must start past the blocker.
            // THE ROAD THAT IS ACTUALLY LEFT, not the road a textbook nudge
            // would like. This wants 16 m and a lane may simply not have it:
            // measured on the real map, a box truck blocked 63.2 m into a
            // 92 m lane had 11.5 m before the junction gate, refused to plan
            // on that basis, and then sat there for the rest of the run a
            // metre from a parked bumper — with a queue behind it backing up
            // through the junction it had already committed to. A short arc
            // still clears the body, and it still has to pass the sweep.
            // Below kNudgeRunFloorM there is no arc worth the name.
            const float room = clear_to - v.dist_along_m;
            const float longest = std::min(36.0f, room);
            if (longest < kNudgeRunFloorM) return;
            const float run = lateral_arc_run(index,
                std::min(longest,
                         std::clamp(1.5f * (ob.gap_m + 2.0f * my.half_length_m +
                                            5.0f + 2.0f), 16.0f, 36.0f)),
                std::min(kNudgeRunFloorM, longest), longest);
            if (run <= 0.0f) return;
            TrafficManeuver move = lateral_pass_arc(*graph_, v.lane, start, v.dist_along_m,
                run, nudge.target, 4.0f, TrafficManeuverKind::Nudge);
            if (submit(move)) {
                ++v.maneuver_decisions;
                ++stats_.nudges;
                v.recovery_action = RecoveryAction::None;
            }
            return;
        }
        case RecoveryAction::GiveUp: {
            const bool dynamic_blocker = rv.blocker == RecoveryBlocker::StaticWreck ||
                                         rv.blocker == RecoveryBlocker::Player;
            if (traffic_should_despawn_jam(true, static_cast<float>(v.jam_steps) * static_cast<float>(kSimDt),
                                           civ.stuck_despawn_s, dynamic_blocker,
                                           tuning_.recovery.legacy_instant_despawn,
                                           /*maneuvers_exhausted=*/true,
                                           rv.dist_from_player,
                                           tuning_.recovery.giveup_min_player_dist))
                v.jam_retire = true;
            return;
        }
        default:
            return;
    }
}
} // namespace apricot
