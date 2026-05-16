// traffic_drive.cpp — per-frame AI driving + police pursuit for TrafficSystem.
//
// Owns: ai_route_valid, ai_extend_route, ai_safe_to_shift, ai_update_speed,
// ai_advance, update_ai_kinematic, try_ai_recover, update_police_dynamic.
// Extracted from traffic.cpp in PBD-007 commit 3.

#include "game/traffic.h"
#include "game/traffic_internal.h"

#include "game/car_models.h"
#include "game/traffic_ai.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <glm/gtc/quaternion.hpp>

#include "world/city_layout.h"
#include "world/heightmap.h"
#include "world/road_graph.h"

namespace pengine {

namespace {

// Drive-only tuning constants.

// Blocked-car avoidance: cars stuck behind a dynamic blocker grow a blocked
// timer. Driver profiles decide when they honk or cautiously shift toward
// the opposing lane.
constexpr float STUCK_SPEED = 1.5f;
constexpr float STUCK_GAP   = 8.0f;
constexpr float SWERVE_RATE = 1.5f;

// At this distance into a lane, the car starts considering its next-lane
// hand-off (turn signals, route extension). Tuned so the signal flashes
// before the corner instead of mid-turn.
constexpr float ROUTE_REFRESH_AT = ROAD_PITCH - 12.f;

// Half-width of the lane corridor used for follow-leader and same-lane
// overlap tests. Slightly wider than the visible lane stripe so cars don't
// pop in and out of "in this lane" classification at boundaries.
constexpr float LANE_HALF_WIDTH = 2.0f;

// Nominal car length used by gap math. Real cars span a range, but the
// follow logic treats the leader's tail as `car_length` behind its origin.
constexpr float CAR_LENGTH = 4.0f;

} // namespace

bool TrafficSystem::ai_route_valid(const Car& c) const {
    if (!graph_) return false;
    TrafficLaneGraph lane_graph(*graph_);
    return lane_graph.lane_loaded(c.ai_lane);
}

void TrafficSystem::ai_extend_route(Car& c) {
    if (!graph_) return;
    TrafficLaneGraph lane_graph(*graph_);
    if (c.ai_route.lanes.empty()) {
        c.ai_route = lane_graph.make_route(c.ai_lane, ROUTE_LANES, rng_);
        c.ai_route_index = 0;
    }
    while (c.ai_route.lanes.size()
           < c.ai_route_index + static_cast<std::size_t>(ROUTE_LANES)) {
        LaneId from = c.ai_route.lanes.empty() ? c.ai_lane : c.ai_route.lanes.back();
        LaneId next = lane_graph.choose_next_lane(from, rng_);
        if (!lane_graph.lane_loaded(next)) break;
        c.ai_route.lanes.push_back(next);
    }
}

bool TrafficSystem::ai_safe_to_shift(const Car& c, float clear_dist) const {
    TrafficDirInfo info = traffic_dir_info(c.ai_lane.dir);
    glm::vec3 from = RoadGraph::intersection_pos(c.ai_lane.i, c.ai_lane.j, 0.f);
    glm::vec3 shift_origin = from - info.right * lane_offset_;

    for (const auto& op : cars_) {
        const Car& o = *op;
        if (&o == &c) continue;
        glm::vec3 op_pos = o.vehicle.position();

        auto in_window = [&](const glm::vec3& origin,
                             float min_along, float max_along) {
            glm::vec3 d = op_pos - origin;
            float along = glm::dot(d, info.unit);
            float lat   = glm::dot(d, info.right);
            return std::abs(lat) <= LANE_HALF_WIDTH
                && along >= min_along && along <= max_along;
        };

        if (in_window(shift_origin, c.ai_distance_along - 8.f,
                      std::min(c.ai_distance_along + clear_dist, ROAD_PITCH))) {
            return false;
        }
        float remaining = (c.ai_distance_along + clear_dist) - ROAD_PITCH;
        if (remaining > 0.f
            && in_window(shift_origin + info.unit * ROAD_PITCH, 0.f, remaining)) {
            return false;
        }
    }
    return true;
}

void TrafficSystem::ai_update_speed(Car& c, float dt, double time_seconds) {
    float target = c.ai_target_speed;
    float best_gap = std::numeric_limits<float>::infinity();
    bool leader_is_dynamic = false;
    bool leader_is_stopped_ai = false;
    {
        TrafficDirInfo info = traffic_dir_info(c.ai_lane.dir);
        glm::vec3 from = RoadGraph::intersection_pos(c.ai_lane.i, c.ai_lane.j, 0.f);
        glm::vec3 lane_origin = from + info.right
            * (lane_offset_ + c.ai_lateral_offset);

        for (const auto& op : cars_) {
            const Car& o = *op;
            if (&o == &c) continue;
            glm::vec3 op_pos = o.vehicle.position();

            glm::vec3 d   = op_pos - lane_origin;
            float    along = glm::dot(d, info.unit);
            float    lat   = glm::dot(d, info.right);
            if (std::abs(lat) <= LANE_HALF_WIDTH
                && along > c.ai_distance_along && along <= ROAD_PITCH) {
                float gap = along - c.ai_distance_along - CAR_LENGTH;
                if (gap < best_gap) {
                    best_gap = gap;
                    leader_is_dynamic = (o.driver != Driver::AI);
                    leader_is_stopped_ai = (o.driver == Driver::AI
                                            && o.ai_speed < STUCK_SPEED);
                }
                continue;
            }

            glm::vec3 d2   = op_pos - (lane_origin + info.unit * ROAD_PITCH);
            float    along2 = glm::dot(d2, info.unit);
            float    lat2   = glm::dot(d2, info.right);
            if (std::abs(lat2) <= LANE_HALF_WIDTH
                && along2 >= 0.f && along2 <= ROAD_PITCH) {
                float gap = (ROAD_PITCH - c.ai_distance_along)
                            + along2 - CAR_LENGTH;
                if (gap < best_gap) {
                    best_gap = gap;
                    leader_is_dynamic = (o.driver != Driver::AI);
                    leader_is_stopped_ai = (o.driver == Driver::AI
                                            && o.ai_speed < STUCK_SPEED);
                }
            }
        }
        if (std::isfinite(best_gap)) {
            target = std::min(target,
                traffic_follow_speed_for_gap(best_gap, c.ai_profile));
        }
    }

    bool blocked = std::isfinite(best_gap)
                && best_gap < STUCK_GAP
                && c.ai_speed < STUCK_SPEED;
    if (blocked) {
        c.ai_blocked_timer += dt;
        c.ai_honk_timer += dt;
    } else {
        c.ai_blocked_timer = std::max(0.f, c.ai_blocked_timer - dt * 2.f);
        c.ai_honk_timer = std::max(0.f, c.ai_honk_timer - dt * 2.f);
    }
    c.ai_honking = blocked && c.ai_honk_timer >= c.ai_profile.honk_after;

    IntersectionId next = TrafficLaneGraph::lane_end(c.ai_lane);
    LightPhase next_phase = light_phase(next.i, next.j, c.ai_lane.dir,
                                        time_seconds);
    bool light_is_hard_red = (next_phase == LightPhase::Red
                              && c.ai_distance_along < ROAD_PITCH - STOP_BACK);
    bool jam_pass_allowed =
        leader_is_dynamic
        || (leader_is_stopped_ai
            && !light_is_hard_red
            && traffic_profile_may_pass_jam(c.ai_profile,
                                            c.ai_blocked_timer));

    if (blocked && jam_pass_allowed
        && c.ai_blocked_timer >= c.ai_profile.patience_seconds
        && c.ai_lane_change == LaneChangeIntent::None
        && ai_safe_to_shift(c, c.ai_profile.safe_lane_gap)) {
        c.ai_lane_change = LaneChangeIntent::AroundBlocker;
        c.ai_pass_until_distance = std::min(ROAD_PITCH - 10.f,
            std::max(c.ai_distance_along + 24.f,
                     c.ai_distance_along + best_gap + CAR_LENGTH + 10.f));
        c.ai_state = TrafficAgentState::AvoidObstacle;
    }
    if (c.ai_lane_change == LaneChangeIntent::AroundBlocker
        && (c.ai_distance_along >= c.ai_pass_until_distance
            || c.ai_distance_along > ROAD_PITCH - 14.f)) {
        c.ai_lane_change = LaneChangeIntent::ReturnToLane;
    }

    {
        float target_offset = 0.f;
        if (c.ai_lane_change == LaneChangeIntent::AroundBlocker)
            target_offset = -2.f * lane_offset_;
        float diff = target_offset - c.ai_lateral_offset;
        float step = std::min(std::abs(diff), SWERVE_RATE * dt);
        float prev = c.ai_lateral_offset;
        c.ai_lateral_offset += (diff > 0.f) ? step : -step;
        c.ai_lateral_rate = (c.ai_lateral_offset - prev) / std::max(dt, 1e-5f);
        if (c.ai_lane_change == LaneChangeIntent::ReturnToLane
            && std::abs(c.ai_lateral_offset) < 0.05f) {
            c.ai_lane_change = LaneChangeIntent::None;
            c.ai_lateral_offset = 0.f;
        }
        if (std::abs(c.ai_lateral_offset) > lane_offset_ * 0.25f) {
            target = std::min(target, 5.0f);
            if (c.ai_state != TrafficAgentState::AvoidObstacle)
                c.ai_state = TrafficAgentState::LaneChange;
        }
    }

    if (blocked && c.ai_blocked_timer > c.ai_profile.patience_seconds + 6.f) {
        c.ai_state = TrafficAgentState::BlockedRecovery;
        target = std::min(target, 1.2f);
    } else if (std::isfinite(best_gap)) {
        c.ai_state = blocked ? TrafficAgentState::Queued
                             : TrafficAgentState::FollowLeader;
    } else if (c.ai_state != TrafficAgentState::AvoidObstacle
               && c.ai_state != TrafficAgentState::LaneChange) {
        c.ai_state = TrafficAgentState::Cruise;
    }

    {
        bool must_stop = (next_phase == LightPhase::Red);
        if (next_phase == LightPhase::Yellow) {
            float dist_to_stop = (ROAD_PITCH - STOP_BACK) - c.ai_distance_along;
            must_stop = traffic_should_stop_for_yellow(dist_to_stop,
                                                       c.ai_speed,
                                                       c.ai_profile);
        }
        if (must_stop) {
            float stop_at = ROAD_PITCH - STOP_BACK;
            float gap = stop_at - c.ai_distance_along;
            if (gap > 0.f) {
                target = std::min(target,
                    traffic_follow_speed_for_gap(gap, c.ai_profile));
                c.ai_state = (gap < 10.f) ? TrafficAgentState::Queued
                                          : TrafficAgentState::ApproachSignal;
            }
        } else if (c.ai_distance_along > ROUTE_REFRESH_AT) {
            c.ai_state = TrafficAgentState::YieldIntersection;
        }
    }

    if (c.ai_in_turn) c.ai_state = TrafficAgentState::TraverseIntersection;

    float diff = target - c.ai_speed;
    float delta = (diff > 0.f) ? std::min(diff, c.ai_profile.accel * dt)
                                : std::max(diff, -c.ai_profile.brake * dt);
    c.ai_speed += delta;
    if (c.ai_speed < 0.f) c.ai_speed = 0.f;
}

void TrafficSystem::ai_advance(Car& c, float dt) {
    if (!ai_route_valid(c)) {
        c.driver = Driver::Parked;
        c.ai_state = TrafficAgentState::PhysicsFallback;
        c.vehicle.set_inputs(0.f, 0.f, 0.f, true);
        return;
    }

    ai_extend_route(c);
    c.ai_distance_along += c.ai_speed * dt;
    while (c.ai_distance_along >= ROAD_PITCH) {
        c.ai_distance_along -= ROAD_PITCH;
        c.ai_prev_lane = c.ai_lane;
        if (c.ai_route_index + 1u < c.ai_route.lanes.size()) {
            ++c.ai_route_index;
            c.ai_lane = c.ai_route.lanes[c.ai_route_index];
        } else if (graph_) {
            TrafficLaneGraph lane_graph(*graph_);
            c.ai_lane = lane_graph.choose_next_lane(c.ai_lane, rng_);
            c.ai_route.lanes.clear();
            c.ai_route.lanes.push_back(c.ai_lane);
            c.ai_route_index = 0;
        }
        c.ai_in_turn = (c.ai_prev_lane.dir != c.ai_lane.dir);
        c.ai_lateral_offset = 0.f;
        c.ai_lateral_rate = 0.f;
        c.ai_pass_until_distance = 0.f;
        c.ai_lane_change = LaneChangeIntent::None;
        ai_extend_route(c);
    }

    // Match the Bezier post-arc length in update_ai_kinematic (TURN_POST = 8 m).
    if (c.ai_in_turn && c.ai_distance_along > 8.f)
        c.ai_in_turn = false;

    c.ai_next_lane = c.ai_lane;
    if (c.ai_route_index + 1u < c.ai_route.lanes.size())
        c.ai_next_lane = c.ai_route.lanes[c.ai_route_index + 1u];
    TrafficTurnKind next_turn =
        TrafficLaneGraph::turn_kind(c.ai_lane.dir, c.ai_next_lane.dir);
    c.ai_turn_signal_left = (next_turn == TrafficTurnKind::Left);
    c.ai_turn_signal_right = (next_turn == TrafficTurnKind::Right);
}

void TrafficSystem::update_ai_kinematic(Car& c, float dt, double time_seconds) {
    ai_update_speed(c, dt, time_seconds);
    ai_advance(c, dt);
    if (c.driver != Driver::AI) return;

    TrafficLaneGraph lane_graph(*graph_);
    TrafficDirInfo info = traffic_dir_info(c.ai_lane.dir);

    // Turn region: a quadratic Bezier through the corner so position and
    // heading evolve together (the previous linear blend held velocity
    // direction constant while yaw rotated separately, which read as a
    // mid-intersection "snap"). The arc starts TURN_PRE before the
    // intersection on the from-lane and ends TURN_POST past it on the
    // to-lane. P1 is where the two lane tangent lines meet, so the Bezier's
    // tangent matches each lane's direction at the endpoints.
    constexpr float TURN_PRE  = 8.f;
    constexpr float TURN_POST = 8.f;
    constexpr float TURN_LEN  = TURN_PRE + TURN_POST;

    bool   in_turn_region = false;
    LaneId from_id{}, to_id{};
    float  turn_t = 0.f;

    if (c.ai_in_turn) {
        // Just past the lane handoff: turn_t covers the post-intersection
        // half of the arc.
        from_id = c.ai_prev_lane;
        to_id   = c.ai_lane;
        turn_t  = (TURN_PRE + c.ai_distance_along) / TURN_LEN;
        in_turn_region = true;
    } else if (c.ai_lane != c.ai_next_lane
               && c.ai_lane.dir != c.ai_next_lane.dir) {
        // Approaching the intersection on the from-lane: start arcing as
        // soon as we cross the TURN_PRE threshold. U-turns have parallel
        // lane tangents (no intersection point), so fall through to the
        // straight lane_pose path for those.
        TrafficTurnKind k = TrafficLaneGraph::turn_kind(c.ai_lane.dir,
                                                        c.ai_next_lane.dir);
        if (k != TrafficTurnKind::UTurn) {
            float dist_to_int = ROAD_PITCH - c.ai_distance_along;
            if (dist_to_int < TURN_PRE) {
                from_id = c.ai_lane;
                to_id   = c.ai_next_lane;
                turn_t  = (TURN_PRE - dist_to_int) / TURN_LEN;
                in_turn_region = true;
            }
        }
    }

    glm::vec3 xz;
    float yaw;

    if (in_turn_region) {
        turn_t = std::clamp(turn_t, 0.f, 1.f);
        TrafficDirInfo from_info = traffic_dir_info(from_id.dir);
        TrafficDirInfo to_info   = traffic_dir_info(to_id.dir);
        glm::vec3 isect = RoadGraph::intersection_pos(
            from_id.i + from_info.di, from_id.j + from_info.dj, 0.f);
        glm::vec3 A = isect + from_info.right * lane_offset_; // end of from
        glm::vec3 B = isect + to_info.right   * lane_offset_; // start of to
        // Tangent intersection. For perpendicular L/R turns this is the
        // inside corner; the Bezier curve sweeps from P0 to P2 staying on
        // the convex side, so left turns cut wide through the box and right
        // turns hug the corner — matches the look of real lane geometry.
        float     s  = glm::dot(B - A, from_info.unit);
        glm::vec3 P1 = A + from_info.unit * s;
        glm::vec3 P0 = A - from_info.unit * TURN_PRE;
        glm::vec3 P2 = B + to_info.unit   * TURN_POST;

        float u = turn_t, omu = 1.f - u;
        xz = omu*omu*P0 + 2.f*u*omu*P1 + u*u*P2;
        glm::vec3 tan = 2.f*omu*(P1 - P0) + 2.f*u*(P2 - P1);
        // Engine convention: yaw 0/90/180/270° = S/W/N/E with body +Z fwd
        // mapped via the rotation. atan2(-tan.x, -tan.z) reproduces the
        // table in traffic_dir_info().
        if (glm::length(tan) > 1e-4f)
            yaw = glm::degrees(std::atan2(-tan.x, -tan.z));
        else
            yaw = info.yaw_deg;
    } else {
        xz = lane_graph.lane_pose(c.ai_lane, c.ai_distance_along,
                                  lane_offset_, c.ai_lateral_offset);
        yaw = info.yaw_deg;
    }

    float ground = Heightmap::sample(xz.x, xz.z);
    const auto& ma = assets_->models[static_cast<std::size_t>(c.model_id)];
    glm::vec3 pos{xz.x, ground + ma.ride_height_at_rest, xz.z};

    float dev = 0.f;
    if (std::abs(c.ai_lateral_rate) > 1e-3f) {
        constexpr float MAX_DEV = glm::radians(30.f);
        float v_fwd_floor = std::max(c.ai_speed, 0.5f);
        dev = std::atan2(c.ai_lateral_rate, v_fwd_floor);
        dev = std::clamp(dev, -MAX_DEV, MAX_DEV);
    }
    glm::quat rot = glm::angleAxis(glm::radians(yaw) + dev,
                                    glm::vec3{0.f, 1.f, 0.f});
    c.vehicle.set_kinematic_pose(pos, rot);

    if (assets_->wheel_visible_radius > 1e-4f) {
        c.wheel_spin_rad += c.ai_speed * dt
                          / assets_->wheel_visible_radius;
    }
}

void TrafficSystem::try_ai_recover(Car& c, float dt) {
    // Only Parked cars carrying an intact AI route are eligible. (Player
    // demotions and route-invalidation despawns also use PhysicsFallback,
    // but they don't set ai_recovery_pending.)
    if (!c.ai_recovery_pending || c.driver != Driver::Parked) return;

    c.ai_recovery_timer += dt;

    // Give the chassis a moment to bleed off the impact before we test for
    // a re-attach. Tuned by feel: short enough that the player still sees
    // the AI try to keep going, long enough that the recovery snap doesn't
    // happen mid-bounce.
    constexpr float RECOVERY_DELAY  = 1.5f;   // s before first attempt
    constexpr float MAX_RECOVERY    = 8.0f;   // s before we give up entirely
    constexpr float MAX_SPEED       = 6.0f;   // m/s — must have slowed down
    constexpr float MIN_UPRIGHT_DOT = 0.6f;   // chassis up vs world up
    constexpr float MAX_LANE_OFFSET = 5.0f;   // m perp distance from lane line

    if (c.ai_recovery_timer < RECOVERY_DELAY) return;

    if (c.ai_recovery_timer > MAX_RECOVERY) {
        // Wreck never recovered — leave it parked permanently. The spawner
        // will keep AI population topped up regardless.
        c.ai_recovery_pending = false;
        return;
    }

    // Need a loaded lane to snap onto. If the area has streamed out, just
    // wait — try_ai_recover will run again next frame.
    if (!graph_ || !ai_route_valid(c)) return;

    glm::vec3 pos = c.vehicle.position();
    glm::vec3 up_world{0.f, 1.f, 0.f};
    if (glm::dot(c.vehicle.up(), up_world) < MIN_UPRIGHT_DOT) return;
    if (c.vehicle.speed() > MAX_SPEED) return;

    // Project the chassis onto its assigned lane. If the impact knocked
    // the car too far sideways or behind/past the lane segment, bail and
    // try again next frame (the car may still be sliding into range).
    TrafficLaneGraph lane_graph(*graph_);
    TrafficLane      L     = lane_graph.lane(c.ai_lane, lane_offset_);
    glm::vec3        delta = pos - L.start;
    float along = glm::dot(delta, L.unit);
    if (along < 0.f || along > ROAD_PITCH) return;
    glm::vec3 perp = delta - L.unit * along;
    perp.y = 0.f;
    if (glm::length(perp) > MAX_LANE_OFFSET) return;

    // All clear — snap pose to the lane and hand control back to the AI
    // script. Reset transient lane-change / turn state so the agent
    // doesn't think it's mid-manoeuvre.
    glm::vec3 snap_xz = lane_graph.lane_pose(c.ai_lane, along,
                                             lane_offset_, 0.f);
    float ground = Heightmap::sample(snap_xz.x, snap_xz.z);
    const auto& ma = assets_->models[static_cast<std::size_t>(c.model_id)];
    glm::vec3 snap_pos{snap_xz.x, ground + ma.ride_height_at_rest, snap_xz.z};

    TrafficDirInfo info = traffic_dir_info(c.ai_lane.dir);
    glm::quat snap_rot = glm::angleAxis(glm::radians(info.yaw_deg),
                                        glm::vec3{0.f, 1.f, 0.f});
    c.vehicle.set_kinematic_pose(snap_pos, snap_rot);

    c.ai_distance_along       = along;
    c.ai_speed                = 0.f;
    c.ai_in_turn              = false;
    c.ai_lateral_offset       = 0.f;
    c.ai_lateral_rate         = 0.f;
    c.ai_pass_until_distance  = 0.f;
    c.ai_lane_change          = LaneChangeIntent::None;
    c.ai_blocked_timer        = 0.f;
    c.ai_state                = TrafficAgentState::Cruise;
    c.ai_recovery_pending     = false;
    c.ai_recovery_timer       = 0.f;
    c.driver                  = Driver::AI;
}

void TrafficSystem::update_police_dynamic(Car& c, float dt) {
    (void)dt;
    glm::vec3 to_target = police_target_pos_ - c.vehicle.position();
    to_target.y = 0.f;
    float dist = glm::length(to_target);
    if (dist < 1e-3f) {
        c.vehicle.set_inputs(0.f, 1.f, 0.f, false);
        return;
    }

    glm::vec3 dir = to_target / dist;
    float ahead = glm::dot(c.vehicle.forward(), dir);
    float side  = glm::dot(c.vehicle.right(), dir);
    float speed = c.vehicle.speed();

    float steer = std::clamp(side * 2.2f, -1.f, 1.f);
    float throttle = 1.f;
    float brake = 0.f;

    // If the target is behind us, brake into a turn first; once nearly
    // stopped, reverse so officers can recover from missed passes.
    if (ahead < -0.25f) {
        if (speed > 5.f) {
            throttle = 0.f;
            brake = 0.75f;
        } else {
            throttle = -0.65f;
            brake = 0.f;
        }
    }

    // Don't endlessly shove at walking speed when already on top of the
    // player; brake unless we are lined up for an actual ram.
    if (dist < 8.f && ahead > 0.3f) {
        throttle = 0.35f;
        if (speed > 10.f) brake = 0.5f;
    }

    bool handbrake = std::abs(side) > 0.75f && ahead > 0.1f && speed > 14.f;
    c.vehicle.set_inputs(throttle, brake, steer, handbrake);
}

} // namespace pengine
