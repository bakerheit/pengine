#include "traffic/crowd.h"

#include <algorithm>
#include <cmath>
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

void append(TrafficManeuver& m, const LanePose& a, const LanePose& b) {
    auto& curve = m.curves[m.count++];
    curve = traffic_steering_curve(a, b);
    m.length_m += curve.length_m;
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
    const float width = footprint.half_width_m + 0.28f;
    const float length = footprint.half_length_m + 0.45f;
    const float end = std::min(move.length_m, from + distance);
    const int samples = std::max(1, int(std::ceil((end - from) / 0.85f)));
    for (int s = 0; s <= samples; ++s) {
        const float d = from + (end - from) * float(s) / float(samples);
        const LanePose pose = traffic_maneuver_pose(move, d);
        const float seconds = (d - from) / std::max(2.0f, move.speed_limit_mps);
        auto blocks = [&](const EmergencyBody& other, bool predict) {
            // Check the current footprint too: assuming that a lead will keep
            // moving is how a maneuver gets admitted into a stopped queue.
            if (footprints_overlap(pose, width, length, other.pos, other.fwd,
                                   other.half_width, other.half_length)) return true;
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
        for (const auto& box : police_officer_world_->static_boxes()) {
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
                  v.emergency_yield != EmergencyYield::None;
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
        emergency_obstacles_.push_back({p, {1,0,0}, 0, 2.5f, 2.5f, {}});
    emergency_player_obstacle_ = static_cast<std::size_t>(-1);
    if (player) {
        emergency_player_obstacle_ = emergency_obstacles_.size();
        emergency_obstacles_.push_back({player->position, vehicle_forward(*player),
            std::max(0.f, vehicle_forward_speed(*player)), kPlayerHalfWidthM, kPlayerHalfLengthM, {}});
    }
    if (on_foot) emergency_obstacles_.push_back({
        {on_foot->position.x, on_foot->height_m, on_foot->position.y}, {1,0,0}, 0, .6f, .6f, {}});

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

        const glm::vec2 delta = police_target_xz_ - xz(v.pos);
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
        if (police_should_ram(police_wanted_level_ > 0, police_target_on_foot_,
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
                // Carry the closing speed into the hit. A ram planned at the
                // 4 m/s the other police arcs use is a nudge, not a threat.
                move.speed_limit_mps = std::max(v.speed_mps,
                    police_target_speed_mps_ + 4.0f);
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
        v.lane = move.destination; v.dist_along_m = v.last_dist_m = move.end_station_m;
        v.roadside_offset_m = move.end_offset_m;
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
} // namespace apricot
