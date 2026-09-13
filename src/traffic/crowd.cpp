#include "traffic/crowd.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>

#include "city/city_rng.h"
#include "city/pedestrian_separation.h"
#include "core/fixed_step.h"
#include "game/character.h"
#include "game/weapon_hit.h"
#include <glm/gtc/quaternion.hpp>

#include "physics/breakaway_contact.h"
#include "physics/vehicle.h"

namespace apricot {

glm::vec3 traffic_body_local_contact(glm::vec2 forward_xz,
                                     glm::vec2 contact_offset_xz) {
    const float length = glm::length(forward_xz);
    if (length > 1e-5f) forward_xz /= length;
    else forward_xz = glm::vec2{0.0f, -1.0f};
    // In world XZ, rotating forward 90 degrees counter-clockwise gives the
    // chassis' +X/right axis. For the canonical -Z-facing car that must be
    // world +X. The previous expression returned its negative and mirrored
    // every AI victim's left/right damage zones.
    const glm::vec2 right{-forward_xz.y, forward_xz.x};
    return glm::vec3{glm::dot(contact_offset_xz, right), 0.0f,
                     -glm::dot(contact_offset_xz, forward_xz)};
}

namespace {

constexpr float kSimDtF = static_cast<float>(kSimDt);
constexpr float kInf = std::numeric_limits<float>::infinity();

// Swept body footprints, not just approach headings. Opposing through lanes
// can run together; a parallel lane cutting across a turn cannot. This is
// baked once per lane-graph build, never sampled in the per-driver loop.
bool turn_corridors_conflict(const std::vector<LanePose>& a,
                            const std::vector<LanePose>& b,
                            float half_width, float half_length) {
    for (const LanePose& pa : a) {
        const glm::vec2 af = glm::normalize(glm::vec2{pa.tangent.x, pa.tangent.z});
        const glm::vec2 ar{-af.y, af.x};
        for (const LanePose& pb : b) {
            const glm::vec2 d{pb.position.x - pa.position.x,
                              pb.position.z - pa.position.z};
            if (glm::dot(d, d) > 4.0f *
                    (half_width * half_width + half_length * half_length))
                continue;
            const glm::vec2 bf = glm::normalize(glm::vec2{pb.tangent.x, pb.tangent.z});
            const glm::vec2 br{-bf.y, bf.x};
            bool separated = false;
            for (glm::vec2 axis : {af, ar, bf, br}) {
                const float radius = half_length *
                    (std::fabs(glm::dot(af, axis)) + std::fabs(glm::dot(bf, axis))) +
                    half_width *
                    (std::fabs(glm::dot(ar, axis)) + std::fabs(glm::dot(br, axis)));
                if (std::fabs(glm::dot(d, axis)) > radius) {
                    separated = true;
                    break;
                }
            }
            if (!separated) return true;
        }
    }
    return false;
}

// The two halves of an agent's identity, packed for ordering only. Ordering is
// lexicographic on (lane key, slot) and never on a hash of them: a hash
// collision would put two distinct agents in an undefined relative order, and
// "undefined" is exactly the property this ordering exists to remove.
struct Ident {
    uint64_t key;
    uint32_t slot;
};

bool ident_less(const Ident& a, const Ident& b) {
    if (a.key != b.key) return a.key < b.key;
    return a.slot < b.slot;
}

uint64_t mix_bits(uint64_t h, uint64_t v) {
    return splitmix64_mix(h ^ (v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2)));
}

uint64_t mix_f32(uint64_t h, float f) {
    uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(bits));
    return mix_bits(h, bits);
}

int32_t floor_div(float v, float cell) {
    return static_cast<int32_t>(std::floor(v / cell));
}

// Cell hash for the pedestrian neighbour grid. Only ever used to pick a bucket;
// the real cell coordinates are compared on the way out, so a collision costs
// wasted distance tests and never a wrong neighbour set.
uint64_t cell_hash(int32_t cx, int32_t cz) {
    return hash_coord(0x9E3779B97F4A7C15ull, cx, cz);
}

const TurnLink* planned_turn(const LaneGraph& graph, LaneRef lane,
                             uint64_t seed, uint32_t decisions) {
    const LaneRef next = graph.choose_next(lane, seed, decisions);
    if (!graph.valid(next)) return nullptr;
    for (const TurnLink& link : graph.outgoing(lane)) {
        if (link.to == next) return &link;
    }
    return nullptr;
}

const TurnLink* turn_link_to(const LaneGraph& graph, LaneRef lane,
                             LaneRef next, bool pursuit = false) {
    if (!graph.valid(lane) || !graph.valid(next)) return nullptr;
    for (const TurnLink& link : graph.outgoing(lane, pursuit)) {
        if (link.to == next) return &link;
    }
    return nullptr;
}

// Where a pursuer with NO usable route turns. `plan_route` returns a one-lane
// holding route both when the target is unreachable and when the cop is
// already on the target's lane, and in either case the old code fell straight
// through to choose_next() — the weighted RANDOM draw ambient traffic uses. A
// cruiser in an active chase was therefore picking its turns by dice, U-turns
// included. Score each legal exit by where it actually leaves you relative to
// the target, tie-break on lane index so the choice is stable across builds,
// and never consult the dice.
LaneRef pursuit_fallback_exit(const LaneGraph& graph, LaneRef lane,
                              glm::vec2 target) {
    LaneRef best = kInvalidLane;
    float best_cost = std::numeric_limits<float>::infinity();
    for (const TurnLink& link : graph.outgoing(lane, true)) {
        if (!graph.valid(link.to)) continue;
        // A point INTO the exit, not its mouth: every exit off one junction
        // starts within a metre of every other, so mouths cannot rank them.
        const float probe = std::min(40.0f, graph.lane(link.to).length_m);
        const glm::vec3 ahead = graph.pose(link.to, probe).position;
        const float cost = glm::distance(glm::vec2{ahead.x, ahead.z}, target);
        if (cost < best_cost || (cost == best_cost && link.to < best)) {
            best_cost = cost;
            best = link.to;
        }
    }
    return best;
}

LaneRef agent_planned_exit(const LaneGraph& graph,
                           const VehicleAgent& agent, uint64_t seed,
                           glm::vec2 pursuit_target, bool have_target) {
    if (agent.police_pursuit &&
        agent.police_route_index < agent.police_route.size() &&
        agent.police_route[agent.police_route_index] == agent.lane &&
        agent.police_route_index + 1u < agent.police_route.size()) {
        const LaneRef next = agent.police_route[agent.police_route_index + 1u];
        if (turn_link_to(graph, agent.lane, next, true)) return next;
    }
    if (agent.police_pursuit && have_target) {
        const LaneRef next = pursuit_fallback_exit(graph, agent.lane,
                                                   pursuit_target);
        if (graph.valid(next)) return next;
    }
    return graph.choose_next(agent.lane, seed, agent.decisions);
}

const TurnLink* agent_planned_turn(const LaneGraph& graph,
                                   const VehicleAgent& agent,
                                   uint64_t seed, glm::vec2 pursuit_target,
                                   bool have_target) {
    if (agent.police_pursuit) {
        const LaneRef next = agent_planned_exit(graph, agent, seed,
                                                pursuit_target, have_target);
        if (const TurnLink* link = turn_link_to(graph, agent.lane, next, true))
            return link;
    }
    return planned_turn(graph, agent.lane, seed, agent.decisions);
}

// parked_vehicle_kind() moved to traffic/ambient.h: the bay gate needs the same
// answer this does, and a file-local copy is how the two came to disagree.

bool stable_police_patrol(uint64_t map_seed, uint64_t lane_key,
                          uint32_t slot, float fraction) {
    const float clamped = std::clamp(fraction, 0.0f, 1.0f);
    const uint64_t roll = splitmix64_mix(
        traffic_vehicle_identity_hash(lane_key, slot) ^ map_seed ^
        0x504F4C494345ull);
    // Use the high 24 bits for a stable [0,1) draw. Keeping the comparison in
    // float matches the live tuning type and avoids any platform RNG state.
    const float unit = static_cast<float>(roll >> 40) /
                       static_cast<float>(1u << 24);
    return unit < clamped;
}

int64_t police_response_cadence_steps(int wanted_level) {
    const float seconds = police_level_profile(wanted_level).cadence_s;
    return std::max<int64_t>(
        1, static_cast<int64_t>(std::ceil(seconds / kSimDtF)));
}

glm::vec3 officer_local_point(const VehicleAgent& car, glm::vec3 local) {
    glm::vec3 forward{car.fwd.x, 0.0f, car.fwd.z};
    if (glm::length(forward) < 1e-5f) forward = {0.0f, 0.0f, -1.0f};
    else forward = glm::normalize(forward);
    const glm::vec3 right{-forward.z, 0.0f, forward.x};
    return car.pos + right * local.x + glm::vec3{0.0f, local.y, 0.0f}
                   - forward * local.z;
}

// Visibility around the four expanded body corners makes a returning officer
// walk around their cruiser even when the suspect led them to its other side.
// This is a six-node local path, with fixed node/tie order, not a navmesh query.
glm::vec3 officer_car_waypoint(glm::vec3 from, glm::vec3 goal,
                               const VehicleAgent& car,
                               const PoliceOfficerVehicleLayout& layout) {
    glm::vec2 forward{car.fwd.x, car.fwd.z};
    if (glm::length(forward) < 1e-5f) return goal;
    forward = glm::normalize(forward);
    const glm::vec2 right{-forward.y, forward.x};
    auto local = [&](glm::vec3 p) {
        const glm::vec2 d{p.x - car.pos.x, p.z - car.pos.z};
        return glm::vec2{glm::dot(d, right), glm::dot(d, forward)};
    };
    const float hx = layout.half_width_m + 0.38f;
    const float hz = layout.half_length_m + 0.38f;
    auto blocked = [&](glm::vec2 a, glm::vec2 b) {
        const glm::vec2 d = b - a;
        float enter = 0.0f, leave = 1.0f;
        for (int axis = 0; axis < 2; ++axis) {
            const float half = axis == 0 ? hx : hz;
            if (std::fabs(d[axis]) < 1e-6f) {
                if (std::fabs(a[axis]) >= half) return false;
                continue;
            }
            float p = (-half - a[axis]) / d[axis];
            float q = (half - a[axis]) / d[axis];
            if (p > q) std::swap(p, q);
            enter = std::max(enter, p);
            leave = std::min(leave, q);
            if (enter >= leave) return false;
        }
        return enter < leave && leave > 0.001f && enter < 0.999f;
    };
    const std::array<glm::vec2, 6> nodes{{local(from), local(goal),
        {-hx - 0.12f, -hz - 0.12f}, {-hx - 0.12f, hz + 0.12f},
        {hx + 0.12f, -hz - 0.12f}, {hx + 0.12f, hz + 0.12f}}};
    if (!blocked(nodes[0], nodes[1])) return goal;
    std::array<float, 6> distance{{0.0f, kInf, kInf, kInf, kInf, kInf}};
    std::array<std::size_t, 6> previous{{6, 6, 6, 6, 6, 6}};
    std::array<bool, 6> visited{};
    for (int pass = 0; pass < 6; ++pass) {
        std::size_t current = 6;
        for (std::size_t n = 0; n < 6; ++n)
            if (!visited[n] && (current == 6 || distance[n] < distance[current]))
                current = n;
        if (current == 6 || !std::isfinite(distance[current])) break;
        visited[current] = true;
        for (std::size_t n = 0; n < 6; ++n) {
            if (visited[n] || blocked(nodes[current], nodes[n])) continue;
            const float cost = distance[current] + glm::distance(nodes[current], nodes[n]);
            if (cost + 1e-5f < distance[n]) {
                distance[n] = cost;
                previous[n] = current;
            }
        }
    }
    if (previous[1] == 6) return from;
    std::size_t next = 1;
    while (previous[next] > 0) next = previous[next];
    const glm::vec2 p = glm::vec2{car.pos.x, car.pos.z} +
                         right * nodes[next].x + forward * nodes[next].y;
    return {p.x, from.y, p.y};
}

float turn_speed_limit(TurnKind kind) {
    switch (kind) {
        case TurnKind::Straight: return 11.0f;
        case TurnKind::Right:    return 7.0f;
        case TurnKind::Left:     return 6.0f;
        case TurnKind::UTurn:    return 3.5f;
    }
    return 7.0f;
}

glm::vec2 clamp_length(glm::vec2 v, float max_length) {
    const float length = glm::length(v);
    if (length > max_length && length > 1e-5f)
        v *= max_length / length;
    return v;
}

void step_collision_reaction(VehicleAgent& vehicle,
                             const CrowdTuning& tuning,
                             const LanePose& lane_pose, float dt,
                             bool clearing_intersection) {
    glm::vec2 lane_fwd{lane_pose.tangent.x, lane_pose.tangent.z};
    const float lane_fwd_length = glm::length(lane_fwd);
    if (!(lane_fwd_length > 1e-5f)) return;
    lane_fwd /= lane_fwd_length;
    const glm::vec2 lane_right{-lane_fwd.y, lane_fwd.x};

    // A longitudinal shove becomes real route progress. Keeping it as a pose
    // offset would later drag the car backward to its pre-crash coordinate —
    // exactly the fake spring-back this recovery is designed to avoid.
    const float longitudinal_offset =
        glm::dot(vehicle.collision_offset_xz, lane_fwd);
    vehicle.dist_along_m = std::max(
        0.0f, vehicle.dist_along_m + longitudinal_offset);
    vehicle.collision_offset_xz -= lane_fwd * longitudinal_offset;

    const float longitudinal_speed =
        glm::dot(vehicle.collision_velocity_xz, lane_fwd);
    vehicle.dist_along_m = std::max(
        0.0f, vehicle.dist_along_m + longitudinal_speed * dt);
    vehicle.collision_velocity_xz -= lane_fwd * longitudinal_speed;

    // First coast with the impact. There is no positional spring here: drag
    // only removes the collision energy, so the car stays where it was shoved.
    vehicle.collision_offset_xz += vehicle.collision_velocity_xz * dt;
    vehicle.collision_velocity_xz *=
        std::exp(-std::max(0.0f, tuning.collision_linear_drag) * dt);
    vehicle.collision_yaw_rad += vehicle.collision_yaw_velocity * dt;
    vehicle.collision_yaw_velocity *=
        std::exp(-std::max(0.0f, tuning.collision_angular_drag) * dt);
    vehicle.collision_recovery_seconds =
        std::max(0.0f, vehicle.collision_recovery_seconds - dt);
    vehicle.collision_steer_rad = 0.0f;

    if (vehicle.collision_recovery_seconds <= 0.0f) {
        // Then the driver aims at a point ahead on the lane and STEERS there.
        // Lateral motion comes from forward speed and heading, so the recovery
        // draws an arc instead of translating the body sideways.
        const float lateral = glm::dot(vehicle.collision_offset_xz, lane_right);
        const float lookahead = std::max(
            2.0f, clearing_intersection
                      ? tuning.intersection_recovery_lookahead_m
                      : tuning.collision_recovery_lookahead_m);
        const float max_yaw = std::max(
            0.0f, clearing_intersection
                      ? tuning.intersection_recovery_max_yaw
                      : tuning.collision_recovery_max_yaw);
        // Positive Y yaw rotates the body toward -lane_right. The old target
        // used the opposite sign, making the car face away from its motion.
        const float desired_yaw = std::clamp(
            std::atan2(lateral, lookahead), -max_yaw, max_yaw);
        const float yaw_rate_limit = std::max(
            0.0f, clearing_intersection
                      ? tuning.intersection_recovery_yaw_rate
                      : tuning.collision_recovery_yaw_rate);
        const float wheelbase = std::max(2.2f,
            traffic_vehicle_footprint(traffic_vehicle_kind(vehicle))
                .half_length_m * 1.15f);
        const float steer_limit = std::clamp(
            tuning.collision_recovery_max_steer_rad, 0.0f, 0.75f);
        const float rolling_yaw_rate = std::max(0.0f, vehicle.speed_mps) *
                                       std::tan(steer_limit) / wheelbase;
        const float max_yaw_step = std::min(yaw_rate_limit, rolling_yaw_rate) * dt;
        const float yaw_step = std::clamp(desired_yaw - vehicle.collision_yaw_rad,
                                          -max_yaw_step, max_yaw_step);
        vehicle.collision_yaw_rad += yaw_step;
        if (vehicle.speed_mps > 0.01f && dt > 0.0f) {
            vehicle.collision_steer_rad = std::atan(
                wheelbase * yaw_step / (vehicle.speed_mps * dt));
        }
        // No positional recentering, even near zero: blocked cars stay where
        // they stopped, and the remaining error disappears only by driving.
    }
}

void apply_collision_pose(VehicleAgent& vehicle, const LanePose& lane_pose) {
    vehicle.pos = lane_pose.position +
                  glm::vec3{vehicle.collision_offset_xz.x, 0.0f,
                            vehicle.collision_offset_xz.y};
    const float c = std::cos(vehicle.collision_yaw_rad);
    const float s = std::sin(vehicle.collision_yaw_rad);
    vehicle.fwd = glm::normalize(glm::vec3{
        lane_pose.tangent.x * c + lane_pose.tangent.z * s,
        lane_pose.tangent.y,
        -lane_pose.tangent.x * s + lane_pose.tangent.z * c});
}

}  // namespace

TrafficTurnCurve traffic_turn_curve(const LaneGraph& graph, LaneRef incoming,
                                    LaneRef outgoing, float entry_m,
                                    float exit_m) {
    TrafficTurnCurve curve;
    if (!graph.valid(incoming) || !graph.valid(outgoing)) return curve;

    const Lane& in_lane = graph.lane(incoming);
    const Lane& out_lane = graph.lane(outgoing);
    const LanePose start = graph.pose(
        incoming, std::max(0.0f, in_lane.length_m - std::max(0.0f, entry_m)));
    const LanePose finish = graph.pose(
        outgoing, std::min(out_lane.length_m, std::max(0.0f, exit_m)));
    curve.p0 = start.position;
    curve.p3 = finish.position;

    const float chord = glm::length(curve.p3 - curve.p0);
    const float alignment = glm::clamp(
        glm::dot(start.tangent, finish.tangent), -1.0f, 1.0f);
    // A straight movement wants one-third handles so the cubic is exactly a
    // line. A quarter-circle wants about 0.39 of its chord; a U-turn needs a
    // little more room to avoid pinching in the middle.
    const float handle_mul = alignment > 0.75f
        ? (1.0f / 3.0f)
        : (alignment < -0.5f ? 0.52f : 0.40f);
    const float handle = chord * handle_mul;
    curve.p1 = curve.p0 + start.tangent * handle;
    curve.p2 = curve.p3 - finish.tangent * handle;

    auto point_at = [&](float t) {
        const float u = 1.0f - t;
        return u * u * u * curve.p0 +
               3.0f * u * u * t * curve.p1 +
               3.0f * u * t * t * curve.p2 +
               t * t * t * curve.p3;
    };
    curve.arc_m[0] = 0.0f;
    glm::vec3 previous = curve.p0;
    for (std::size_t i = 1; i < curve.arc_m.size(); ++i) {
        const float t = static_cast<float>(i) /
                        static_cast<float>(curve.arc_m.size() - 1u);
        const glm::vec3 point = point_at(t);
        curve.arc_m[i] = curve.arc_m[i - 1u] + glm::length(point - previous);
        previous = point;
    }
    curve.length_m = curve.arc_m.back();
    return curve;
}

LanePose traffic_turn_pose(const TrafficTurnCurve& curve,
                           float distance_along_m) {
    LanePose out;
    if (!(curve.length_m > 1e-5f)) {
        out.position = curve.p3;
        const glm::vec3 tangent = curve.p3 - curve.p0;
        if (glm::length(tangent) > 1e-5f)
            out.tangent = glm::normalize(tangent);
        out.right = glm::normalize(glm::cross(
            out.tangent, glm::vec3{0.0f, 1.0f, 0.0f}));
        return out;
    }

    const float d = std::clamp(distance_along_m, 0.0f, curve.length_m);
    auto upper = std::upper_bound(curve.arc_m.begin(), curve.arc_m.end(), d);
    std::size_t hi = static_cast<std::size_t>(upper - curve.arc_m.begin());
    hi = std::clamp<std::size_t>(hi, 1u, curve.arc_m.size() - 1u);
    const std::size_t lo = hi - 1u;
    const float segment = curve.arc_m[hi] - curve.arc_m[lo];
    const float local = segment > 1e-6f
        ? (d - curve.arc_m[lo]) / segment : 0.0f;
    const float t = (static_cast<float>(lo) + local) /
                    static_cast<float>(curve.arc_m.size() - 1u);
    const float u = 1.0f - t;
    out.position = u * u * u * curve.p0 +
                   3.0f * u * u * t * curve.p1 +
                   3.0f * u * t * t * curve.p2 +
                   t * t * t * curve.p3;
    const glm::vec3 derivative =
        3.0f * u * u * (curve.p1 - curve.p0) +
        6.0f * u * t * (curve.p2 - curve.p1) +
        3.0f * t * t * (curve.p3 - curve.p2);
    if (glm::length(derivative) > 1e-5f)
        out.tangent = glm::normalize(derivative);
    const glm::vec3 right = glm::cross(
        out.tangent, glm::vec3{0.0f, 1.0f, 0.0f});
    if (glm::length(right) > 1e-5f) out.right = glm::normalize(right);
    return out;
}

namespace {

bool active_turn(const LaneGraph& graph, const VehicleAgent& vehicle) {
    return graph.valid(vehicle.turn_from_lane) && graph.valid(vehicle.lane) &&
           vehicle.turn_length_m > 1e-5f;
}

TrafficTurnCurve agent_turn_curve(const LaneGraph& graph,
                                  const VehicleAgent& vehicle) {
    return traffic_turn_curve(graph, vehicle.turn_from_lane, vehicle.lane,
                              vehicle.turn_entry_m, vehicle.turn_exit_m);
}

LanePose route_pose(const LaneGraph& graph, const VehicleAgent& vehicle) {
    if (active_turn(graph, vehicle))
        return traffic_turn_pose(agent_turn_curve(graph, vehicle),
                                 vehicle.turn_progress_m);
    if (vehicle.maneuver.active())
        return traffic_maneuver_pose(vehicle.maneuver, vehicle.maneuver.progress_m);
    return graph.pose(vehicle.lane, vehicle.dist_along_m, vehicle.roadside_offset_m);
}

float curve_steer(const TrafficTurnCurve& curve, float progress_m) {
    const LanePose now = traffic_turn_pose(curve, progress_m);
    const LanePose ahead = traffic_turn_pose(
        curve, std::min(curve.length_m, progress_m + 1.8f));
    const float cross_y = now.tangent.z * ahead.tangent.x -
                          now.tangent.x * ahead.tangent.z;
    const float dot = glm::clamp(
        glm::dot(now.tangent, ahead.tangent), -1.0f, 1.0f);
    return std::clamp(std::atan2(cross_y, dot), -0.45f, 0.45f);
}

struct PlanarBodyContact {
    bool collided = false;
    float penetration_m = 0.0f;
    // Points from body A toward body B.
    glm::vec2 normal{1.0f, 0.0f};
};

PlanarBodyContact planar_body_contact(
    glm::vec2 a_centre, glm::vec2 a_right, glm::vec2 a_forward,
    float a_half_width, float a_half_length,
    glm::vec2 b_centre, glm::vec2 b_right, glm::vec2 b_forward,
    float b_half_width, float b_half_length) {
    PlanarBodyContact out;
    const glm::vec2 delta = b_centre - a_centre;
    float least_overlap = kInf;
    const std::array<glm::vec2, 4> axes{
        a_right, a_forward, b_right, b_forward};
    for (const glm::vec2 axis : axes) {
        const float centre_distance = glm::dot(delta, axis);
        const float a_radius =
            a_half_width * std::fabs(glm::dot(a_right, axis)) +
            a_half_length * std::fabs(glm::dot(a_forward, axis));
        const float b_radius =
            b_half_width * std::fabs(glm::dot(b_right, axis)) +
            b_half_length * std::fabs(glm::dot(b_forward, axis));
        const float overlap = a_radius + b_radius -
                              std::fabs(centre_distance);
        if (overlap <= 0.0f) return out;
        if (overlap < least_overlap) {
            least_overlap = overlap;
            out.normal = centre_distance < 0.0f ? -axis : axis;
        }
    }
    out.collided = true;
    out.penetration_m = least_overlap;
    return out;
}

// Exact point where a ray from an OBB centre in `direction` reaches its
// perimeter. This keeps front hits on bumpers and side hits on doors instead
// of stamping every non-cardinal hit onto a made-up corner.
glm::vec2 body_surface_offset(glm::vec2 right, glm::vec2 forward,
                              glm::vec2 direction,
                              glm::vec2 toward_other_centre,
                              float half_width, float half_length) {
    const float direction_length = glm::length(direction);
    if (!(direction_length > 1e-5f)) direction = forward;
    else direction /= direction_length;
    const float direction_x = glm::dot(direction, right);
    const float direction_f = glm::dot(direction, forward);
    const float tx = std::fabs(direction_x) > 1e-5f
        ? half_width / std::fabs(direction_x) : kInf;
    const float tf = std::fabs(direction_f) > 1e-5f
        ? half_length / std::fabs(direction_f) : kInf;
    const glm::vec2 radial = direction * std::min(tx, tf);
    float local_x = std::clamp(
        glm::dot(toward_other_centre, right), -half_width, half_width);
    float local_f = std::clamp(
        glm::dot(toward_other_centre, forward), -half_length, half_length);
    const float width_face_error =
        std::fabs(std::fabs(glm::dot(radial, right)) - half_width);
    const float length_face_error =
        std::fabs(std::fabs(glm::dot(radial, forward)) - half_length);
    if (width_face_error <= length_face_error) {
        local_x = std::copysign(half_width, glm::dot(direction, right));
    }
    if (length_face_error <= width_face_error) {
        local_f = std::copysign(half_length, glm::dot(direction, forward));
    }
    return right * local_x + forward * local_f;
}

struct DamageAdjustedPlanarBody {
    glm::vec2 centre{0.0f};
    float half_width = 0.1f;
    float half_length = 0.1f;
};

DamageAdjustedPlanarBody damage_adjusted_planar_body(
    glm::vec2 base_centre, glm::vec2 right, glm::vec2 forward,
    float base_half_width, float base_half_length,
    const VehicleDamageState& damage, glm::vec2 toward_other_centre) {
    const float half_width = std::max(base_half_width, 0.1f);
    const float half_length = std::max(base_half_length, 0.1f);
    const glm::vec2 toward = toward_other_centre - base_centre;
    // VehicleDamageState is -Z front while `forward` points toward the nose.
    const glm::vec2 local_contact01{
        glm::dot(toward, right) / half_width,
        -glm::dot(toward, forward) / half_length};
    const VehicleDamageCollisionFootprint footprint =
        vehicle_damage_collision_footprint(
            damage, half_width, half_length, local_contact01);

    DamageAdjustedPlanarBody out;
    out.centre = base_centre + right * footprint.centre_right_m +
                 forward * footprint.centre_forward_m;
    out.half_width = footprint.half_width_m;
    out.half_length = footprint.half_length_m;
    return out;
}

}  // namespace

TrafficBodyCollision resolve_traffic_body_collision(
    VehicleAgent& a, VehicleAgent& b, const CrowdTuning& tuning) {
    TrafficBodyCollision result;
    if (std::fabs(a.pos.y - b.pos.y) > 2.5f) return result;

    auto planar_forward = [](const VehicleAgent& vehicle) {
        glm::vec2 fwd{vehicle.fwd.x, vehicle.fwd.z};
        const float length = glm::length(fwd);
        return length > 1e-5f ? fwd / length : glm::vec2{0.0f, -1.0f};
    };
    const glm::vec2 af = planar_forward(a);
    const glm::vec2 bf = planar_forward(b);
    const glm::vec2 ar{-af.y, af.x};
    const glm::vec2 br{-bf.y, bf.x};
    const glm::vec2 ac{a.pos.x, a.pos.z};
    const glm::vec2 bc{b.pos.x, b.pos.z};
    const glm::vec2 delta = bc - ac;
    const TrafficVehicleFootprint a_footprint =
        traffic_vehicle_footprint(traffic_vehicle_kind(a));
    const TrafficVehicleFootprint b_footprint =
        traffic_vehicle_footprint(traffic_vehicle_kind(b));
    const float a_half_w = std::max(0.1f, a_footprint.half_width_m);
    const float a_half_l = std::max(0.1f, a_footprint.half_length_m);
    const float b_half_w = std::max(0.1f, b_footprint.half_width_m);
    const float b_half_l = std::max(0.1f, b_footprint.half_length_m);

    float least_overlap = kInf;
    glm::vec2 normal{1.0f, 0.0f};
    const std::array<glm::vec2, 4> axes{ar, af, br, bf};
    for (const glm::vec2 axis : axes) {
        const float centre_distance = glm::dot(delta, axis);
        const float a_radius =
            a_half_w * std::fabs(glm::dot(ar, axis)) +
            a_half_l * std::fabs(glm::dot(af, axis));
        const float b_radius =
            b_half_w * std::fabs(glm::dot(br, axis)) +
            b_half_l * std::fabs(glm::dot(bf, axis));
        const float overlap = a_radius + b_radius -
                              std::fabs(centre_distance);
        if (overlap <= 0.0f) return result;
        if (overlap < least_overlap) {
            least_overlap = overlap;
            normal = centre_distance < 0.0f ? -axis : axis;
        }
    }

    result.collided = true;
    result.penetration_m = least_overlap;
    result.normal_xz = normal;
    const glm::vec2 separation = normal * (least_overlap * 0.5f + 0.005f);
    a.collision_offset_xz -= separation;
    b.collision_offset_xz += separation;
    a.pos.x -= separation.x;
    a.pos.z -= separation.y;
    b.pos.x += separation.x;
    b.pos.z += separation.y;

    const glm::vec2 av = af * a.speed_mps + a.collision_velocity_xz;
    const glm::vec2 bv = bf * b.speed_mps + b.collision_velocity_xz;
    const float inward = glm::dot(bv - av, normal);
    result.closing_speed_mps = std::max(0.0f, -inward);
    const float damage = std::min(
        std::max(result.closing_speed_mps - 2.5f, 0.0f) * 2.2f, 35.0f);
    if (damage > 0.0f) {
        const auto support = [&](glm::vec2 body_right, glm::vec2 body_forward,
                                 glm::vec2 direction, float half_width,
                                 float half_length) {
            const float lateral = glm::dot(body_right, direction);
            const float longitudinal = glm::dot(body_forward, direction);
            const float lateral_weight = std::clamp(
                (std::fabs(lateral) - 0.35f) / 0.35f, 0.0f, 1.0f);
            const float longitudinal_weight = std::clamp(
                (std::fabs(longitudinal) - 0.35f) / 0.35f, 0.0f, 1.0f);
            return body_right * (std::copysign(half_width, lateral) *
                                 lateral_weight) +
                   body_forward * (std::copysign(half_length, longitudinal) *
                                   longitudinal_weight);
        };
        const glm::vec2 a_contact = support(
            ar, af, normal, a_half_w, a_half_l);
        const glm::vec2 b_contact = support(
            br, bf, -normal, b_half_w, b_half_l);
        const glm::vec2 relative_motion = bv - av;
        const float relative_speed = glm::length(relative_motion);
        const float glancing = relative_speed > 1e-5f
            ? std::clamp(1.0f - result.closing_speed_mps / relative_speed,
                         0.0f, 1.0f)
            : 0.0f;
        const glm::vec3 a_motion_local =
            traffic_body_local_contact(af, relative_motion);
        const glm::vec3 b_motion_local =
            traffic_body_local_contact(bf, -relative_motion);
        apply_vehicle_impact(a.body_damage,
                             traffic_body_local_contact(af, a_contact), damage,
                             a_half_w, a_half_l,
                             {a_motion_local.x, a_motion_local.z}, 0.43f,
                             0.58f, glancing);
        apply_vehicle_impact(b.body_damage,
                             traffic_body_local_contact(bf, b_contact), damage,
                             b_half_w, b_half_l,
                             {b_motion_local.x, b_motion_local.z}, 0.43f,
                             0.58f, glancing);
    }
    if (inward < 0.0f) {
        // Equal traffic masses cancel from the two-body impulse, leaving half
        // the relative closing speed on each car.
        const float delta_speed =
            -(1.0f + tuning.collision_restitution) * inward * 0.5f;
        const glm::vec2 impulse_velocity = normal * delta_speed;
        a.collision_velocity_xz -= impulse_velocity;
        b.collision_velocity_xz += impulse_velocity;
        a.collision_velocity_xz = clamp_length(
            a.collision_velocity_xz, tuning.collision_max_speed_mps);
        b.collision_velocity_xz = clamp_length(
            b.collision_velocity_xz, tuning.collision_max_speed_mps);

        auto cross2 = [](glm::vec2 x, glm::vec2 y) {
            return x.x * y.y - x.y * y.x;
        };
        const bool a_in_box = a.committed_junction != 0xFFFFFFFFu;
        const bool b_in_box = b.committed_junction != 0xFFFFFFFFu;
        if (!a_in_box || result.closing_speed_mps >=
                             tuning.intersection_low_speed_yaw_threshold_mps) {
            a.collision_yaw_velocity += std::clamp(
                cross2(af, -impulse_velocity) * 0.10f, -1.5f, 1.5f);
        }
        if (!b_in_box || result.closing_speed_mps >=
                             tuning.intersection_low_speed_yaw_threshold_mps) {
            b.collision_yaw_velocity += std::clamp(
                cross2(bf, impulse_velocity) * 0.10f, -1.5f, 1.5f);
        }
    }

    a.mode = AgentMode::Integrating;
    b.mode = AgentMode::Integrating;
    auto arm_recovery = [&](VehicleAgent& vehicle) {
        const bool in_box = vehicle.committed_junction != 0xFFFFFFFFu;
        const float delay = in_box
            ? tuning.intersection_collision_recovery_delay_s
            : tuning.collision_recovery_delay_s;
        if (in_box) {
            // Repeated low-speed body contact must not keep extending the
            // normal half-second shock pause forever.
            vehicle.collision_recovery_seconds =
                vehicle.collision_recovery_seconds > 0.0f
                    ? std::min(vehicle.collision_recovery_seconds, delay)
                    : std::max(0.0f, delay);
        } else if (result.closing_speed_mps > 0.2f) {
            vehicle.collision_recovery_seconds = std::max(
                vehicle.collision_recovery_seconds, delay);
        }
    };
    arm_recovery(a);
    arm_recovery(b);
    return result;
}

bool traffic_exit_has_storage(float nearest_vehicle_center_m,
                              float junction_clear_m, float car_length_m,
                              float min_follow_gap_m, float margin_m) {
    const float required = std::max(0.0f, junction_clear_m) +
                           std::max(0.0f, car_length_m) +
                           std::max(0.0f, min_follow_gap_m) +
                           std::max(0.0f, margin_m);
    return nearest_vehicle_center_m >= required;
}

bool traffic_same_committed_movement(
    LaneRef approach, LaneRef exit, LaneRef committed_approach,
    LaneRef committed_exit) {
    return approach != kInvalidLane && exit != kInvalidLane &&
           approach == committed_approach && exit == committed_exit;
}

float traffic_junction_clearance(float widest_carriageway_m,
                                 float vehicle_half_length_m, float margin_m,
                                 float minimum_m) {
    return std::max(std::max(0.0f, minimum_m),
                    0.5f * std::max(0.0f, widest_carriageway_m) +
                        std::max(0.0f, vehicle_half_length_m) +
                        std::max(0.0f, margin_m));
}

float traffic_junction_clearance(const LaneGraph& graph, uint32_t junction,
                                 const CrowdTuning& tuning) {
    const float minimum = std::max(tuning.stop_line_m, tuning.junction_clear_m);
    if (junction >= graph.junction_count()) return minimum;
    struct Arm { glm::vec2 point, outward; };
    std::vector<Arm> arms;
    float widest = 0.0f;
    float shortest = kInf;
    bool all_sidewalks = true;
    const auto& node = graph.junction(junction);
    auto add_arm = [&](LaneRef ref, bool incoming) {
        const Lane& lane = graph.lane(ref);
        widest = std::max(widest, lane.width_m);
        all_sidewalks = all_sidewalks && lane.sidewalks;
        shortest = std::min(shortest, lane.length_m);
        const LanePose pose = graph.pose(ref, incoming ? lane.length_m : 0.0f);
        arms.push_back({{pose.position.x, pose.position.z},
            glm::normalize(glm::vec2{pose.tangent.x, pose.tangent.z}) *
                (incoming ? -1.0f : 1.0f)});
    };
    for (LaneRef ref : node.incoming) add_arm(ref, true);
    for (LaneRef ref : node.outgoing) add_arm(ref, false);
    const float front_margin = node.degree >= 3 && all_sidewalks
        ? std::max(tuning.junction_storage_margin_m,
            kRoadJunctionMarginM + kRoadCrosswalkDepthM + 0.75f)
        : tuning.junction_storage_margin_m;
    const float base = traffic_junction_clearance(widest,
        tuning.traffic_half_length_m, front_margin, minimum);
    float clearance = base;
    auto cross = [](glm::vec2 a, glm::vec2 b) { return a.x*b.y - a.y*b.x; };
    // Angled road mouths can overlap beyond an ordinary square stop box.
    // Find where their lane corridors cross and reserve that whole merge area,
    // so two queues cannot park through one another while obeying their lights.
    for (std::size_t a = 0; a < arms.size(); ++a) {
        for (std::size_t b = a + 1; b < arms.size(); ++b) {
            const float alignment = glm::dot(arms[a].outward, arms[b].outward);
            // Yield/stop approaches can wait beside live priority traffic.
            // Their full angled mouths need clearance even at a 60-degree
            // merge. Signals already serialize those crossing directions;
            // retain their established box except for acute overlapping arms.
            const float alignment_floor = node.control == JunctionControl::Signal
                ? 0.8660254f : -0.8660254f;
            if (alignment < alignment_floor || alignment > 0.9999f) continue;
            const float sine = cross(arms[a].outward, arms[b].outward);
            const glm::vec2 delta = arms[b].point - arms[a].point;
            const float along_a = cross(delta, arms[b].outward) / sine;
            const float along_b = cross(delta, arms[a].outward) / sine;
            const float corridor = (2.0f*tuning.traffic_half_width_m + 0.5f) /
                                   std::fabs(sine);
            if (along_a + corridor < 0.0f || along_b + corridor < 0.0f) continue;
            clearance = std::max(clearance,
                std::max(along_a, along_b) + corridor + tuning.traffic_half_length_m);
        }
    }
    // Leave storage between adjacent controls; a curve must not consume the
    // next intersection's stop line on a short connecting lane.
    return std::min(clearance, std::max(base, shortest * 0.45f));
}

TrafficSignalPhase traffic_signal_phase(const LaneGraph& graph,
                                        uint32_t junction, LaneRef incoming,
                                        int64_t step,
                                        const CrowdTuning& tuning) {
    if (junction >= graph.junction_count() || !graph.valid(incoming) ||
        graph.junction_control(junction) != JunctionControl::Signal) {
        return TrafficSignalPhase::Green;
    }

    const int64_t period = std::max<int64_t>(2, tuning.signal_period_steps);
    const int64_t half = std::max<int64_t>(1, period / 2);
    const int64_t cycle = ((step % period) + period) % period;
    const bool phase_a_active = cycle < half;
    const bool approach_a = graph.approach_group_a(junction, incoming);
    if (approach_a != phase_a_active) return TrafficSignalPhase::Red;

    const int64_t within = cycle % half;
    const int64_t yellow = std::clamp<int64_t>(tuning.signal_yellow_steps, 0,
                                               std::max<int64_t>(0, half - 1));
    return yellow > 0 && within >= half - yellow
               ? TrafficSignalPhase::Yellow
               : TrafficSignalPhase::Green;
}

TrafficStopDecision traffic_stop_decision(float slack_to_line_m,
                                          float speed_mps,
                                          int64_t wait_steps,
                                          int elapsed_steps,
                                          int64_t required_steps,
                                          float capture_m) {
    TrafficStopDecision out;
    out.wait_steps = std::max<int64_t>(0, wait_steps);
    required_steps = std::max<int64_t>(0, required_steps);
    if (out.wait_steps >= required_steps) {
        out.hold = false;
        out.completed = true;
        return out;
    }
    if (slack_to_line_m <= std::max(0.0f, capture_m) && speed_mps <= 0.35f) {
        out.wait_steps += std::max(0, elapsed_steps);
    }
    out.completed = out.wait_steps >= required_steps;
    out.hold = !out.completed;
    return out;
}

DriverProfile traffic_driver_after_wait(const DriverProfile& profile,
                                        float delay_seconds) {
    DriverProfile out = profile;
    const float frustration = std::clamp(
        delay_seconds / std::max(1.0f, profile.patience_seconds * 3.0f), 0.0f, 1.0f);
    out.headway *= 1.0f - frustration * 0.15f;
    out.accel *= 1.0f + frustration * 0.15f;
    return out;
}

float traffic_gap_margin_seconds(const DriverProfile& profile, float delay_seconds) {
    const float comfort = std::max(1.25f, profile.headway * 1.6f);
    const float frustration = std::clamp(
        delay_seconds / std::max(1.0f, profile.patience_seconds * 3.0f), 0.0f, 1.0f);
    return std::max(0.9f, comfort * (1.0f - frustration * 0.35f));
}

float traffic_travel_seconds(float distance_m, float speed_mps,
                             float acceleration_mps2, float speed_cap_mps) {
    const float distance = std::max(0.0f, distance_m);
    const float cap = std::max(0.5f, speed_cap_mps);
    const float speed = std::clamp(speed_mps, 0.0f, cap);
    const float accel = std::max(0.1f, acceleration_mps2);
    const float ramp_distance = (cap * cap - speed * speed) / (2.0f * accel);
    if (distance <= ramp_distance)
        return (std::sqrt(speed * speed + 2.0f * accel * distance) - speed) / accel;
    return (cap - speed) / accel + (distance - ramp_distance) / cap;
}

bool traffic_approach_yields(const TrafficApproachView& mine,
                             const TrafficApproachView& other,
                             bool all_way_stop, float eta_tie_seconds) {
    if (!other.valid) return false;
    if (!mine.valid) return true;
    if (mine.committed != other.committed) return other.committed;

    auto mine_after_other = [&]() {
        if (mine.lane_key != other.lane_key)
            return mine.lane_key > other.lane_key;
        return mine.slot > other.slot;
    };

    if (all_way_stop) {
        if (mine.arrival_step >= 0 && other.arrival_step >= 0 &&
            mine.arrival_step != other.arrival_step) {
            return mine.arrival_step > other.arrival_step;
        }
        return mine_after_other();
    }

    if (mine.priority != other.priority)
        return mine.priority < other.priority &&
               other.eta_seconds <= mine.clearance_seconds;
    const float tie = std::max(0.0f, eta_tie_seconds);
    if (std::fabs(mine.eta_seconds - other.eta_seconds) > tie)
        return mine.eta_seconds > other.eta_seconds;
    return mine_after_other();
}

// ---------------------------------------------------------------------------
//  build
// ---------------------------------------------------------------------------

void Crowd::build(const LaneGraph& graph, uint64_t map_seed,
                  const AmbientTuning& ambient, const CrowdTuning& tuning) {
    clear();
    graph_ = &graph;
    map_seed_ = map_seed;
    ambient_ = ambient;
    tuning_ = tuning;
    ped_paths_.build(graph, ambient_.sidewalk_offset_m);

    const std::size_t n = graph.lane_count();
    veh_sched_.resize(n);
    ped_sched_.resize(n);
    parked_bays_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const Lane& l = graph.lane(static_cast<LaneRef>(i));
        veh_sched_[i] = vehicle_schedule(map_seed_, l, ambient_);
        // The hotspot gain is the one thing here that needs the GRAPH rather
        // than the lane, which is why it is resolved at this seam and threaded
        // in: ped_schedule() stays a pure function of one lane.
        ped_sched_[i] = ped_schedule(
            map_seed_, l, ambient_,
            ped_hotspot_gain(graph, static_cast<LaneRef>(i), ambient_));
        // How much kerb a road has is a fact about the road, so the bay is
        // derived once here beside the two schedules and never at runtime.
        parked_bays_[i] = parked_lane_bay(l, ambient_);
    }

    lane_buckets_.assign(n, {});
    junction_heads_.assign(graph.junction_count(), {});
    auto build_envelopes = [&](const CrowdTuning& envelope,
                               std::vector<float>& clearance_cache,
                               std::vector<float>& turn_cache,
                               std::set<std::array<LaneRef,4>>& conflict_cache) {
    clearance_cache.assign(graph.junction_count(),
                                 std::max(envelope.stop_line_m,
                                          envelope.junction_clear_m));
    turn_cache = clearance_cache;
    for (uint32_t j = 0; j < graph.junction_count(); ++j) {
        const LaneJunction& junction = graph.junction(j);
        clearance_cache[j] = traffic_junction_clearance(graph, j, envelope);
        float widest = 0.0f;
        for (LaneRef ref : junction.incoming)
            widest = std::max(widest, graph.lane(ref).width_m);
        const float core = traffic_junction_clearance(widest,
            envelope.traffic_half_length_m, envelope.junction_storage_margin_m,
            std::max(envelope.stop_line_m, envelope.junction_clear_m));
        turn_cache[j] = core;

        struct Movement { LaneRef from, to; std::vector<LanePose> path; };
        std::vector<Movement> movements;
        for (LaneRef incoming : junction.incoming) {
            for (const TurnLink& turn : graph.outgoing(incoming, true)) {
                const TrafficTurnCurve curve = traffic_turn_curve(
                    graph, incoming, turn.to, core, core);
                Movement movement{incoming, turn.to, {}};
                const float outer = clearance_cache[j];
                for (float d = outer; d > core; d -= 1.0f)
                    movement.path.push_back(graph.pose(incoming,
                        graph.length(incoming) - d));
                const int samples = std::max(2, static_cast<int>(std::ceil(curve.length_m / 1.0f)));
                for (int sample = 0; sample <= samples; ++sample)
                    movement.path.push_back(traffic_turn_pose(
                        curve, curve.length_m * static_cast<float>(sample) /
                                           static_cast<float>(samples)));
                for (float d = core; d < outer; d += 1.0f)
                    movement.path.push_back(graph.pose(turn.to, d));
                movement.path.push_back(graph.pose(turn.to, outer));
                movements.push_back(std::move(movement));
            }
        }
        for (std::size_t a = 0; a < movements.size(); ++a) {
            for (std::size_t b = a + 1; b < movements.size(); ++b) {
                const auto& first = movements[a];
                const auto& second = movements[b];
                if (!turn_corridors_conflict(first.path, second.path,
                        envelope.traffic_half_width_m + 0.25f,
                        envelope.traffic_half_length_m + 0.5f)) continue;
                conflict_cache.insert({first.from, first.to, second.from, second.to});
                conflict_cache.insert({second.from, second.to, first.from, first.to});
            }
        }
    }
    };
    build_envelopes(tuning_, junction_clearance_m_, junction_turn_clearance_m_,
                    movement_conflicts_);
    // Keep dry traffic byte-for-byte on its existing geometry. The larger
    // body/blade envelope is selected only for an active winter service.
    CrowdTuning service_envelope = tuning_;
    const auto body = traffic_vehicle_footprint(TrafficVehicleKind::Snowplow);
    service_envelope.traffic_half_width_m = std::max(service_envelope.traffic_half_width_m,
                                                    body.half_width_m);
    service_envelope.traffic_half_length_m = std::max(service_envelope.traffic_half_length_m,
                                                     body.half_length_m);
    build_envelopes(service_envelope, snowplow_junction_clearance_m_,
                    snowplow_junction_turn_clearance_m_, snowplow_movement_conflicts_);
    build_lane_index();
}

float Crowd::junction_clearance(uint32_t junction) const {
    const auto& cache = snowplow_service_ || snowplow_envelope_active_
        ? snowplow_junction_clearance_m_ : junction_clearance_m_;
    if (junction >= cache.size())
        return std::max(tuning_.stop_line_m, tuning_.junction_clear_m);
    return cache[junction];
}

float Crowd::junction_turn_clearance(uint32_t junction) const {
    const auto& cache = snowplow_service_ || snowplow_envelope_active_
        ? snowplow_junction_turn_clearance_m_ : junction_turn_clearance_m_;
    return junction < cache.size() ? cache[junction] : junction_clearance(junction);
}

bool Crowd::movements_conflict(LaneRef from, LaneRef to,
                               LaneRef other_from, LaneRef other_to) const {
    if (!graph_->valid(from) || !graph_->valid(to) ||
        !graph_->valid(other_from) || !graph_->valid(other_to)) return true;
    const auto& cache = snowplow_service_ || snowplow_envelope_active_
        ? snowplow_movement_conflicts_ : movement_conflicts_;
    return cache.count({from, to, other_from, other_to}) != 0u;
}

void Crowd::clear() {
    graph_ = nullptr;
    veh_sched_.clear();
    ped_sched_.clear();
    parked_bays_.clear();
    ambient_parked_.clear();
    ped_paths_.clear();
    index_cells_.clear();
    index_nx_ = index_nz_ = 0;
    vehicles_.clear();
    parked_vehicle_positions_.clear();
    peds_.clear();
    snowplow_service_ = false;
    snowplow_envelope_active_ = false;
    snowplow_dispatch_serial_ = 0;
    police_wanted_level_ = 0;
    police_target_xz_ = glm::vec2{0.0f};
    visible_police_.clear();
    police_response_due_ = false;
    police_next_response_step_ = 0;
    police_officers_enabled_ = false;
    police_target_on_foot_ = false;
    police_target_armed_ = false;
    police_target_velocity_ = {};
    police_target_speed_mps_ = 0.0f;
    police_officer_world_ = nullptr;
    police_officer_layout_ = {};
    police_search_centre_ = glm::vec2{0.0f};
    police_search_velocity_ = glm::vec2{0.0f};
    police_last_seen_step_ = -1;
    police_searching_ = false;
    police_level_rose_ = false;
    police_dispatch_serial_ = 0;
    police_response_gate_ = {};
    police_responding_level_ = 0;
    police_dispatch_radio_step_ = -1;
    police_armed_delay_s_ = 0.0f;
    police_escalated_from_zero_ = false;
    police_spawn_pending_ = false;
    police_player_contacts_.clear();
    police_shots_.clear();
    ped_run_downs_.clear();
    on_foot_player_hits_.clear();
    retired_.clear();
    lane_buckets_.clear();
    touched_lanes_.clear();
    leader_gap_.clear();
    leader_speed_.clear();
    junction_frozen_.clear();
    emergency_frozen_.clear();
    emergency_obstacles_.clear();
    emergency_reservations_.clear();
    junction_heads_.clear();
    junction_clearance_m_.clear();
    junction_turn_clearance_m_.clear();
    movement_conflicts_.clear();
    snowplow_movement_conflicts_.clear();
    snowplow_junction_clearance_m_.clear();
    snowplow_junction_turn_clearance_m_.clear();
    ped_bucket_start_.clear();
    ped_bucket_items_.clear();
    ped_bucket_of_.clear();
    ped_table_ = 0;
    ped_cell_keys_.clear();
    ped_pos_frozen_.clear();
    dead_nodes_.clear();
    stats_ = CrowdStats{};
}

void Crowd::set_police_officer_context(bool target_on_foot,
                                       bool target_armed,
                                       glm::vec2 target_velocity,
                                       const TerrainCollider* world) {
    police_officers_enabled_ = true;
    police_target_on_foot_ = target_on_foot;
    police_target_armed_ = target_on_foot && target_armed;
    police_target_velocity_ =
        std::isfinite(target_velocity.x) && std::isfinite(target_velocity.y)
            ? target_velocity : glm::vec2{0.0f};
    police_target_speed_mps_ = glm::length(police_target_velocity_);
    police_officer_world_ = world;
}

bool Crowd::report_police_vehicle_hit(VisiblePoliceIdentity cruiser) {
    if (police_wanted_level_ <= 0) return false;
    auto struck = std::find_if(vehicles_.begin(), vehicles_.end(), [&](const auto& car) {
        return car.police_unit && car.lane_key == cruiser.lane_key && car.slot == cruiser.slot;
    });
    if (struck == vehicles_.end()) return false;
    if (struck->police_pursuit) return true;
    const int target_units = police_level_profile(police_wanted_level_).units;
    if (static_cast<int>(police_pursuit_count()) >= target_units) {
        VehicleAgent* release = nullptr;
        float farthest = -1.0f;
        for (auto& car : vehicles_) {
            if (!car.police_pursuit) continue;
            const glm::vec2 delta{car.pos.x - police_target_xz_.x,
                                  car.pos.z - police_target_xz_.y};
            const float distance = glm::dot(delta, delta);
            if (!release || distance > farthest) {
                release = &car;
                farthest = distance;
            }
        }
        if (release) {
            release->police_pursuit = false;
            release->police_route.clear();
            release->police_route_index = 0;
            release->police_last_replan_step = -1;
        }
    }
    struck->police_pursuit = true;
    struck->mode = AgentMode::Integrating;
    struck->police_route.clear();
    struck->police_route_index = 0;
    struck->police_last_replan_step = -1;
    struck->police_last_target = police_target_xz_;
    return true;
}

void Crowd::step_police_officers(const VehicleState* player,
                                 const OnFootTrafficHazard* on_foot_player,
                                 int64_t step) {
    if (!police_officers_enabled_ || !graph_) return;
    const CharacterTuning walking{};
    // The car poses are final for this step and never change in this pass.
    // Freeze the officer poses too, so two officers do not read each other's
    // partly advanced walk and make scan order part of their decisions.
    std::vector<glm::vec3> frozen_people(vehicles_.size(), glm::vec3{kInf});
    for (std::size_t n = 0; n < vehicles_.size(); ++n)
        if (vehicles_[n].police_unit && police_officer_on_foot(vehicles_[n].officer))
            frozen_people[n] = vehicles_[n].officer.pos;

    for (std::size_t own = 0; own < vehicles_.size(); ++own) {
        VehicleAgent& car = vehicles_[own];
        if (!car.police_unit || !graph_->valid(car.lane)) continue;
        PoliceOfficerState& officer = car.officer;
        if (officer.weapon_flash_ticks > 0) --officer.weapon_flash_ticks;
        officer.previous_pos = officer.pos;
        officer.previous_heading = officer.heading;
        glm::vec3 forward{car.fwd.x, 0.0f, car.fwd.z};
        if (glm::length(forward) < 1e-5f) forward = {0.0f, 0.0f, -1.0f};
        else forward = glm::normalize(forward);
        const glm::vec3 right{-forward.z, 0.0f, forward.x};
        const glm::vec3 seat = officer_local_point(car,
            police_officer_layout_.driver_seat_local);
        glm::vec3 door = officer_local_point(car,
            police_officer_layout_.driver_door_local);

        auto ground = [&](glm::vec3& p, float reference_y) {
            if (!police_officer_world_) { p.y = reference_y; return true; }
            const float lift = walking.max_step_m + 0.08f;
            const auto support = police_officer_world_->probe_down(
                {p.x, reference_y + lift, p.z}, walking.max_drop_m + lift);
            if (!support.hit || support.normal.y < 0.55f ||
                support.point.y > reference_y + walking.max_step_m ||
                support.point.y < reference_y - walking.max_drop_m) return false;
            p.y = support.point.y;
            return character_position_clear(*police_officer_world_, p, walking);
        };
        auto body_blocks = [&](glm::vec3 p, const VehicleAgent& vehicle) {
            if (std::fabs(p.y - vehicle.pos.y) > 2.3f) return false;
            const auto footprint = traffic_vehicle_footprint(traffic_vehicle_kind(vehicle));
            const glm::vec3 d = p - vehicle.pos;
            const glm::vec3 vehicle_right{-vehicle.fwd.z, 0.0f, vehicle.fwd.x};
            return std::fabs(glm::dot(d, vehicle.fwd)) < footprint.half_length_m + walking.radius_m &&
                   std::fabs(glm::dot(d, vehicle_right)) < footprint.half_width_m + walking.radius_m;
        };
        auto dynamic_clear = [&](glm::vec3 p, bool skip_own) {
            for (std::size_t n = 0; n < vehicles_.size(); ++n) {
                if (!(skip_own && n == own) && body_blocks(p, vehicles_[n])) return false;
                if (skip_own && n != own) {
                    const VehicleAgent& other = vehicles_[n];
                    const glm::vec3 velocity = other.fwd * other.speed_mps +
                        glm::vec3{other.collision_velocity_xz.x, 0.0f,
                                  other.collision_velocity_xz.y};
                    for (int future = 1; future <= 4; ++future)
                        if (body_blocks(p - velocity * (0.15f * float(future)), other))
                            return false;
                }
                if (n != own && std::fabs(p.y - frozen_people[n].y) < 1.8f &&
                    glm::distance(glm::vec2{p.x, p.z},
                        glm::vec2{frozen_people[n].x, frozen_people[n].z}) < 0.67f)
                    return false;
            }
            if (player && !police_target_on_foot_ &&
                std::fabs(p.y - player->position.y) < 2.5f) {
                const glm::vec3 pf = vehicle_forward(*player);
                const glm::vec3 pr{-pf.z, 0.0f, pf.x};
                const glm::vec3 d = p - player->position;
                if (std::fabs(glm::dot(d, pf)) < 3.05f &&
                    std::fabs(glm::dot(d, pr)) < 1.50f) return false;
            }
            if (on_foot_player &&
                glm::distance(glm::vec2{p.x, p.z}, on_foot_player->position) < 0.75f)
                return false;
            return true;
        };
        auto clear_segment = [&](glm::vec3 from, glm::vec3 to, bool skip_own) {
            const float length = glm::distance(from, to);
            const int count = std::max(1, static_cast<int>(std::ceil(length / 0.14f)));
            float support_y = from.y;
            for (int sample = 1; sample <= count; ++sample) {
                glm::vec3 p = glm::mix(from, to, float(sample) / float(count));
                if (!ground(p, support_y) || !dynamic_clear(p, skip_own)) return false;
                support_y = p.y;
            }
            return true;
        };

        const bool needs_door = officer.phase != PoliceOfficerPhase::Seated;
        const bool supported_door = needs_door && ground(door, car.pos.y);
        officer.door_pos = door;
        const bool entering = officer.phase == PoliceOfficerPhase::Returning ||
                              officer.phase == PoliceOfficerPhase::Entering;
        const glm::vec3 door_facing = entering ? right : -right;
        officer.door_heading = std::atan2(door_facing.x, -door_facing.z);
        const float distance_to_target = glm::distance(
            glm::vec2{car.pos.x, car.pos.z}, police_target_xz_);
        const Lane& lane = graph_->lane(car.lane);
        const float stopping_distance = car.speed_mps * car.speed_mps /
            (2.0f * std::max(0.5f, car.profile.brake));
        const float stopping_room = officer.phase == PoliceOfficerPhase::Seated
            ? stopping_distance : 0.0f;
        const bool safe_road_position = !car.maneuver.active() && !active_turn(*graph_, car) &&
            car.committed_junction == 0xFFFFFFFFu &&
            car.dist_along_m > junction_clearance(lane.junction_from) + 3.0f &&
            lane.length_m - car.dist_along_m >
                junction_clearance(lane.junction_to) + 3.0f + stopping_room;
        bool door_clear = false;
        // Only perform capsule/door-sweep queries when a stopped officer may
        // actually use them. Ordinary patrol cars retain the cheap path.
        const float movement = std::fabs(car.speed_mps) +
            glm::length(car.collision_velocity_xz) +
            std::fabs(car.collision_yaw_velocity) * police_officer_layout_.half_length_m;
        if (supported_door && movement <= 0.08f &&
            officer.phase != PoliceOfficerPhase::Seated &&
            officer.phase != PoliceOfficerPhase::Pursuing) {
            door_clear = dynamic_clear(door, true) &&
                clear_segment(seat, door, true) &&
                clear_segment(door, door + forward * 0.75f, true);
        }
        const PoliceOfficerPhase old_phase = officer.phase;
        PoliceOfficerStepInput input;
        input.engaged = car.police_pursuit && police_wanted_level_ > 0;
        input.target_on_foot = police_target_on_foot_;
        input.target_speed_mps = police_target_speed_mps_;
        input.target_distance_m = distance_to_target;
        input.vehicle_speed_mps = movement;
        input.stopping_distance_m = stopping_distance;
        input.safe_road_position = safe_road_position;
        // A stationary queue near a stopped suspect is a place to dismount,
        // not a reason to sit with the lights on until the pursuit times out.
        input.approach_blocked = car.delay_seconds >= 2.0f;
        input.door_clear = door_clear;
        input.at_door = glm::distance(officer.pos, door) < 0.075f;
        step_police_officer_phase(officer, input);

        // Shot. The body stays exactly where it fell for the whole window:
        // no walk solve, no weapon, no door traversal. Its cruiser is already
        // held by police_officer_driving_allowed(), so putting an officer down
        // takes that unit out of the chase rather than leaving a driverless
        // car steering itself at the player.
        if (police_officer_downed(officer)) {
            officer.armed = false;
            officer.next_shot_step = -1;
            continue;
        }

        const bool can_arm = officer.phase == PoliceOfficerPhase::Pursuing &&
            input.engaged && police_target_on_foot_ && police_target_armed_;
        officer.armed = can_arm;
        if (!can_arm) officer.next_shot_step = -1;

        if (officer.phase == PoliceOfficerPhase::Seated ||
            officer.phase == PoliceOfficerPhase::Braking) {
            officer.pos = seat;
            officer.heading = std::atan2(forward.x, -forward.z);
            continue;
        }
        if (officer.transition.active()) {
            const auto sample = sample_vehicle_transition(officer.transition);
            officer.pos = glm::mix(door, seat, sample.traverse);
            officer.heading = officer.door_heading;
            continue;
        }
        if (old_phase == PoliceOfficerPhase::Exiting) {
            officer.pos = door;
            officer.heading = officer.door_heading;
            continue;
        }
        // Face and move using the same character solver as the player, then
        // add frozen traffic body clearance that the terrain collider lacks.
        glm::vec3 goal = officer.phase == PoliceOfficerPhase::Returning
            ? door : glm::vec3{police_target_xz_.x, officer.pos.y, police_target_xz_.y};
        const float stand_off = officer.phase == PoliceOfficerPhase::Returning
            ? 0.0f : (can_arm ? kPoliceArmedStandOffM :
                (police_target_on_foot_ ? kPoliceOfficerFootStandOffM : 4.4f));
        const glm::vec2 target_delta{goal.x - officer.pos.x, goal.z - officer.pos.z};
        const float target_distance = glm::length(target_delta);
        if (can_arm && target_distance > 1e-4f)
            officer.heading = std::atan2(target_delta.x, -target_delta.y);
        const bool has_shot = can_arm && on_foot_player &&
            target_distance <= kPoliceShootRangeM && police_has_line_of_sight(car);
        if (has_shot) {
            if (officer.next_shot_step < 0)
                officer.next_shot_step = police_first_shot_step(
                    step, car.lane_key, car.slot);
            if (step >= officer.next_shot_step) {
                const glm::vec3 muzzle = officer.pos + glm::vec3{0.0f, 1.42f, 0.0f};
                const glm::vec3 torso{police_target_xz_.x,
                    on_foot_player->height_m + 1.05f, police_target_xz_.y};
                police_shots_.push_back(make_police_shot(
                    car.lane_key, car.slot, officer.shots_fired,
                    step, muzzle, torso));
                ++officer.shots_fired;
                officer.weapon_flash_ticks = 8;
                officer.next_shot_step = step + kPoliceShotPeriodSteps;
            }
        } else if (can_arm) {
            officer.next_shot_step = -1;
        }
        if (glm::length(target_delta) <= stand_off + 0.03f) continue;
        goal = officer_car_waypoint(officer.pos, goal, car, police_officer_layout_);
        glm::vec3 direction = goal - officer.pos;
        direction.y = 0.0f;
        const float remaining = glm::length(direction);
        if (remaining < 1e-5f) continue;
        direction /= remaining;
        const float speed = officer.phase == PoliceOfficerPhase::Pursuing ? 4.8f : 2.8f;
        const float travel = std::min(remaining,
            std::min(speed * kSimDtF, std::max(0.0f, glm::length(target_delta) - stand_off)));
        // Prefer a straight stride. Fixed alternate headings let an officer
        // edge around a post or stopped neighbour without walking through it.
        for (float angle : {0.0f, 0.78539816f, -0.78539816f,
                            1.57079633f, -1.57079633f}) {
            const glm::vec3 candidate_direction{
                direction.x * std::cos(angle) - direction.z * std::sin(angle),
                0.0f,
                direction.x * std::sin(angle) + direction.z * std::cos(angle)};
            const glm::vec3 destination = officer.pos + candidate_direction * travel;
            const glm::vec3 lookahead = officer.pos + candidate_direction *
                std::min(0.38f, std::max(travel, remaining));
            if (!clear_segment(officer.pos, lookahead, false)) continue;
            glm::vec3 next = destination;
            if (!ground(next, officer.pos.y)) continue;
            const float target_heading = std::atan2(candidate_direction.x, -candidate_direction.z);
            if (police_officer_world_) {
                PlayerCharacterState current;
                current.position = officer.pos;
                current.facing_yaw = officer.heading;
                current.view_yaw = target_heading;
                CharacterTuning tuning = walking;
                tuning.walk_speed_mps = travel / kSimDtF;
                InputFrame intent;
                intent.throttle = 1.0f;
                next = step_character(current, tuning, intent,
                                      *police_officer_world_, kSimDtF).position;
            }
            if (!dynamic_clear(next, false)) continue;
            officer.distance_walked_m += glm::distance(officer.pos, next);
            officer.pos = next;
            const float turn = std::remainder(target_heading - officer.heading, 6.283185307f);
            officer.heading += std::clamp(turn, -12.0f * kSimDtF, 12.0f * kSimDtF);
            break;
        }
    }
}

void Crowd::set_police_context(
    int wanted_level, glm::vec2 target_xz,
    const std::vector<VisiblePoliceIdentity>& visible_police) {
    const int next_level = std::max(0, wanted_level);
    if (next_level > police_wanted_level_) {
        police_response_due_ = true;
        police_level_rose_ = true;
        if (police_wanted_level_ <= 0) police_escalated_from_zero_ = true;
    }
    police_wanted_level_ = next_level;
    police_target_xz_ = target_xz;
    // Is he even on the road? Half the lane width is normal driving; well
    // beyond it means a forecourt, a verge, a car park — somewhere the lane
    // graph has no answer for, and where a lane-following pursuit will park on
    // the nearest tarmac and look like it has given up. Which is exactly what
    // it has done.
    police_target_offroad_m_ = 0.0f;
    if (graph_) {
        const auto snap = graph_->nearest_lane(police_target_xz_, 45.0f);
        police_target_offroad_m_ = snap.valid()
            ? std::fabs(snap.lateral_m) : 45.0f;
    }
    visible_police_ = visible_police;
    std::sort(visible_police_.begin(), visible_police_.end());
    visible_police_.erase(
        std::unique(visible_police_.begin(), visible_police_.end()),
        visible_police_.end());

    if (next_level <= 0) {
        // Clear presentation state now, not after another traffic step. Loads,
        // cutscenes, and death resets can all render before the sim advances.
        for (VehicleAgent& agent : vehicles_) {
            if (!agent.police_pursuit) continue;
            agent.police_pursuit = false;
            agent.police_route.clear();
            agent.police_route_index = 0;
            agent.police_last_replan_step = -1;
        }
        police_response_due_ = false;
        police_next_response_step_ = 0;
        visible_police_.clear();
        police_search_centre_ = glm::vec2{0.0f};
        police_search_velocity_ = glm::vec2{0.0f};
        police_last_seen_step_ = -1;
        police_searching_ = false;
        police_level_rose_ = false;
        police_response_gate_ = {};
        police_responding_level_ = 0;
        police_dispatch_radio_step_ = -1;
        police_armed_delay_s_ = 0.0f;
        police_escalated_from_zero_ = false;
        police_spawn_pending_ = false;
    }
}

bool Crowd::police_has_line_of_sight(const VehicleAgent& agent) const {
    const VisiblePoliceIdentity identity{agent.lane_key, agent.slot};
    return std::binary_search(visible_police_.begin(), visible_police_.end(),
                              identity);
}

std::size_t Crowd::police_unit_count() const {
    return static_cast<std::size_t>(std::count_if(
        vehicles_.begin(), vehicles_.end(),
        [](const VehicleAgent& agent) { return agent.police_unit; }));
}

std::size_t Crowd::police_pursuit_count() const {
    return static_cast<std::size_t>(std::count_if(
        vehicles_.begin(), vehicles_.end(), [](const VehicleAgent& agent) {
            return agent.police_unit && agent.police_pursuit;
        }));
}

bool Crowd::player_in_police_view() const {
    if (police_wanted_level_ <= 0) return false;
    for (const VehicleAgent& agent : vehicles_) {
        if (!agent.police_unit) continue;
        // A cop face down in the road is not looking at anybody. Leaving him
        // in the view test would let a neutralised unit hold the wanted level
        // hot from the ground.
        if (police_officer_downed(agent.officer)) continue;
        if (!police_has_line_of_sight(agent)) continue;
        const glm::vec3 eye = police_officer_eye_position(agent);
        const glm::vec2 position{eye.x, eye.z};
        if (agent.police_pursuit) {
            if (police_maintains_contact(position, police_target_xz_,
                                         true, tuning_.police)) {
                return true;
            }
            continue;
        }
        const glm::vec3 forward = police_officer_forward(agent);
        if (police_can_witness(position, {forward.x, forward.z},
                               police_target_xz_, true, true,
                               tuning_.police)) {
            return true;
        }
    }
    return false;
}

bool Crowd::police_target_sighted() const {
    if (police_wanted_level_ <= 0) return false;
    const float range = police_level_profile(police_wanted_level_).detect_range_m;
    for (const VehicleAgent& agent : vehicles_) {
        if (!agent.police_unit) continue;
        if (police_officer_downed(agent.officer)) continue;
        if (!police_has_line_of_sight(agent)) continue;
        const glm::vec3 eye = police_officer_eye_position(agent);
        if (glm::distance(glm::vec2{eye.x, eye.z}, police_target_xz_) <= range) return true;
    }
    return false;
}

const VehicleAgent* Crowd::nearest_police_pursuer() const {
    const VehicleAgent* nearest = nullptr;
    float nearest_dist2 = kInf;
    for (const VehicleAgent& agent : vehicles_) {
        if (!agent.police_unit || !agent.police_pursuit) continue;
        const glm::vec2 delta{agent.pos.x - police_search_centre_.x,
                              agent.pos.z - police_search_centre_.y};
        const float dist2 = glm::dot(delta, delta);
        if (dist2 < nearest_dist2) {
            nearest = &agent;
            nearest_dist2 = dist2;
        }
    }
    return nearest;
}

void Crowd::update_police_route(VehicleAgent& agent, LaneRef target_lane,
                                glm::vec2 route_target, int64_t step) {
    if (!agent.police_pursuit || !graph_ || !graph_->valid(agent.lane)) {
        agent.police_route.clear();
        agent.police_route_index = 0;
        return;
    }

    // The frozen junction claim and the movement driven must agree. A target
    // crossing a side street cannot change the exit after admission: that
    // used to strand the cruiser at the lane end with its lights flashing.
    if (agent.committed_junction != 0xFFFFFFFFu) {
        agent.police_route = {agent.lane};
        if (agent.lane == agent.committed_approach_lane &&
            graph_->valid(agent.committed_exit_lane))
            agent.police_route.push_back(agent.committed_exit_lane);
        agent.police_route_index = 0;
        agent.police_last_replan_step = -1;
        return;
    }

    bool route_invalid = agent.police_route.empty();
    if (!route_invalid) {
        auto current = std::find(agent.police_route.begin() +
                                     static_cast<std::ptrdiff_t>(std::min<std::size_t>(
                                         agent.police_route_index,
                                         agent.police_route.size())),
                                 agent.police_route.end(), agent.lane);
        if (current == agent.police_route.end()) {
            route_invalid = true;
        } else {
            agent.police_route_index = static_cast<uint32_t>(
                current - agent.police_route.begin());
        }
    }

    const float since_last = agent.police_last_replan_step < 0
        ? std::numeric_limits<float>::infinity()
        : static_cast<float>(step - agent.police_last_replan_step) * kSimDtF;
    if (!police_should_replan(
            since_last, tuning_.police.replan_interval, route_target,
            agent.police_last_target, tuning_.police.replan_target_move,
            route_invalid)) {
        return;
    }

    agent.police_route.clear();
    if (graph_->valid(target_lane)) {
        LaneProjection from;
        from.lane = agent.lane;
        from.dist_along_m = agent.dist_along_m;
        agent.police_route = graph_->plan_route(from,
            graph_->project_onto(target_lane, route_target), true);
    }
    // An unreachable or off-road target must not make A* run every step. A
    // one-lane holding route is valid for the 0.6 s cadence, after which the
    // target is projected again and the cop gets another chance.
    if (agent.police_route.empty()) agent.police_route.push_back(agent.lane);
    agent.police_route_index = 0;
    agent.police_last_replan_step = step;
    agent.police_last_target = route_target;
}

void Crowd::update_police_response(int64_t step) {
    if (police_wanted_level_ <= 0) {
        for (VehicleAgent& agent : vehicles_) {
            if (!agent.police_pursuit) continue;
            agent.police_pursuit = false;
            agent.police_route.clear();
            agent.police_route_index = 0;
            agent.police_last_replan_step = -1;
        }
        police_response_due_ = false;
        police_next_response_step_ = step;
        police_searching_ = false;
        police_last_seen_step_ = -1;
        police_level_rose_ = false;
        police_response_gate_ = {};
        police_responding_level_ = 0;
        police_dispatch_radio_step_ = -1;
        police_escalated_from_zero_ = false;
        police_spawn_pending_ = false;
        return;
    }

    // WHERE THE PURSUIT IS GOING. Seen by any unit (the same per-unit range
    // and cone gates the witness check uses) -> the centre is the live
    // target. Not seen -> it freezes where he was last seen and the pursuit
    // becomes a search. A level that just rose re-centres on the crime even
    // with no witness: the report placed it, the way a phone call would.
    {
        const bool seen = police_target_sighted();
        const bool recentre = seen || police_level_rose_ || police_last_seen_step_ < 0;
        police_level_rose_ = false;
        if (recentre) {
            const bool was_searching = police_searching_;
            police_search_centre_ = police_target_xz_;
            police_search_velocity_ = police_target_velocity_;
            police_searching_ = false;
            if (seen || police_last_seen_step_ < 0) police_last_seen_step_ = step;
            // Re-acquired: every unit replans NOW, not in 0.6 s. A post is a
            // place to wait for a suspect nobody can see; once somebody can,
            // waiting there is the wrong thing to do for the whole interval.
            if (was_searching)
                for (VehicleAgent& agent : vehicles_)
                    if (agent.police_pursuit) agent.police_last_replan_step = -1;
        } else {
            police_searching_ = true;
            police_search_velocity_ = glm::vec2{0.0f};
        }
    }

    // THE RADIO HOLD (PENG-45). A crime that took the level from zero starts
    // the beat of nothing: `radio_latency_s` until the callout, then the
    // district's response time (read off the lane the suspect is on) before
    // the DISPATCHER acts on the level at all. The pure gate kernel owns the
    // three-state machine; this just feeds it. A patrol that witnessed the
    // crime converts below on the true level regardless — it did not need
    // the radio to tell it.
    {
        const PoliceTuning& pt = tuning_.police;
        if (police_escalated_from_zero_) {
            police_dispatch_radio_step_ = step + static_cast<int64_t>(
                std::ceil(std::max(0.0f, pt.radio_latency_s) / kSimDtF));
            float armed = pt.default_response_s;
            const LaneProjection snap = graph_->nearest_lane(police_target_xz_, 45.0f);
            if (snap.valid() && graph_->lane(snap.lane).response_s > 0.0f)
                armed = graph_->lane(snap.lane).response_s;
            police_armed_delay_s_ = armed;
        }
        if (pt.radio_latency_s <= 0.0f && police_armed_delay_s_ <= 0.0f) {
            // No hold authored at all: the kernel would sit pending forever
            // on a zero delay, so it is bypassed rather than fed a zero.
            police_response_gate_ = {};
            police_responding_level_ = police_wanted_level_;
        } else {
            const bool radio_played = police_dispatch_radio_step_ >= 0 &&
                                      step >= police_dispatch_radio_step_;
            const ResponseGateStep g = police_response_gate_step(
                police_response_gate_, police_wanted_level_,
                police_escalated_from_zero_, radio_played,
                police_armed_delay_s_, kSimDtF);
            police_response_gate_ = g.gate;
            police_responding_level_ = g.responding_level;
        }
        police_escalated_from_zero_ = false;
    }

    const int target_units = police_level_profile(police_wanted_level_).units;
    const int held_units = police_level_profile(police_responding_level_).units;
    int engaged = static_cast<int>(police_pursuit_count());
    auto engage = [&](VehicleAgent& agent) {
        agent.police_pursuit = true;
        agent.mode = AgentMode::Integrating;
        agent.police_route.clear();
        agent.police_route_index = 0;
        agent.police_last_replan_step = -1;
        agent.police_last_target = police_search_centre_;
        ++engaged;
    };

    // Organic response wins: a cruising patrol that actually sees the player
    // joins before the dispatcher converts a farther off-screen traffic slot.
    for (VehicleAgent& agent : vehicles_) {
        if (engaged >= target_units) break;
        if (!agent.police_unit || agent.police_pursuit ||
            vehicle_engine_failed(agent.mechanical) ||
            !police_has_line_of_sight(agent)) {
            continue;
        }
        const glm::vec3 eye = police_officer_eye_position(agent);
        const glm::vec3 forward = police_officer_forward(agent);
        if (police_can_witness(
                {eye.x, eye.z}, {forward.x, forward.z},
                police_target_xz_, true, true, tuning_.police)) {
            engage(agent);
        }
    }

    const int deficit = police_spawn_fallback_count(
        true, held_units, engaged);
    const bool response_tick = police_response_due_ ||
                               step >= police_next_response_step_;
    if (deficit > 0 && response_tick) {
        VehicleAgent* ring_candidate = nullptr;
        VehicleAgent* fallback_candidate = nullptr;
        uint64_t ring_score = std::numeric_limits<uint64_t>::max();
        uint64_t fallback_score = std::numeric_limits<uint64_t>::max();
        constexpr float kResponseMinM = 55.0f;
        constexpr float kResponseMaxM = 135.0f;
        for (VehicleAgent& agent : vehicles_) {
            if (!agent.police_unit || agent.police_pursuit ||
                vehicle_engine_failed(agent.mechanical) ||
                !graph_->valid(agent.lane)) {
                continue;
            }
            const glm::vec2 delta{agent.pos.x - police_target_xz_.x,
                                  agent.pos.z - police_target_xz_.y};
            const float dist2 = glm::dot(delta, delta);
            const uint64_t score = splitmix64_mix(
                traffic_vehicle_identity_hash(agent.lane_key, agent.slot) ^
                0x524553504F4E5345ull);
            const bool in_alpha_ring =
                dist2 >= kResponseMinM * kResponseMinM &&
                dist2 <= kResponseMaxM * kResponseMaxM;
            if (in_alpha_ring) {
                if (!ring_candidate || score < ring_score) {
                    ring_candidate = &agent;
                    ring_score = score;
                }
                continue;
            }
            // When the authored ring is sparse, keep drawing toward the alpha
            // target count from the already-active patrol pool. Never wake a
            // cruiser inside witness range: a nearby unit must actually own a
            // clear LOS identity and convert through the organic gate above.
            const float witness_range = std::max(
                0.0f, tuning_.police.witness_range);
            if (dist2 <= witness_range * witness_range) continue;
            if (!fallback_candidate || score < fallback_score) {
                fallback_candidate = &agent;
                fallback_score = score;
            }
        }
        VehicleAgent* candidate = ring_candidate
            ? ring_candidate : fallback_candidate;
        if (candidate) {
            engage(*candidate);
        } else if (police_level_profile(police_responding_level_).may_spawn) {
            // Nobody resident to convert: the next refresh instantiates one
            // out of sight (dispatch_police). Latched, not done here — the
            // vehicle vector is being iterated by the step that called us.
            police_spawn_pending_ = true;
        }
        police_response_due_ = false;
        police_next_response_step_ =
            step + police_response_cadence_steps(police_wanted_level_);
    } else if (deficit <= 0) {
        police_response_due_ = false;
    }

    const float target_snap_m = std::max(
        60.0f, std::min(180.0f, tuning_.vehicle_activate_m));
    // The intercept lead extrapolates the velocity AT THE LAST SIGHTING; while
    // searching that velocity is zero and the route goes to the point itself.
    const glm::vec2 route_target = police_pursuit_intercept(
        police_search_centre_, police_search_velocity_);
    LaneProjection target;
    if (glm::length(police_search_velocity_) > 0.15f)
        target = graph_->nearest_lane_along(
            route_target, police_search_velocity_, target_snap_m);
    if (!target.valid()) target = graph_->nearest_lane(route_target, target_snap_m);
    const LaneRef target_lane = target.valid() ? target.lane : kInvalidLane;

    // Who routes where. One route call per pursuer, with its own destination
    // decided first: calling the centre and then the post would replan twice
    // a step and thrash police_last_replan_step.
    std::vector<uint32_t> pursuers;
    for (uint32_t i = 0; i < vehicles_.size(); ++i)
        if (vehicles_[i].police_pursuit && graph_->valid(vehicles_[i].lane))
            pursuers.push_back(i);
    std::vector<LaneRef> dest_lane(pursuers.size(), target_lane);
    std::vector<glm::vec2> dest_xz(pursuers.size(), route_target);
    if (police_searching_ && target.valid() && pursuers.size() > 1) {
        // The HOLDER — nearest the centre, ties by identity — keeps the route
        // to it. Everyone else is ranked by an identity hash (stable while the
        // pursuer set is unchanged, so posts do not churn as cars move) and
        // sent one or two junctions out.
        std::size_t holder = 0;
        float holder_dist2 = kInf;
        for (std::size_t k = 0; k < pursuers.size(); ++k) {
            const VehicleAgent& v = vehicles_[pursuers[k]];
            const glm::vec2 delta{v.pos.x - police_search_centre_.x,
                                  v.pos.z - police_search_centre_.y};
            const float d2 = glm::dot(delta, delta);
            const VehicleAgent& h = vehicles_[pursuers[holder]];
            if (d2 < holder_dist2 ||
                (d2 == holder_dist2 &&
                 ident_less(Ident{v.lane_key, v.slot}, Ident{h.lane_key, h.slot}))) {
                holder = k;
                holder_dist2 = d2;
            }
        }
        std::vector<std::pair<uint64_t, std::size_t>> ranked;
        for (std::size_t k = 0; k < pursuers.size(); ++k) {
            if (k == holder) continue;
            const VehicleAgent& v = vehicles_[pursuers[k]];
            ranked.push_back({phantom_key(map_seed_, v.lane_key, v.slot,
                                          kChannelPoliceSearchPost), k});
        }
        std::sort(ranked.begin(), ranked.end(), [&](const auto& a, const auto& b) {
            if (a.first != b.first) return a.first < b.first;
            const VehicleAgent& x = vehicles_[pursuers[a.second]];
            const VehicleAgent& y = vehicles_[pursuers[b.second]];
            return ident_less(Ident{x.lane_key, x.slot}, Ident{y.lane_key, y.slot});
        });
        for (std::size_t ordinal = 0; ordinal < ranked.size(); ++ordinal) {
            const LaneRef post = police_search_post(
                *graph_, target.lane, static_cast<uint32_t>(ordinal));
            if (!graph_->valid(post)) continue;
            const Lane& lane = graph_->lane(post);
            const glm::vec3 p = graph_->pose(post, std::min(40.0f, lane.length_m)).position;
            dest_lane[ranked[ordinal].second] = post;
            dest_xz[ranked[ordinal].second] = glm::vec2{p.x, p.z};
        }
    }
    for (std::size_t k = 0; k < pursuers.size(); ++k)
        update_police_route(vehicles_[pursuers[k]], dest_lane[k], dest_xz[k], step);
}

LaneRef police_search_post(const LaneGraph& graph, LaneRef centre_lane,
                           uint32_t ordinal) {
    if (!graph.valid(centre_lane)) return kInvalidLane;
    auto sorted_exits = [&](LaneRef from) {
        std::vector<LaneRef> out;
        for (const TurnLink& link : graph.outgoing(from, true))
            if (graph.valid(link.to) && link.to != from) out.push_back(link.to);
        std::sort(out.begin(), out.end(), [&](LaneRef a, LaneRef b) {
            return graph.lane(a).key < graph.lane(b).key;
        });
        out.erase(std::unique(out.begin(), out.end()), out.end());
        return out;
    };
    const std::vector<LaneRef> first = sorted_exits(centre_lane);
    if (first.empty()) return kInvalidLane;
    const uint32_t n = static_cast<uint32_t>(first.size());
    const LaneRef hop1 = first[ordinal % n];
    const uint32_t ring = ordinal / n;
    if (ring == 0) return hop1;
    const std::vector<LaneRef> second = sorted_exits(hop1);
    if (second.empty()) return hop1;
    return second[(ring - 1) % static_cast<uint32_t>(second.size())];
}

// AN OFF-SCREEN CRUISER (PENG-45), on the snowplow dispatcher's pattern: a
// reserved slot space and a serial for fresh identities, lanes ranked by a
// stable key, a few separated stations per lane, and a final re-sort. What is
// police-specific: the station must sit in the annulus around the wanted
// centre, be occluded from it when there is a world to ask, and prefer the
// side the suspect is heading toward. The player's heading is the SUSPECT's
// velocity at the last sighting — a sim quantity — never a camera vector.
void Crowd::dispatch_police(glm::vec2 player_xz) {
    (void)player_xz;
    if (!graph_ || police_wanted_level_ <= 0 ||
        vehicles_.size() >= tuning_.max_vehicles) return;
    if (police_dispatch_serial_ >= 0x10000000u) return;
    const PoliceTuning& pt = tuning_.police;
    const uint32_t slot = kPoliceDispatchSlotBase + police_dispatch_serial_;
    const glm::vec2 centre = police_search_centre_;
    glm::vec2 heading{0.0f};
    if (glm::length(police_search_velocity_) > 1.0f)
        heading = glm::normalize(police_search_velocity_);
    // Ground height at the centre, for the occlusion ray.
    float centre_y = 0.0f;
    {
        const LaneProjection snap = graph_->nearest_lane(centre, 45.0f);
        if (snap.valid()) centre_y = graph_->pose(snap.lane, snap.dist_along_m).position.y;
    }
    std::vector<LaneRef> candidates;
    gather_lanes(centre, pt.spawn_max_m + 5.0f, candidates);
    std::sort(candidates.begin(), candidates.end(), [&](LaneRef a, LaneRef b) {
        const uint64_t ak = graph_->lane(a).key, bk = graph_->lane(b).key;
        const uint64_t ah = phantom_key(map_seed_, ak, slot, kChannelPoliceDispatch);
        const uint64_t bh = phantom_key(map_seed_, bk, slot, kChannelPoliceDispatch);
        return ah == bh ? ak < bk : ah < bh;
    });
    LaneRef best_lane = kInvalidLane;
    float best_along = 0.0f, best_score = -1.0f;
    LanePose best_pose{};
    for (LaneRef ref : candidates) {
        const Lane& lane = graph_->lane(ref);
        if (lane.cls == RoadClass::Dirt || lane.cls == RoadClass::Alley ||
            graph_->outgoing(ref).empty()) continue;
        const float start = junction_clearance(lane.junction_from) + 8.0f;
        const float end = lane.length_m - std::max(tuning_.spawn_junction_exclusion_m,
            junction_clearance(lane.junction_to)) - 8.0f;
        if (end <= start) continue;
        for (int sample = 0; sample < 5; ++sample) {
            const float along = start + (end - start) * (static_cast<float>(sample) + 0.5f) / 5.0f;
            const LanePose pose = graph_->pose(ref, along);
            const glm::vec2 here{pose.position.x, pose.position.z};
            const float dist = glm::distance(here, centre);
            if (dist < pt.spawn_min_m || dist > pt.spawn_max_m) continue;
            bool clear = true;
            for (const VehicleAgent& v : vehicles_) {
                if (std::fabs(v.pos.y - pose.position.y) > 3.0f) continue;
                const glm::vec2 separation{v.pos.x - pose.position.x, v.pos.z - pose.position.z};
                if (glm::dot(separation, separation) < 12.0f * 12.0f ||
                    (v.lane == ref && std::fabs(v.dist_along_m - along) <
                        std::max(18.0f, v.speed_mps * 2.0f))) { clear = false; break; }
            }
            if (!clear) continue;
            if (police_officer_world_) {
                // Out of sight of the centre: a cruiser that pops into being
                // in view is the one thing this exists to avoid.
                const glm::vec3 from = pose.position + glm::vec3{0.0f, 1.2f, 0.0f};
                const glm::vec3 to{centre.x, centre_y + 1.05f, centre.y};
                const glm::vec3 delta = to - from;
                const float length = glm::length(delta);
                if (length > 0.05f) {
                    const auto hit = police_officer_world_->raycast(from, delta / length, length);
                    if (!hit.hit || hit.distance >= length - 0.08f) continue;
                }
            }
            Rng roll{phantom_key(map_seed_, lane.key,
                                 slot ^ (static_cast<uint32_t>(sample + 1) * 0x9E3779B9u),
                                 kChannelPoliceDispatch)};
            const glm::vec2 dir = dist > 1e-3f ? (here - centre) / dist : glm::vec2{0.0f};
            const float score = roll.next_float() * (0.5f + 0.5f * glm::dot(heading, dir));
            if (score > best_score) {
                best_score = score; best_lane = ref; best_along = along; best_pose = pose;
            }
        }
        // Hash order makes the first lane with a usable station the answer.
        if (best_score >= 0.0f) break;
    }
    if (!graph_->valid(best_lane)) return;
    const Lane& lane = graph_->lane(best_lane);
    VehicleAgent v;
    v.lane_key = lane.key;
    v.slot = slot;
    v.lane = best_lane;
    v.dist_along_m = v.last_dist_m = best_along;
    v.speed_mps = std::min(12.0f, lane.speed_limit_mps);
    v.cruise_mps = lane.speed_limit_mps;
    v.mode = AgentMode::Integrating;
    v.police_unit = true;
    v.police_pursuit = true;
    v.police_last_target = centre;
    v.police_last_replan_step = -1;
    v.profile = driver_profile_for(map_seed_,
        static_cast<int32_t>(static_cast<uint32_t>(lane.key)),
        static_cast<int32_t>(static_cast<uint32_t>(lane.key >> 32)), slot);
    v.pos = best_pose.position;
    v.fwd = best_pose.tangent;
    v.officer.pos = v.officer.previous_pos =
        officer_local_point(v, police_officer_layout_.driver_seat_local);
    v.officer.heading = v.officer.previous_heading = std::atan2(v.fwd.x, -v.fwd.z);
    v.spawn_ordinal = static_cast<uint32_t>(stats_.activated);
    vehicles_.push_back(v);
    ++stats_.activated;
    ++police_dispatch_serial_;
    std::sort(vehicles_.begin(), vehicles_.end(), [](const VehicleAgent& a, const VehicleAgent& b) {
        return ident_less(Ident{a.lane_key, a.slot}, Ident{b.lane_key, b.slot});
    });
}

void Crowd::build_lane_index() {
    index_cells_.clear();
    if (!graph_ || graph_->lane_count() == 0) return;

    glm::vec2 lo{kInf, kInf};
    glm::vec2 hi{-kInf, -kInf};
    for (const Lane& l : graph_->lanes()) {
        for (const glm::vec3& p : l.centreline) {
            lo = glm::min(lo, glm::vec2{p.x, p.z});
            hi = glm::max(hi, glm::vec2{p.x, p.z});
        }
    }
    if (!(hi.x >= lo.x)) return;

    index_min_ = lo - glm::vec2{index_cell_m_};
    const glm::vec2 span = (hi - lo) + glm::vec2{2.0f * index_cell_m_};
    index_nx_ = std::max(1, static_cast<int>(span.x / index_cell_m_) + 1);
    index_nz_ = std::max(1, static_cast<int>(span.y / index_cell_m_) + 1);
    index_cells_.assign(static_cast<std::size_t>(index_nx_) *
                            static_cast<std::size_t>(index_nz_),
                        {});

    // A lane goes into every cell its bounding box touches. Conservative rather
    // than exact: a rasterised segment walk would put fewer lanes in fewer
    // cells, and gather_lanes() distance-filters anyway, so the exactness buys
    // nothing and the walk is one more thing to get wrong at a corner.
    for (std::size_t i = 0; i < graph_->lane_count(); ++i) {
        const Lane& l = graph_->lane(static_cast<LaneRef>(i));
        if (l.centreline.empty()) continue;
        glm::vec2 a{kInf, kInf};
        glm::vec2 b{-kInf, -kInf};
        for (const glm::vec3& p : l.centreline) {
            a = glm::min(a, glm::vec2{p.x, p.z});
            b = glm::max(b, glm::vec2{p.x, p.z});
        }
        const int x0 = std::max(0, floor_div(a.x - index_min_.x, index_cell_m_));
        const int x1 = std::min(index_nx_ - 1,
                                floor_div(b.x - index_min_.x, index_cell_m_));
        const int z0 = std::max(0, floor_div(a.y - index_min_.y, index_cell_m_));
        const int z1 = std::min(index_nz_ - 1,
                                floor_div(b.y - index_min_.y, index_cell_m_));
        for (int z = z0; z <= z1; ++z) {
            for (int x = x0; x <= x1; ++x) {
                const std::size_t c = static_cast<std::size_t>(z) *
                                          static_cast<std::size_t>(index_nx_) +
                                      static_cast<std::size_t>(x);
                index_cells_[c].push_back(static_cast<LaneRef>(i));
            }
        }
    }
}

void Crowd::gather_lanes(glm::vec2 xz, float radius_m,
                         std::vector<LaneRef>& out) const {
    out.clear();
    if (index_cells_.empty()) return;
    const int x0 = std::max(0, floor_div(xz.x - radius_m - index_min_.x, index_cell_m_));
    const int x1 = std::min(index_nx_ - 1,
                            floor_div(xz.x + radius_m - index_min_.x, index_cell_m_));
    const int z0 = std::max(0, floor_div(xz.y - radius_m - index_min_.y, index_cell_m_));
    const int z1 = std::min(index_nz_ - 1,
                            floor_div(xz.y + radius_m - index_min_.y, index_cell_m_));
    for (int z = z0; z <= z1; ++z) {
        for (int x = x0; x <= x1; ++x) {
            const std::size_t c = static_cast<std::size_t>(z) *
                                      static_cast<std::size_t>(index_nx_) +
                                  static_cast<std::size_t>(x);
            const std::vector<LaneRef>& cell = index_cells_[c];
            out.insert(out.end(), cell.begin(), cell.end());
        }
    }
    // A lane spanning several cells arrives several times. Sorting and uniquing
    // is what makes the candidate list a function of the QUERY and not of the
    // cell walk order.
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

// ---------------------------------------------------------------------------
//  sub-rate scheduling
// ---------------------------------------------------------------------------

int Crowd::sub_rate_phase(uint64_t lane_key, uint32_t slot, uint32_t index,
                          uint32_t spawn_ordinal, int k) const {
    if (k <= 1) return 0;
    const uint32_t uk = static_cast<uint32_t>(k);
    switch (tuning_.policy) {
        case SubRatePolicy::Keyed: {
            // The phase travels with the agent, so it is the same in every run
            // that contains this agent, regardless of what else is active.
            const uint64_t h = phantom_key(map_seed_, lane_key, slot, 0x2500u);
            return static_cast<int>(h % uk);
        }
        case SubRatePolicy::ContainerIndex:
            // Safe here, and only because the vector is sorted by identity.
            return static_cast<int>(index % uk);
        case SubRatePolicy::SpawnOrdinal:
            // The bug, on purpose.
            return static_cast<int>(spawn_ordinal % uk);
    }
    return 0;
}

bool Crowd::take_vehicle(uint64_t lane_key, uint32_t slot, VehicleAgent& out) {
    const auto it=std::find_if(vehicles_.begin(),vehicles_.end(),[&](const VehicleAgent& v) {
        return v.lane_key==lane_key && v.slot==slot;
    });
    if (it==vehicles_.end() || it->police_unit || it->snowplow_unit) return false;
    out=*it;
    if (it->node!=kInvalidId) dead_nodes_.push_back(it->node);
    retire(lane_key, slot, it->generation);
    vehicles_.erase(it);
    stats_.vehicles=vehicles_.size();
    rebuild_buckets();
    return true;
}

// Retired identities sort by (lane, slot, lap). The lap is part of the KEY, not
// a payload: two departures of one slot are two identities, and only the one
// that was actually simulated may be banned.
bool Crowd::retired_id_less(const RetiredId& a, const RetiredId& b) {
    if (a.key != b.key || a.slot != b.slot)
        return ident_less(Ident{a.key, a.slot}, Ident{b.key, b.slot});
    return a.lap < b.lap;
}

bool Crowd::is_retired(uint64_t lane_key, uint32_t slot, int64_t lap) const {
    const RetiredId want{lane_key, slot, std::numeric_limits<int64_t>::min()};
    const auto it = std::lower_bound(retired_.begin(), retired_.end(), want,
                                     retired_id_less);
    if (it == retired_.end() || it->key != lane_key || it->slot != slot)
        return false;
    // `>=`, not `==`. A lap can only ever be asked about after the laps below
    // it, so the older-lap case should be unreachable — and if it ever becomes
    // reachable, refusing is the safe answer: spawning a departure OLDER than
    // one already retired would materialise a car at an obsolete closed-form
    // position, which is the exact thing permanent retirement exists to stop.
    // This makes that an invariant of the structure, not of an argument.
    return it->lap >= lap;
}

void Crowd::retire(uint64_t lane_key, uint32_t slot, int64_t lap) {
    // ONE ENTRY PER (lane, slot), holding the NEWEST retired lap. An entry for
    // an older lap is unreachable the moment a newer one exists, because the
    // lap a slot is on only ever increases with the step — so keeping the
    // older one would be memory spent on a question that can never be asked.
    // This is what stops lap-aware retirement from turning a set that grew
    // once per (lane, slot) into one that grows once per retirement: the bound
    // is unchanged from before this policy, at 8 bytes more per entry.
    const RetiredId want{lane_key, slot, std::numeric_limits<int64_t>::min()};
    const auto it = std::lower_bound(retired_.begin(), retired_.end(), want,
                                     retired_id_less);
    if (it != retired_.end() && it->key == lane_key && it->slot == slot) {
        it->lap = std::max(it->lap, lap);
        return;
    }
    retired_.insert(it, RetiredId{lane_key, slot, lap});
}

// ---------------------------------------------------------------------------
//  refresh: instantiate what came into range, retire what left
// ---------------------------------------------------------------------------

std::size_t Crowd::snowplow_unit_count() const {
    return static_cast<std::size_t>(std::count_if(vehicles_.begin(), vehicles_.end(),
        [](const VehicleAgent& v) { return v.snowplow_unit; }));
}

void Crowd::dispatch_snowplows(glm::vec2 player_xz) {
    if (!snowplow_service_ || !graph_) return;
    std::size_t fleet = snowplow_unit_count();
    if (fleet >= kMaxSnowplowFleet || vehicles_.size() >= tuning_.max_vehicles) return;
    // Dedicated service identities cannot collide with car or pedestrian slots.
    // Rank lanes by a stable key, independent of rebuild and scan order.
    constexpr uint32_t service_slot_base = 0x40000000u;
    std::vector<LaneRef> candidates = lane_scratch_;
    std::sort(candidates.begin(), candidates.end(), [&](LaneRef a, LaneRef b) {
        const uint64_t ak = graph_->lane(a).key, bk = graph_->lane(b).key;
        const uint64_t ah = phantom_key(map_seed_, ak, service_slot_base, 0x534E4F57u);
        const uint64_t bh = phantom_key(map_seed_, bk, service_slot_base, 0x534E4F57u);
        return ah == bh ? ak < bk : ah < bh;
    });
    for (LaneRef ref : candidates) {
        if (fleet >= kMaxSnowplowFleet || vehicles_.size() >= tuning_.max_vehicles) break;
        // Each departure has a fresh lifetime identity. Crowd retirement is
        // permanent, but it must not permanently exhaust a service lane after
        // the first storm. The serial advances only on successful dispatch.
        if (snowplow_dispatch_serial_ >= service_slot_base) break;
        const uint32_t service_slot = service_slot_base + snowplow_dispatch_serial_;
        const Lane& lane = graph_->lane(ref);
        if (lane.cls == RoadClass::Dirt || lane.cls == RoadClass::Alley ||
            graph_->outgoing(ref).empty()) continue;
        if (std::any_of(vehicles_.begin(), vehicles_.end(), [&](const VehicleAgent& v) {
                return v.lane_key == lane.key && v.snowplow_unit;
            })) continue;
        const float start = junction_clearance(lane.junction_from) + 8.0f;
        const float end = lane.length_m - std::max(tuning_.spawn_junction_exclusion_m,
            junction_clearance(lane.junction_to)) - 8.0f;
        if (end <= start) continue;
        // Try a few separated points so busy lanes do not starve the service.
        for (int sample = 0; sample < 5; ++sample) {
            const float along = start + (end-start) * (static_cast<float>(sample)+0.5f)/5.0f;
            const LanePose pose = graph_->pose(ref, along);
            const glm::vec2 delta{pose.position.x-player_xz.x, pose.position.z-player_xz.y};
            if (glm::dot(delta, delta) > tuning_.vehicle_activate_m*tuning_.vehicle_activate_m ||
                glm::dot(delta, delta) < 35.0f*35.0f) continue;
            bool clear = true;
            for (const VehicleAgent& v : vehicles_) {
                if (std::fabs(v.pos.y-pose.position.y) > 3.0f) continue;
                const glm::vec2 separation{v.pos.x-pose.position.x, v.pos.z-pose.position.z};
                // A working convoy spreads its blades across the local network.
                const float gap = v.snowplow_unit ? 55.0f : 12.0f;
                if (glm::dot(separation,separation) < gap*gap ||
                    (v.lane == ref && std::fabs(v.dist_along_m-along) <
                        std::max(18.0f, v.speed_mps*2.0f))) { clear = false; break; }
            }
            if (!clear) continue;
            VehicleAgent v;
            v.lane_key = lane.key;
            v.slot = service_slot;
            v.lane = ref;
            v.dist_along_m = v.last_dist_m = along;
            v.speed_mps = v.cruise_mps = std::min(kSnowplowWorkSpeedMps, lane.speed_limit_mps);
            v.mode = AgentMode::Integrating;
            v.snowplow_unit = true;
            v.pos = pose.position;
            v.fwd = pose.tangent;
            v.profile = driver_profile_for(map_seed_,
                static_cast<int32_t>(static_cast<uint32_t>(lane.key)),
                static_cast<int32_t>(static_cast<uint32_t>(lane.key >> 32)), service_slot);
            v.spawn_ordinal = static_cast<uint32_t>(stats_.activated);
            vehicles_.push_back(v);
            ++stats_.activated;
            ++fleet;
            snowplow_envelope_active_ = true;
            ++snowplow_dispatch_serial_;
            break;
        }
    }
    std::sort(vehicles_.begin(), vehicles_.end(), [](const VehicleAgent& a, const VehicleAgent& b) {
        return ident_less(Ident{a.lane_key,a.slot}, Ident{b.lane_key,b.slot});
    });
}

void Crowd::refresh(int64_t step, glm::vec2 player_xz) {
    if (!graph_) return;

    const float retire_r = std::max(
        std::max(tuning_.vehicle_retire_m, tuning_.ped_retire_m),
        tuning_.parked_activate_m);
    gather_lanes(player_xz, retire_r, lane_scratch_);
    stats_.lanes_scanned = lane_scratch_.size();

    const float va2 = tuning_.vehicle_activate_m * tuning_.vehicle_activate_m;
    const float vr2 = tuning_.vehicle_retire_m * tuning_.vehicle_retire_m;
    const float pa2 = tuning_.ped_activate_m * tuning_.ped_activate_m;
    const float pr2 = tuning_.ped_retire_m * tuning_.ped_retire_m;

    // --- retire ------------------------------------------------------------
    // std::remove_if is stable, so the survivors keep their sorted order and
    // the merge below only has to splice the newcomers in.
    {
        const auto dead = std::remove_if(
            vehicles_.begin(), vehicles_.end(), [&](const VehicleAgent& v) {
                const glm::vec2 d{v.pos.x - player_xz.x, v.pos.z - player_xz.y};
                const float dist2 = d.x * d.x + d.y * d.y;
                if (dist2 <= vr2) return false;
                if (v.police_unit && !police_officer_driving_allowed(v.officer)) {
                    const glm::vec2 officer_delta{v.officer.pos.x - player_xz.x,
                                                  v.officer.pos.z - player_xz.y};
                    if (glm::dot(officer_delta, officer_delta) <= vr2) return false;
                }
                // A UNIT THAT IS CHASING YOU IS NOT AMBIENT TRAFFIC.
                // The ordinary ring deletes anything past ~320 m, and a cruiser
                // that spends fifteen seconds turning around is already past it
                // at city speeds. Retiring it there is what made a pursuit read
                // as nobody following: the car behind you was never the same
                // car twice, because each one was deleted as soon as it lost
                // ground and a fresh local patrol was converted in its place.
                // And a unit SEARCHING is where it was sent: posted at the
                // corner the suspect vanished from, which may be well behind
                // where he actually is by now. Measure it from the search
                // centre as well as from him, or the search retires its own
                // units for doing their job.
                float pursuit_dist2 = dist2;
                if (police_searching_) {
                    const glm::vec2 c{v.pos.x - police_search_centre_.x,
                                      v.pos.z - police_search_centre_.y};
                    pursuit_dist2 = std::min(pursuit_dist2, c.x * c.x + c.y * c.y);
                }
                if (v.police_unit && v.police_pursuit &&
                    !police_should_despawn_standdown(
                        pursuit_dist2, police_wanted_level_ <= 0,
                        tuning_.police.stand_down_distance,
                        tuning_.police.pursuit_retire_m))
                    return false;
                if (v.node != kInvalidId) dead_nodes_.push_back(v.node);
                retire(v.lane_key, v.slot, v.generation);
                ++stats_.retired;
                return true;
            });
        vehicles_.erase(dead, vehicles_.end());
    }
    {
        const auto dead = std::remove_if(
            peds_.begin(), peds_.end(), [&](const PedAgent& p) {
                const glm::vec2 d{p.pos.x - player_xz.x, p.pos.z - player_xz.y};
                if (d.x * d.x + d.y * d.y <= pr2) return false;
                if (p.node != kInvalidId) dead_nodes_.push_back(p.node);
                retire(p.lane_key, p.slot, p.generation);
                ++stats_.retired;
                return true;
            });
        peds_.erase(dead, peds_.end());
    }

    snowplow_envelope_active_ = snowplow_unit_count() > 0;

    // --- instantiate -------------------------------------------------------
    const std::size_t veh_have = vehicles_.size();
    const std::size_t ped_have = peds_.size();

    for (std::size_t li = 0; li < lane_scratch_.size(); ++li) {
        const LaneRef lr = tuning_.reverse_scan_order
                               ? lane_scratch_[lane_scratch_.size() - 1 - li]
                               : lane_scratch_[li];
        const Lane& lane = graph_->lane(lr);

        const LaneSchedule& vs = veh_sched_[lr];
        for (uint32_t slot = 0; slot < vs.slots; ++slot) {
            if (vehicles_.size() >= tuning_.max_vehicles) break;
            const Ident id{lane.key, slot};
            const auto lo = std::lower_bound(
                vehicles_.begin(), vehicles_.begin() + static_cast<long>(veh_have),
                id, [](const VehicleAgent& a, const Ident& b) {
                    return ident_less(Ident{a.lane_key, a.slot}, b);
                });
            if (lo != vehicles_.begin() + static_cast<long>(veh_have) &&
                lo->lane_key == id.key && lo->slot == id.slot)
                continue;
            // The LAP, not just the pair: this slot re-departs once per
            // schedule period, and banning the pair alone would destroy every
            // future departure along with the one car that was simulated.
            const int64_t lap = phantom_lap(vs, slot, step);
            if (is_retired(id.key, id.slot, lap)) continue;

            const PhantomState ph =
                phantom_vehicle(map_seed_, lane, vs, slot, step, ambient_);
            // A phantom has no cross-traffic history. Promoting one after the
            // stop line, or inside the departure corridor, would materialise
            // a car in an intersection without ever passing its right-of-way
            // gate. Both ends matter, especially on an acute self-loop.
            // Defer this slot until its schedule reaches open lane space.
            if (ph.dist_along_m < junction_clearance(lane.junction_from) ||
                lane.length_m - ph.dist_along_m <
                std::max(tuning_.spawn_junction_exclusion_m,
                         junction_clearance(lane.junction_to)))
                continue;
            // Nor inside a parked car. A bay that reaches into the driving
            // corridor (none does at the authored clearance, but the knob
            // exists) would otherwise materialise a phantom overlapping a
            // body it then can neither pass nor push. Defer the slot the same
            // way the junction window does.
            if (tuning_.civilian.ambient_parked_obstacles &&
                lr < parked_bays_.size() && parked_bays_[lr].slots > 0) {
                const ParkedLaneBay& bay = parked_bays_[lr];
                if (bay.lateral_m - ambient_.parked_half_width_m <
                    tuning_.traffic_half_width_m + tuning_.civilian.parked_corridor_margin_m &&
                    bay.pitch_m > 0.0f) {
                    const float k = std::round((ph.dist_along_m - bay.first_m) / bay.pitch_m);
                    const uint32_t nearest = static_cast<uint32_t>(
                        std::clamp(k, 0.0f, static_cast<float>(bay.slots - 1)));
                    const ParkedSlot ps = parked_slot(map_seed_, lane, bay, nearest, ambient_);
                    if (std::fabs(ph.dist_along_m - ps.dist_along_m) <
                        2.0f * tuning_.traffic_half_length_m + 1.0f)
                        continue;
                }
            }
            // A scheduled phantom only knows about the other slots authored
            // on this lane. Real cars can arrive here from upstream between
            // refreshes. Do not materialise the phantom through one of those
            // cars; wait until its deterministic schedule reaches open road.
            // This is a frozen membership scan: existing cars only, never the
            // candidates being appended below, so scan order cannot decide
            // which identities exist.
            const float spawn_gap = traffic_vehicle_spacing_m(
                ambient_.vehicle_spacing_m, lane.traffic_density,
                ph.speed_mps);
            bool spawn_space_clear = true;
            for (auto existing_it = vehicles_.begin();
                 existing_it != vehicles_.begin() + static_cast<long>(veh_have);
                 ++existing_it) {
                const VehicleAgent& existing = *existing_it;
                if (existing.lane == lr &&
                    std::fabs(existing.dist_along_m - ph.dist_along_m) <
                        spawn_gap) {
                    spawn_space_clear = false;
                    break;
                }
            }
            if (!spawn_space_clear) continue;
            const LanePose pose = graph_->pose(lr, ph.dist_along_m);
            const glm::vec2 d{pose.position.x - player_xz.x,
                              pose.position.z - player_xz.y};
            if (d.x * d.x + d.y * d.y > va2) continue;

            VehicleAgent v;
            v.lane_key = lane.key;
            v.slot = slot;
            v.generation = lap;
            v.lane = lr;
            v.dist_along_m = ph.dist_along_m;
            v.speed_mps = ph.speed_mps;
            v.cruise_mps = ph.speed_mps;
            v.last_dist_m = ph.dist_along_m;
            v.mode = AgentMode::Analytic;
            // The driver comes off the agent's identity, not off a stream. The
            // lane key is split across the two coordinate axes exactly as
            // lane_graph.h prescribes, so the same car has the same driver
            // forever regardless of approach order.
            v.profile = driver_profile_for(
                map_seed_,
                static_cast<int32_t>(static_cast<uint32_t>(lane.key)),
                static_cast<int32_t>(static_cast<uint32_t>(lane.key >> 32)), slot);
            v.police_unit = stable_police_patrol(
                map_seed_, lane.key, slot, tuning_.police.patrol_fraction);
            v.pos = pose.position;
            v.fwd = pose.tangent;
            if (v.police_unit) {
                v.officer.pos = v.officer.previous_pos =
                    officer_local_point(v, police_officer_layout_.driver_seat_local);
                v.officer.heading = v.officer.previous_heading =
                    std::atan2(v.fwd.x, -v.fwd.z);
            }
            v.spawn_ordinal = static_cast<uint32_t>(stats_.activated);
            vehicles_.push_back(v);
            ++stats_.activated;
        }

        const LaneSchedule& ps = ped_sched_[lr];
        for (uint32_t slot = 0; slot < ps.slots; ++slot) {
            if (peds_.size() >= tuning_.max_peds) break;
            const uint32_t walk_path = ped_paths_.for_lane(lr, slot);
            if (!ped_paths_.valid(walk_path)) continue;
            const Ident id{lane.key, slot};
            const auto lo = std::lower_bound(
                peds_.begin(), peds_.begin() + static_cast<long>(ped_have), id,
                [](const PedAgent& a, const Ident& b) {
                    return ident_less(Ident{a.lane_key, a.slot}, b);
                });
            if (lo != peds_.begin() + static_cast<long>(ped_have) &&
                lo->lane_key == id.key && lo->slot == id.slot)
                continue;
            // Same policy as vehicles: ban the departure, not the slot.
            const int64_t lap = phantom_lap(ps, slot, step);
            if (is_retired(id.key, id.slot, lap)) continue;

            const PhantomState ph =
                phantom_ped(map_seed_, lane, ps, slot, step, ambient_);
            const PedWalkPath& walk = ped_paths_.path(walk_path);
            const float walk_distance = std::clamp(ph.dist_along_m / lane.length_m,
                                                    0.0f, 1.0f) * walk.line.length;
            const LanePose pose = walk.line.pose(walk_distance);
            const glm::vec2 d{pose.position.x - player_xz.x,
                              pose.position.z - player_xz.y};
            if (d.x * d.x + d.y * d.y > pa2) continue;

            PedAgent p;
            p.lane_key = lane.key;
            p.slot = slot;
            p.generation = lap;
            p.lane = walk.lane;
            p.walk_path = walk_path;
            p.dist_along_m = walk_distance;
            p.speed_mps = ph.speed_mps;
            p.base_lateral_m = walk.side * (lane.width_m * 0.5f + ambient_.sidewalk_offset_m)
                               - graph_->lane(walk.lane).lateral_offset_m;
            p.lateral_m = p.base_lateral_m;
            p.last_dist_m = walk_distance;
            // THE NERVE ROLL BELONGS TO THE SPAWNER, and city/pedestrian_
            // reactions.h says so at the top of the file: the [0,1) roll is
            // keyed to spawn identity through hash_coord(), never pulled off a
            // stream, because how many people were made before this one is a
            // fact about which way the player drove in. The lane key is split
            // across the two coordinate axes exactly as the driver profile
            // splits it, so the same person has the same nerve forever.
            //
            // It is the SAME disposition the punch reaction uses. There is one
            // hidden nerve per person and one place it is drawn.
            p.disposition = ped_react::disposition_from_nerve(
                city_unit_roll(map_seed_,
                               static_cast<int32_t>(static_cast<uint32_t>(lane.key)),
                               static_cast<int32_t>(static_cast<uint32_t>(lane.key >> 32)),
                               slot, kChannelPedNerve));
            // The phantom establishes identity and initial phase only. Active
            // walkers retain real path progress across schedule wraps.
            p.mode = AgentMode::Integrating;
            p.pos = pose.position;
            p.fwd = pose.tangent;
            p.spawn_ordinal = static_cast<uint32_t>(stats_.activated);
            peds_.push_back(p);
            ++stats_.activated;
        }
    }

    // Newcomers were appended in lane-scan order, which is a fact about the
    // scan and not about the population. Sort them and splice: the resulting
    // vector is ordered by identity alone, so two runs that instantiated the
    // same agents in different orders end up byte-identical here.
    if (vehicles_.size() > veh_have) {
        const auto mid = vehicles_.begin() + static_cast<long>(veh_have);
        std::sort(mid, vehicles_.end(),
                  [](const VehicleAgent& a, const VehicleAgent& b) {
                      return ident_less(Ident{a.lane_key, a.slot},
                                        Ident{b.lane_key, b.slot});
                  });
        std::inplace_merge(vehicles_.begin(), mid, vehicles_.end(),
                           [](const VehicleAgent& a, const VehicleAgent& b) {
                               return ident_less(Ident{a.lane_key, a.slot},
                                                 Ident{b.lane_key, b.slot});
                           });
    }
    if (peds_.size() > ped_have) {
        const auto mid = peds_.begin() + static_cast<long>(ped_have);
        std::sort(mid, peds_.end(), [](const PedAgent& a, const PedAgent& b) {
            return ident_less(Ident{a.lane_key, a.slot},
                              Ident{b.lane_key, b.slot});
        });
        std::inplace_merge(peds_.begin(), mid, peds_.end(),
                           [](const PedAgent& a, const PedAgent& b) {
                               return ident_less(Ident{a.lane_key, a.slot},
                                                 Ident{b.lane_key, b.slot});
                           });
    }

    dispatch_snowplows(player_xz);
    if (police_spawn_pending_) {
        dispatch_police(player_xz);
        police_spawn_pending_ = false;
    }

    // --- kerbside parking --------------------------------------------------
    //
    // Rebuilt wholesale, because there is nothing to keep: a parked car is a
    // pure function of (map_seed, lane key, slot), so the one that leaves the
    // radius and the one that comes back are the same bytes. That is exactly
    // what a phantom vehicle CANNOT claim once it has been simulated, which is
    // why that one is retired permanently and this one is not retired at all.
    ambient_parked_.clear();
    {
        const float pk2 = tuning_.parked_activate_m * tuning_.parked_activate_m;
        for (const LaneRef lr : lane_scratch_) {
            const ParkedLaneBay& bay = parked_bays_[lr];
            if (bay.slots == 0) continue;
            const Lane& lane = graph_->lane(lr);
            for (uint32_t slot = 0; slot < bay.slots; ++slot) {
                const ParkedSlot ps =
                    parked_slot(map_seed_, lane, bay, slot, ambient_);
                const LanePose pose =
                    graph_->pose(lr, ps.dist_along_m, ps.lateral_m);
                const glm::vec2 d{pose.position.x - player_xz.x,
                                  pose.position.z - player_xz.y};
                if (d.x * d.x + d.y * d.y > pk2) continue;
                AmbientParkedCar car;
                car.lane_key = lane.key;
                car.slot = slot;
                car.pos = pose.position;
                car.fwd = ps.reversed ? -pose.tangent : pose.tangent;
                car.kind = parked_vehicle_kind(lane.key, slot);
                ambient_parked_.push_back(car);
            }
        }
        // Sorted by identity for the same reason the active set is: the lane
        // scan is a fact about the scan, and anything downstream that sums or
        // indexes over this list must see the population instead.
        std::sort(ambient_parked_.begin(), ambient_parked_.end(),
                  [](const AmbientParkedCar& a, const AmbientParkedCar& b) {
                      return ident_less(Ident{a.lane_key, a.slot},
                                        Ident{b.lane_key, b.slot});
                  });
    }

    stats_.vehicles = vehicles_.size();
    stats_.peds = peds_.size();
    stats_.parked = ambient_parked_.size();
}

// ---------------------------------------------------------------------------
//  rebuild_buckets: freeze every cross-agent read for this step
// ---------------------------------------------------------------------------

void Crowd::rebuild_buckets() {
    if (!graph_) return;

    for (LaneRef lr : touched_lanes_) lane_buckets_[lr].clear();
    touched_lanes_.clear();
    for (std::vector<uint32_t>& heads : junction_heads_) heads.clear();

    junction_frozen_.resize(vehicles_.size());

    for (uint32_t i = 0; i < vehicles_.size(); ++i) {
        const VehicleAgent& v = vehicles_[i];
        junction_frozen_[i] = JunctionSnapshot{
            v.lane, v.dist_along_m, v.speed_mps, v.decisions,
            v.stop_junction, v.stop_wait_steps, v.stop_arrival_step,
            v.stop_completed,
            v.committed_junction, v.committed_approach_lane,
            v.committed_exit_lane,
            agent_planned_exit(*graph_, v, map_seed_, police_search_centre_,
                               police_wanted_level_ > 0),
            active_turn(*graph_, v),
            vehicle_engine_failed(v.mechanical), v.delay_seconds};
        if (!graph_->valid(v.lane) || v.chase_active) continue;
        const LanePose centre = graph_->pose(v.lane, v.dist_along_m);
        // A car changing lane stays in its ORIGIN bucket for the whole arc:
        // the follower behind it keeps following, and the lane it is moving
        // into sees it through emergency_obstacle_speed(), which predicts
        // along the arc. Dropping it out at 2.65 m of offset (the rule for
        // every other displaced body) would make it vanish from both lanes
        // for the middle of the move.
        if ((v.maneuver.active() || std::fabs(v.roadside_offset_m) > 0.01f) &&
            v.maneuver.kind != TrafficManeuverKind::LaneChange &&
            std::fabs(glm::dot(v.pos - centre.position, centre.right)) >
                tuning_.traffic_half_width_m * 2.0f + 0.35f) continue;
        std::vector<BucketEntry>& b = lane_buckets_[v.lane];
        if (b.empty()) touched_lanes_.push_back(v.lane);
        b.push_back(BucketEntry{v.dist_along_m, i});
    }

    leader_gap_.assign(vehicles_.size(), kInf);
    leader_speed_.assign(vehicles_.size(), kInf);
    leader_index_.assign(vehicles_.size(), 0xFFFFFFFFu);
    for (LaneRef lr : touched_lanes_) {
        std::vector<BucketEntry>& b = lane_buckets_[lr];
        // Ties broken on agent index, which is itself ordered by identity, so
        // two cars stopped at exactly the same distance still have a defined
        // leader-follower relationship rather than whichever std::sort picked.
        std::sort(b.begin(), b.end(), [](const BucketEntry& a, const BucketEntry& c) {
            if (a.dist != c.dist) return a.dist < c.dist;
            return a.agent < c.agent;
        });
        for (std::size_t j = 0; j + 1 < b.size(); ++j) {
            leader_gap_[b[j].agent] = b[j + 1].dist - b[j].dist;
            leader_speed_[b[j].agent] = vehicles_[b[j + 1].agent].speed_mps;
            leader_index_[b[j].agent] = b[j + 1].agent;
        }
        // The last car on a lane looks into the lane it is about to enter. One
        // extra bucket probe, and without it every car at the head of a queue
        // accelerates into the back of the queue on the far side of the
        // junction.
        if (!b.empty()) {
            const BucketEntry& head = b.back();
            const VehicleAgent& v = vehicles_[head.agent];
            const Lane& lane = graph_->lane(v.lane);
            const float to_end = lane.length_m - head.dist;
            float head_lookahead = tuning_.junction_lookahead_m;
            const auto control = graph_->junction_control(lane.junction_to);
            if (control == JunctionControl::Yield ||
                control == JunctionControl::PriorityStop) {
                // A minor-road driver needs to see a usable gap several
                // seconds away, including fast traffic beyond the old 18 m
                // stop-line scan. Still only one frozen head per lane.
                head_lookahead = std::max(head_lookahead,
                    std::max(v.speed_mps, lane.speed_limit_mps) * 12.0f);
            }
            if (lane.speed_limit_mps > 20.0f) {
                head_lookahead = std::max(
                    head_lookahead,
                    v.speed_mps * v.speed_mps /
                            (2.0f * std::max(0.1f, v.profile.brake)) +
                        tuning_.junction_storage_margin_m);
            }
            if (lane.junction_to < junction_heads_.size() &&
                to_end - junction_clearance(lane.junction_to) <=
                    head_lookahead) {
                junction_heads_[lane.junction_to].push_back(head.agent);
            }
            const std::vector<TurnLink>& outs = graph_->outgoing(v.lane);
            if (!outs.empty()) {
                const LaneRef nxt = agent_planned_exit(
                    *graph_, v, map_seed_, police_search_centre_,
                    police_wanted_level_ > 0);
                if (graph_->valid(nxt) && !lane_buckets_[nxt].empty()) {
                    const float remain = graph_->lane(v.lane).length_m - head.dist;
                    const uint32_t leader_index =
                        lane_buckets_[nxt].front().agent;
                    const VehicleAgent& leader = vehicles_[leader_index];
                    // Logical ownership changes to the outgoing lane when a
                    // turn starts, but the body is still on the curve before
                    // the node. Measuring `remain + outgoing distance` then
                    // puts that leader roughly one junction-clearance too far
                    // ahead and lets a freeway follower rear-end it. When it
                    // is the continuation of this exact lane, measure from the
                    // curve entry and its real progress instead.
                    if (active_turn(*graph_, leader) &&
                        leader.turn_from_lane == v.lane &&
                        leader.lane == nxt) {
                        const float entry_dist = std::max(
                            0.0f, graph_->lane(v.lane).length_m -
                                      leader.turn_entry_m);
                        leader_gap_[head.agent] = std::max(
                            0.0f, entry_dist - head.dist) +
                            leader.turn_progress_m;
                    } else {
                        leader_gap_[head.agent] =
                            remain + lane_buckets_[nxt].front().dist;
                    }
                    leader_speed_[head.agent] = leader.speed_mps;
                    leader_index_[head.agent] = leader_index;
                }
            }
        }
    }

    // A dying auxiliary lane is a real merge, not two unrelated lanes that
    // happen to cross at the taper tip. Once their centres begin converging,
    // cars in either lane must see the nearest car ahead in the other lane.
    // That gives the pair the full taper to form one queue instead of waiting
    // for the body solver to discover an overlap at the parapet end.
    constexpr float kAuxMergeLookAcrossM = 4.75f;
    auto publish_cross_lane_leader = [&](const BucketEntry& follower,
                                         const BucketEntry& leader) {
        const float centre_gap = leader.dist - follower.dist;
        if (centre_gap < 0.0f || centre_gap >= leader_gap_[follower.agent])
            return;
        leader_gap_[follower.agent] = centre_gap;
        leader_speed_[follower.agent] = vehicles_[leader.agent].speed_mps;
        leader_index_[follower.agent] = leader.agent;
    };
    for (LaneRef dying_ref : touched_lanes_) {
        const Lane& dying = graph_->lane(dying_ref);
        if (dying.lanes_at_start <= dying.lanes_at_end ||
            dying.index < dying.lanes_at_end)
            continue;

        LaneRef through_ref = kInvalidLane;
        for (LaneRef candidate : graph_->lanes_of_edge(dying.edge)) {
            const Lane& through = graph_->lane(candidate);
            if (through.forward == dying.forward &&
                through.index + 1 == dying.lanes_at_end &&
                through.index + 1 == dying.index) {
                through_ref = candidate;
                break;
            }
        }
        if (!graph_->valid(through_ref)) continue;
        const Lane& through = graph_->lane(through_ref);
        const std::vector<BucketEntry>& dying_bucket = lane_buckets_[dying_ref];
        const std::vector<BucketEntry>& through_bucket = lane_buckets_[through_ref];
        if (dying_bucket.empty() || through_bucket.empty()) continue;

        auto centres_are_merging = [&](float dist) {
            const float t = std::clamp(
                dist / std::max(1e-6f, dying.length_m), 0.0f, 1.0f);
            const float dying_offset = glm::mix(
                dying.lateral_offset_start_m,
                dying.lateral_offset_end_m, t);
            const float through_offset = glm::mix(
                through.lateral_offset_start_m,
                through.lateral_offset_end_m, t);
            return std::fabs(dying_offset - through_offset) <=
                   kAuxMergeLookAcrossM;
        };
        for (const BucketEntry& follower : dying_bucket) {
            if (!centres_are_merging(follower.dist)) continue;
            const auto leader = std::lower_bound(
                through_bucket.begin(), through_bucket.end(), follower.dist,
                [](const BucketEntry& entry, float dist) {
                    return entry.dist < dist;
                });
            if (leader != through_bucket.end())
                publish_cross_lane_leader(follower, *leader);
        }
        for (const BucketEntry& follower : through_bucket) {
            if (!centres_are_merging(follower.dist)) continue;
            const auto leader = std::upper_bound(
                dying_bucket.begin(), dying_bucket.end(), follower.dist,
                [](float dist, const BucketEntry& entry) {
                    return dist < entry.dist;
                });
            if (leader != dying_bucket.end())
                publish_cross_lane_leader(follower, *leader);
        }
    }

    // A car already through the lane boundary still owns the junction until
    // its rear bumper has cleared. This is the bit that stops cross traffic
    // launching into a vehicle which technically changed to its outgoing lane
    // one step ago but is still physically inside the box.
    for (uint32_t i = 0; i < junction_frozen_.size(); ++i) {
        const JunctionSnapshot& s = junction_frozen_[i];
        if (s.committed_junction >= junction_heads_.size() ||
            !graph_->valid(s.lane))
            continue;
        const Lane& lane = graph_->lane(s.lane);
        const bool approaching_box =
            // A self-loop's destination is also its origin. Only its actual
            // approach lane may hold an approach claim; a departing loop car
            // must release the box once its rear clears, not after a full lap.
            lane.junction_to == s.committed_junction &&
            s.lane == s.committed_approach_lane;
        const bool clearing_box =
            lane.junction_from == s.committed_junction &&
            s.dist_along_m < junction_clearance(s.committed_junction);
        if (!approaching_box && !clearing_box && !s.active_turn) continue;
        std::vector<uint32_t>& heads =
            junction_heads_[s.committed_junction];
        if (std::find(heads.begin(), heads.end(), i) == heads.end())
            heads.push_back(i);
    }

    // --- pedestrian neighbour grid ----------------------------------------
    // A spatial hash sized to the POPULATION, not to the area: a dense grid
    // over the active box is fine at a 160 m radius and is nine megabytes of
    // memset per step at a kilometre.
    const std::size_t np = peds_.size();
    ped_pos_frozen_.resize(np);
    for (std::size_t i = 0; i < np; ++i)
        ped_pos_frozen_[i] = glm::vec2{peds_[i].pos.x, peds_[i].pos.z};

    std::size_t table = 64;
    while (table < np * 2u) table <<= 1;
    ped_table_ = table;

    ped_cell_keys_.resize(np);
    ped_bucket_of_.resize(np);
    ped_bucket_items_.resize(np);
    ped_bucket_start_.assign(table + 1, 0u);

    for (std::size_t i = 0; i < np; ++i) {
        const int32_t cx = floor_div(ped_pos_frozen_[i].x, PED_SEPARATION_RADIUS);
        const int32_t cz = floor_div(ped_pos_frozen_[i].y, PED_SEPARATION_RADIUS);
        ped_cell_keys_[i] = (static_cast<int64_t>(cx) << 32) |
                            static_cast<int64_t>(static_cast<uint32_t>(cz));
        const uint32_t b =
            static_cast<uint32_t>(cell_hash(cx, cz) & (table - 1u));
        ped_bucket_of_[i] = b;
        ++ped_bucket_start_[b + 1];
    }
    for (std::size_t b = 0; b < table; ++b)
        ped_bucket_start_[b + 1] += ped_bucket_start_[b];

    // Scattered by ascending ped index, and index order IS identity order, so a
    // bucket's contents come out ordered by identity. ped_separation() SUMS a
    // push vector over them, float addition is not associative, and that is the
    // whole reason this scatter has to be stable.
    std::vector<uint32_t> cursor(ped_bucket_start_.begin(),
                                 ped_bucket_start_.end() - 1);
    for (std::size_t i = 0; i < np; ++i)
        ped_bucket_items_[cursor[ped_bucket_of_[i]]++] = static_cast<uint32_t>(i);
}

// ---------------------------------------------------------------------------
//  step_vehicles
// ---------------------------------------------------------------------------

// FREE-DRIVE PURSUIT.
//
// Everything else in this file makes a cruiser a better piece of TRAFFIC — it
// runs the lights, it jumps the queue, it corners harder. None of it can make
// the cruiser leave the road, because a lane-following agent is defined by the
// lane. That is the whole of "they stay on the road and never actually chase
// me": inside the last sixty metres a police car in this genre stops using the
// road network at all and drives at you across whatever is in between.
//
// police_terminal_pursuit_cmd is the command kernel for exactly this. It came
// across with the rest of the police lift and was never called by anything —
// only the lane-following half of that port was ever wired up, which is why a
// pursuit here has always been a commute with a siren on.
//
// While engaged the agent is stepped by step_vehicle: the same physics, the
// same collider, the same fixed dt as the player's own car. Its lane fields go
// stale for the duration and are re-anchored on release.
bool Crowd::step_police_free_chase(VehicleAgent& v, float dt) {
    // A suspect ON FOOT is not something you drive at. That case belongs to
    // the officer: the cruiser brakes, the door opens and somebody walks after
    // you. Free-driving here would run the phase machine's dismount inputs off
    // stale lane fields and simply drive over the arrest.
    const bool engaged = v.police_unit && v.police_pursuit &&
                         police_wanted_level_ > 0 &&
                         !police_target_on_foot_ &&
                         police_officer_driving_allowed(v.officer) &&
                         !vehicle_engine_failed(v.mechanical);
    const auto release = [&]() {
        if (!v.chase_active) return;
        v.chase_active = false;
        // Back onto the network at whatever lane this car is actually on now.
        // Without this the agent keeps a lane reference from wherever it left
        // the road, and rejoins by teleporting to it.
        const glm::vec3 forward = vehicle_forward(v.chase);
        const auto snap = graph_->nearest_lane_along(
            {v.pos.x, v.pos.z}, {forward.x, forward.z}, 90.0f);
        if (snap.valid()) {
            v.lane = snap.lane;
            v.dist_along_m = v.last_dist_m = snap.dist_along_m;
            v.cruise_mps = std::min(v.cruise_mps,
                                    graph_->lane(snap.lane).speed_limit_mps);
            // REJOIN ON AN ARC, NOT ON A FRAME. Handing the road back used to
            // assign the lane and let the next step read the pose straight off
            // it, which teleports the heading from wherever the car was
            // actually pointing to the lane tangent — measured at 25 degrees
            // inside one step, and the eye reads that as the car twitching.
            // The maneuver system already exists to carry a body from a real
            // pose onto a lane, so use it: the curve holds the heading while
            // the car drives back into line.
            const Lane& home = graph_->lane(snap.lane);
            const float rejoin_end = std::min(home.length_m,
                                              snap.dist_along_m + 14.0f);
            // Only a MOVING car needs the arc. A stopped one has no visible
            // heading pop worth smoothing, and stopped is exactly the case
            // where the officer is about to get out — see the deadlock note on
            // the cancel in step_emergency_maneuver().
            if (v.speed_mps > 3.0f && rejoin_end > snap.dist_along_m + 1.0f) {
                const glm::vec3 right{-forward.z, 0.0f, forward.x};
                const LanePose from{v.pos, forward, right};
                const LanePose to = graph_->pose(snap.lane, rejoin_end);
                TrafficManeuver rejoin;
                rejoin.kind = TrafficManeuverKind::MergeBack;
                rejoin.destination = snap.lane;
                rejoin.end_station_m = rejoin_end;
                rejoin.end_offset_m = 0.0f;
                rejoin.speed_limit_mps = std::max(6.0f, v.speed_mps);
                rejoin.curves[0] = traffic_steering_curve(from, to);
                rejoin.count = 1;
                rejoin.length_m = rejoin.curves[0].length_m;
                if (rejoin.length_m > 1.0f) v.maneuver = rejoin;
            }
        }
        v.mode = AgentMode::Integrating;
        v.collision_offset_xz = {0.0f, 0.0f};
        v.collision_velocity_xz = {0.0f, 0.0f};
        v.roadside_offset_m = 0.0f;
        v.police_route.clear();
        v.police_route_index = 0;
        v.police_last_replan_step = -1;
        v.committed_junction = 0xFFFFFFFFu;
        v.committed_approach_lane = kInvalidLane;
        v.committed_exit_lane = kInvalidLane;
        v.turn_from_lane = kInvalidLane;
    };
    if (!engaged || !police_officer_world_ || !graph_) { release(); return false; }
    if (!v.chase_active && v.chase_cooldown_s > 0.0f) {
        v.chase_cooldown_s = std::max(0.0f, v.chase_cooldown_s - dt);
        return false;
    }

    const glm::vec2 here = v.chase_active
        ? glm::vec2{v.chase.position.x, v.chase.position.z}
        : glm::vec2{v.pos.x, v.pos.z};
    const float range = glm::distance(here, police_search_centre_);
    if (!v.chase_active) {
        // OFF THE ROAD IS THE WHOLE POINT OF FREE DRIVING. Inside the lane
        // network the hand-off is a short contact move; once the suspect has
        // left the network there is nothing to route to, so the cruiser has to
        // leave it too or it just parks on the nearest tarmac and waits.
        const float engage = police_target_offroad_m_ > 5.0f
            ? tuning_.police.free_chase_release
            : tuning_.police.free_chase_range;
        if (range > engage) return false;
        // Never leave the road for a suspect this unit cannot actually see.
        // Aiming is the whole of the free-drive controller, so a building in
        // between does not become a detour — it becomes the thing it drives
        // into. The lane path is what gets you around a corner.
        if (!police_has_line_of_sight(v)) return false;
        // Hand over from the pose the lane path was already showing, so the
        // switch is invisible: seeding from anything else pops the car.
        // TWO CONVENTIONS FOR "WHERE THE CAR IS", and they differ by a ride
        // height. VehicleAgent::pos is the road surface — traffic_visual lifts
        // the body onto its wheels from there — while VehicleState::position
        // is the chassis origin, sitting up on its springs. Seed one from the
        // other without this and the cruiser spawns half a metre into the
        // tarmac; write it back without it and the renderer adds the body
        // offset on top of a chassis height and the car floats.
        const float ride = vehicle_rest_ride_height(police_vehicle_tuning_);
        v.chase = VehicleState{};
        v.chase.position = v.pos + glm::vec3{0.0f, ride, 0.0f};
        glm::vec3 forward{v.fwd.x, 0.0f, v.fwd.z};
        forward = glm::length(forward) > 1e-5f ? glm::normalize(forward)
                                               : glm::vec3{0.0f, 0.0f, -1.0f};
        // quatLookAt maps LOCAL -Z onto the direction it is given, and
        // vehicle_forward() is orientation * (0,0,-1) — so this takes `forward`
        // directly. Passing -forward seeds the car facing exactly backwards,
        // which is a 180-degree flip on the very step the hand-off happens and
        // then reads as "the target is behind me" forever after: the cruiser
        // spends the whole engagement trying to turn around from a spin it
        // never actually performed.
        v.chase.orientation = glm::quatLookAt(forward, glm::vec3{0, 1, 0});
        v.chase.velocity = forward * std::max(0.0f, v.speed_mps);
        v.chase_active = true;
        v.chase_stall_s = 0.0f;
        v.chase_reverse_s = 0.0f;
        v.chase_turnaround_s = 0.0f;
        v.maneuver = {};
        v.roadside_offset_m = 0.0f;
        v.collision_offset_xz = {0.0f, 0.0f};
        v.collision_velocity_xz = {0.0f, 0.0f};
    } else if (range > tuning_.police.free_chase_release) {
        release();
        return false;
    }

    const glm::vec3 forward3 = vehicle_forward(v.chase);
    const glm::vec2 forward{forward3.x, forward3.z};
    const glm::vec2 right{-forward.y, forward.x};
    // Aim at the CENTRE: the live target while it is in view, the last-seen
    // point once it is not. A free-driving cruiser that lost sight aims at
    // the corner, not through the wall.
    const glm::vec2 to_target = police_search_centre_ - here;
    const float distance = glm::length(to_target);
    const glm::vec2 direction = distance > 1e-4f ? to_target / distance : forward;
    const float ahead_dot = glm::dot(forward, direction);
    const float side_dot = glm::dot(right, direction);
    const float forward_speed = vehicle_forward_speed(v.chase);
    const bool turning_around = ahead_dot < -0.25f && forward_speed <= 5.0f;
    if (!turning_around) {
        v.chase_reverse_s = 0.0f;
        v.chase_turn_stall_s = 0.0f;
    }
    // BOXED IN IS SOMETHING YOU FIND OUT, NOT SOMETHING YOU PROBE FOR. A ray
    // cast down the nose was tried and it reads a rising kerb, a slope and the
    // ground itself as walls — it declared the car blocked almost everywhere,
    // sent it into reverse, and the reverse cap then switched free driving off
    // entirely (measured: off-road time 14% -> 3%, mean gap 53 m -> 95 m).
    // Trying the forward arc and noticing it went nowhere is both cheaper and
    // right, and it is what a driver does.
    const bool forward_blocked = v.chase_reverse_s > 0.0f;
    // THE CONTACT CAP, free-driving (PoliceTuning::contact_closing_mps). The
    // terminal command floors it at anything lined up inside 8 m — that is the
    // ram — and with a real engine behind it from 22 m out, nothing held the
    // hit under what the engine could reach. Measured bumper to bumper along
    // this car's own nose. While searching the centre is a point, not a car.
    const float suspect_along = police_searching_
        ? 0.0f : glm::dot(police_target_velocity_, forward);
    const PursuitCmd cmd = police_limit_contact_closing(
        police_terminal_pursuit_cmd(distance, ahead_dot, side_dot,
                                    forward_speed, forward_blocked),
        ahead_dot, forward_speed,
        police_contact_speed_mps(suspect_along,
                                 distance * ahead_dot - tuning_.car_length_m,
                                 tuning_.police));

    InputFrame intent;
    intent.steer = cmd.steer;
    // The gearbox is arcade: the brake axis IS reverse once the car has stopped,
    // so a negative pursuit throttle becomes brake and the physics does the rest.
    intent.throttle = std::max(0.0f, cmd.throttle);
    intent.brake = cmd.throttle < 0.0f ? -cmd.throttle : cmd.brake;
    intent.handbrake = cmd.handbrake ? 1.0f : 0.0f;

    v.chase = step_vehicle(v.chase, police_vehicle_tuning_, intent,
                           *police_officer_world_, dt);
    v.pos = v.chase.position -
            glm::vec3{0.0f, vehicle_rest_ride_height(police_vehicle_tuning_), 0.0f};
    v.fwd = forward3;
    v.speed_mps = std::max(0.0f, vehicle_forward_speed(v.chase));
    v.mode = AgentMode::Integrating;
    v.delay_seconds = 0.0f;

    // Pinned. Asking for throttle and not moving means the aim has walked this
    // car into something it cannot see; give the road back rather than sit
    // there revving, and stay on it long enough not to repeat the mistake.
    // A turn-around has its OWN escape (the reverse leg below), so a stall
    // while swinging the nose round must not be read as "pinned, give up".
    // Conflating the two is what collapsed free driving to 3% of the chase:
    // every attempted turn released the car back onto the lane graph.
    if (!turning_around && intent.throttle > 0.1f &&
        std::fabs(v.speed_mps) < 1.5f)
        v.chase_stall_s += dt;
    else if (!turning_around) v.chase_stall_s = 0.0f;

    // The three-point turn. Swing the nose round forwards first; only when
    // that has visibly failed does the car back up, and backing up is a leg of
    // a turn rather than a way of getting anywhere — past the cap it hands
    // back to the lane router, which can plan a way round the block.
    const float travelling = glm::length(
        glm::vec2{v.chase.velocity.x, v.chase.velocity.z});
    if (turning_around && !forward_blocked) {
        // Swinging the nose round forwards. If that goes nowhere the car is
        // boxed in, and only then does it back up.
        if (travelling < 1.0f) v.chase_turn_stall_s += dt;
        else v.chase_turn_stall_s = 0.0f;
        if (v.chase_turn_stall_s > 0.8f) {
            v.chase_reverse_s = 1e-3f;
            v.chase_turn_stall_s = 0.0f;
        }
    } else if (forward_blocked) {
        v.chase_reverse_s += dt;
        // Nose is round, or backing up has stopped achieving anything: either
        // way the reverse leg is over. Reversing is ONE LEG OF A TURN here,
        // never a way of getting anywhere.
        if (ahead_dot > -0.1f ||
            (v.chase_reverse_s > 0.3f && travelling < 0.6f))
            v.chase_reverse_s = 0.0f;
    }

    // FREE DRIVING IS A CLOSING MOVE, NOT A MANOEUVRING ONE. Once the nose is
    // off the target this controller can only shuffle, and shuffling in front
    // of the player is precisely what "they just reverse instead of turning
    // around" looks like. The lane router already knows how to turn a cruiser
    // round properly — PoliceTurnaround, or a route round the block — so hand
    // the road back and let it, rather than three-point-turning in the street.
    if (turning_around) v.chase_turnaround_s += dt;
    else v.chase_turnaround_s = 0.0f;

    const bool pinned = v.chase_stall_s > 1.5f;
    // Giving up the road only helps when the router can do better. Right on
    // top of the player it cannot: handing back there just puts the cruiser
    // into the same queue it was in, and measured on the jammed district that
    // cost 26 m of mean gap against 80 m. Close in, keep working the turn.
    const bool gave_up_turning = distance > 22.0f &&
        (v.chase_reverse_s > 2.0f || v.chase_turnaround_s > 3.0f);
    if (pinned || gave_up_turning) {
        release();
        v.chase_stall_s = 0.0f;
        v.chase_reverse_s = 0.0f;
        v.chase_turn_stall_s = 0.0f;
        v.chase_turnaround_s = 0.0f;
        // Being PINNED means this car drove into something it cannot see, and
        // it will do it again the moment it is let back out — hold it on the
        // road. Failing to come round is not the same mistake: the router puts
        // the nose back on the target and the hand-off is welcome again almost
        // immediately.
        v.chase_cooldown_s = pinned ? 5.0f : 1.5f;
        return false;
    }
    return true;
}

void Crowd::step_vehicles(int64_t step, const VehicleState* player,
                          const OnFootTrafficHazard* on_foot_player) {
    if (!graph_) return;
    police_shots_.clear();
    on_foot_player_hits_.clear();
    prepare_emergency_maneuvers(step, player, on_foot_player);
    const int k = std::max(1, tuning_.vehicle_sub_rate);
    const float dt = static_cast<float>(k) * kSimDtF;
    const float convoy_car_length = snowplow_unit_count() > 0
        ? std::max(tuning_.car_length_m, 6.6f) : tuning_.car_length_m;
    const float min_gap_floor = convoy_car_length + 0.6f;

    stats_.vehicles_stepped = 0;
    stats_.vehicles_analytic = 0;
    stats_.player_hazards = 0;
    stats_.junction_yields = 0;
    stats_.stop_holds = 0;
    stats_.junction_box_holds = 0;
    stats_.jam_reroutes = 0;
    stats_.stalled_vehicles = 0;
    stats_.intersection_escapes = 0;
    stats_.ai_collision_pairs = 0;
    stats_.ai_collisions = 0;

    auto required_stop_steps = [&](const DriverProfile& profile) {
        float scale = 1.0f;
        switch (profile.kind) {
            case DriverProfileKind::Cautious:       scale = 1.20f; break;
            case DriverProfileKind::Normal:         scale = 1.00f; break;
            case DriverProfileKind::Impatient:      scale = 0.75f; break;
            case DriverProfileKind::AggressiveLite: scale = 0.55f; break;
        }
        return static_cast<int64_t>(
            std::ceil(static_cast<double>(tuning_.stop_dwell_steps) * scale));
    };

    auto exit_ready = [&](uint32_t index, LaneRef exit_lane) {
        if (index >= junction_frozen_.size() || !graph_->valid(exit_lane)) return false;
        const auto& snapshot = junction_frozen_[index];
        if (!graph_->valid(snapshot.lane)) return false;
        const auto& approach = graph_->lane(snapshot.lane);
        // STORAGE, NOT CONFLICT. This asks "is there physical room past the
        // box for my car", so the reserved head of the exit lane is the plain
        // junction box. It used to use junction_clearance(), which is the
        // acute-merge CONFLICT envelope: at the Spine/Sycamore junction two
        // arms sit 11.5 degrees apart and their corridors only cross 58 m out,
        // so that envelope is 72 m against a 17 m box. Requiring 72 m of empty
        // exit before entering starved the junction -- a healthy lead waited
        // 49.6 s across 17,387 steps of GREEN, because ambient spacing is 48 m
        // and the exit is 160-180 m long, so the room asked for was almost
        // never there. Serializing the merge is still junction_clearance()'s
        // job, through movement conflicts and box ownership, and it is
        // unchanged; this one call was the only place the two were conflated.
        const float clear = junction_turn_clearance(approach.junction_to);
        const auto& bucket = lane_buckets_[exit_lane];
        float nearest = bucket.empty() ? kInf : bucket.front().dist;
        // Admission needs space that exists in the frozen state. Predicting a
        // moving leader several seconds into the future admits a follower even
        // when that leader brakes on the next step. The committed-car escape
        // rule then forces the follower through it. This happened on every
        // Route 1 segment boundary, where resident phantoms and arriving cars
        // share the successor lane. Wait for the leader to physically clear.
        // Signal phases are the narrow exception: a moving platoon leader gets
        // under a second of conservative travel credit, which lets the green
        // drain without restoring the old four-second guess.
        if (!bucket.empty() &&
            graph_->junction_control(approach.junction_to) ==
                JunctionControl::Signal) {
            const uint32_t leader = bucket.front().agent;
            if (leader < junction_frozen_.size()) {
                nearest += std::max(
                    0.0f, junction_frozen_[leader].speed_mps - 1.0f) * 0.85f;
            }
        }
        return traffic_exit_has_storage(nearest, clear, convoy_car_length,
            effective_min_gap(vehicles_[index].profile, min_gap_floor),
            tuning_.junction_storage_margin_m);
    };

    auto approach_view = [&](uint32_t index, uint32_t junction,
                             int64_t arrival_override) {
        TrafficApproachView out;
        if (index >= junction_frozen_.size()) return out;
        const JunctionSnapshot& s = junction_frozen_[index];
        const VehicleAgent& a = vehicles_[index];
        out.valid = true;
        if (s.committed_junction == junction && graph_->valid(s.lane)) {
            const Lane& claim_lane = graph_->lane(s.lane);
            out.committed = (claim_lane.junction_to == junction &&
                             s.lane == s.committed_approach_lane) ||
                            (claim_lane.junction_from == junction &&
                             s.dist_along_m < junction_clearance(junction)) ||
                            s.active_turn;
        }
        out.lane_key = a.lane_key;
        out.slot = a.slot;
        out.arrival_step = arrival_override >= 0
                               ? arrival_override : s.stop_arrival_step;
        if (out.committed) {
            out.priority = 100;
            out.eta_seconds = 0.0f;
            return out;
        }
        if (!graph_->valid(s.lane)) {
            out.valid = false;
            return out;
        }
        const Lane& approach = graph_->lane(s.lane);
        const TurnLink* turn = turn_link_to(*graph_, s.lane,
                                           s.planned_exit_lane, a.police_pursuit);
        out.priority = turn ? turn_priority_rank(turn->priority) : 0;
        if (graph_->junction_control(junction) == JunctionControl::Signal &&
            out.arrival_step >= 0) {
            // Road priority is not permission to starve a waiting green lane.
            // After a complete missed cycle, age beats a fresh arrival. Only
            // conflicting, storage-ready movements reach this compare. Old red
            // queues may reserve a drain interval, but cannot enter on red.
            const int64_t cycles = std::clamp<int64_t>(
                (step - out.arrival_step) /
                    std::max<int64_t>(1, tuning_.signal_period_steps), 0, 20);
            out.priority += static_cast<int>(cycles) * 4;
        }
        const float remaining = std::max(0.0f, approach.length_m - s.dist_along_m);
        out.eta_seconds = remaining / std::max(s.speed_mps, 1.0f);
        const auto control = graph_->junction_control(junction);
        if (control == JunctionControl::Yield ||
            control == JunctionControl::PriorityStop) {
            const auto driver = traffic_driver_after_wait(a.profile, s.delay_seconds);
            const float clear = junction_clearance(junction);
            // Predict priority traffic accelerating, not holding its current
            // low speed. Otherwise a stopped lead looks falsely far away.
            out.eta_seconds = traffic_travel_seconds(remaining - clear,
                s.speed_mps, driver.accel, std::max(a.cruise_mps, s.speed_mps));
            const float cap = std::min(std::max(0.5f, a.cruise_mps),
                turn ? turn_speed_limit(turn->kind) : 6.0f);
            out.clearance_seconds = out.eta_seconds +
                traffic_travel_seconds(clear * 2.0f + tuning_.car_length_m,
                    s.speed_mps, driver.accel, cap) +
                traffic_gap_margin_seconds(a.profile, s.delay_seconds);
        }
        return out;
    };

    for (uint32_t i = 0; i < vehicles_.size(); ++i) {
        VehicleAgent& v = vehicles_[i];
        if (v.mode == AgentMode::Analytic) ++stats_.vehicles_analytic;
        if (!graph_->valid(v.lane)) continue;
        if (static_cast<int>(step % k) !=
            sub_rate_phase(v.lane_key, v.slot, i, v.spawn_ordinal, k))
            continue;
        ++stats_.vehicles_stepped;
        step_vehicle_mechanical(v.mechanical,v.body_damage,dt,
            splitmix64_mix(map_seed_ ^ v.lane_key ^ (static_cast<uint64_t>(v.slot)<<32)));

        // A cruiser that has left the road owes none of the lane path below.
        if (step_police_free_chase(v, dt)) continue;

        const Lane& lane = graph_->lane(v.lane);
        const bool turning = active_turn(*graph_, v);
        const bool committed_approach =
            v.committed_junction != 0xFFFFFFFFu &&
            v.lane == v.committed_approach_lane &&
            lane.junction_to == v.committed_junction &&
            v.dist_along_m >= lane.length_m -
                junction_clearance(v.committed_junction);
        const bool committed_departure =
            v.committed_junction != 0xFFFFFFFFu &&
            lane.junction_from == v.committed_junction &&
            v.dist_along_m < junction_clearance(v.committed_junction);
        const bool clearing_intersection =
            committed_approach || committed_departure ||
            (turning && v.committed_junction != 0xFFFFFFFFu);
        if (clearing_intersection &&
            v.speed_mps < tuning_.intersection_stall_speed_mps) {
            v.intersection_stall_steps += k;
        } else if (!clearing_intersection ||
                   v.speed_mps >= tuning_.intersection_min_clear_speed_mps) {
            v.intersection_stall_steps = 0;
        }
        const bool intersection_escape = clearing_intersection &&
            v.intersection_stall_steps >=
                std::max<int64_t>(1, tuning_.intersection_escape_after_steps);
        const LanePose recovery_pose = route_pose(*graph_, v);
        const float logical_dist_before_recovery = v.dist_along_m;
        step_collision_reaction(v, tuning_, recovery_pose, dt,
                                clearing_intersection);
        if (v.maneuver.active()) {
            v.maneuver.progress_m = std::clamp(v.maneuver.progress_m +
                v.dist_along_m - logical_dist_before_recovery, 0.f, v.maneuver.length_m);
        }
        if (step_emergency_maneuver(i, dt)) {
            apply_collision_pose(v, route_pose(*graph_, v));
            continue;
        }
        if (turning) {
            // Collision recovery normally turns longitudinal displacement
            // into lane progress. During a curve the outgoing lane distance
            // is only a bookkeeping projection, so apply those metres to the
            // actual curve and rebuild the projection from its ratio.
            const float reaction_progress =
                v.dist_along_m - logical_dist_before_recovery;
            v.turn_progress_m = std::clamp(
                v.turn_progress_m + reaction_progress,
                0.0f, v.turn_length_m);
            v.dist_along_m = v.turn_exit_m *
                (v.turn_progress_m / v.turn_length_m);
        }
        if (v.committed_junction != 0xFFFFFFFFu) {
            const bool still_approaching =
                lane.junction_to == v.committed_junction &&
                v.lane == v.committed_approach_lane;
            const bool still_clearing =
                lane.junction_from == v.committed_junction &&
                v.dist_along_m < junction_clearance(v.committed_junction);
            if (!still_approaching && !still_clearing && !turning) {
                v.committed_junction = 0xFFFFFFFFu;
                v.committed_approach_lane = kInvalidLane;
                v.committed_exit_lane = kInvalidLane;
            }
        }
        const float to_end = lane.length_m - v.dist_along_m;
        const float stop_distance = junction_clearance(lane.junction_to);
        const float slack = to_end - stop_distance;
        const TurnLink* turn = turning
            ? nullptr
            : agent_planned_turn(*graph_, v, map_seed_, police_search_centre_,
                                 police_wanted_level_ > 0);
        bool seamless_continuation = false;
        if (turn && graph_->valid(turn->to) &&
            lane.junction_to < graph_->junction_count() &&
            graph_->junction(lane.junction_to).degree == 2 &&
            graph_->junction_control(lane.junction_to) ==
                JunctionControl::None &&
            turn->kind == TurnKind::Straight) {
            const LanePose from = graph_->pose(v.lane, lane.length_m);
            const LanePose to = graph_->pose(turn->to, 0.0f);
            const glm::vec2 seam{from.position.x - to.position.x,
                                 from.position.z - to.position.z};
            const glm::vec2 from_dir = glm::normalize(
                glm::vec2{from.tangent.x, from.tangent.z});
            const glm::vec2 to_dir = glm::normalize(
                glm::vec2{to.tangent.x, to.tangent.z});
            seamless_continuation = glm::dot(seam, seam) < 0.25f &&
                                    glm::dot(from_dir, to_dir) > 0.97f;
        }

        // --- what would slow me down ---------------------------------------
        const float gap = leader_gap_[i] - convoy_car_length;
        DriverProfile prof = traffic_driver_after_wait(v.profile, v.delay_seconds);
        prof.min_gap = effective_min_gap(prof, min_gap_floor);
        const float pursuit_cruise = police_pursuit_cruise_mps(
            lane.speed_limit_mps, police_target_speed_mps_,
            police_wanted_level_);
        const bool engaged_pursuit = v.police_pursuit &&
            police_wanted_level_ > 0 &&
            police_officer_driving_allowed(v.officer);
        const float desired_cruise = v.police_pursuit
            ? std::max(v.cruise_mps, pursuit_cruise)
            : v.cruise_mps;
        float target = std::min(desired_cruise,
                                traffic_follow_speed_for_gap(gap, prof));
        if (i < leader_speed_.size() && std::isfinite(leader_speed_[i])) {
            const float leader_speed = std::max(0.0f, leader_speed_[i]);
            const float closing_speed = std::max(0.0f, v.speed_mps - leader_speed);
            const float braking_room = std::max(
                0.0f, gap - prof.min_gap - closing_speed);
            const float braking_limit = std::sqrt(
                leader_speed * leader_speed +
                2.0f * std::max(0.1f, prof.brake) * braking_room);
            target = std::min(target, braking_limit);
        }
        // Owning a junction means keep moving when the box is clear. It does
        // not grant permission to drive through the car already ahead. This
        // matters at wide degree-two freeway seams: both cars are briefly
        // committed while crossing the authored edge boundary, and the old
        // 4.5 m/s clearance floor made the follower rear-end a slowing leader.
        float leader_clearance_need = 0.0f;
        bool leader_allows_clearance = true;
        if (i < leader_speed_.size() && std::isfinite(leader_speed_[i]) &&
            std::isfinite(leader_gap_[i])) {
            const float leader_speed = std::max(0.0f, leader_speed_[i]);
            const float closing_speed = std::max(0.0f, v.speed_mps - leader_speed);
            leader_clearance_need = prof.min_gap +
                closing_speed * closing_speed /
                    (2.0f * std::max(0.1f, prof.brake));
            leader_allows_clearance = gap > leader_clearance_need;
        }

        auto exit_has_storage = [&](LaneRef exit_lane) {
            return exit_ready(i, exit_lane);
        };

        // Do not enter an intersection unless the chosen destination has room
        // for this entire car beyond the conflict box. The old follower check
        // slowed cars for the downstream queue, but still let their nose cross
        // the line; one stopped car then owned the box and gridlocked every
        // phase. A patient driver can also sample deterministic alternate
        // turns instead of waiting forever behind one saturated street.
        bool box_blocked = false;
        bool long_box_wait = false;
        float storage_lookahead = tuning_.junction_storage_lookahead_m;
        // The authored lookahead is enough on city streets. A freeway car can
        // need more road than that just to shed its speed, so begin the same
        // storage check at its braking distance before it reaches the box.
        if (lane.speed_limit_mps > 20.0f) {
            storage_lookahead = std::max(
                storage_lookahead,
                v.speed_mps * v.speed_mps /
                        (2.0f * std::max(0.1f, prof.brake)) +
                    tuning_.junction_storage_margin_m);
        }
        if (turn && slack > 0.0f &&
            slack <= storage_lookahead &&
            v.committed_junction == 0xFFFFFFFFu) {
            box_blocked = !exit_has_storage(turn->to);
            if (box_blocked) {
                v.blocked_exit_steps += k;
                const int64_t patience_steps = static_cast<int64_t>(std::ceil(
                    std::max(0.5f, prof.patience_seconds) /
                    static_cast<float>(kSimDt)));
                const int64_t retry_steps = std::max<int64_t>(
                    1, tuning_.blocked_exit_replan_every_steps);
                const int64_t waited_past_patience =
                    v.blocked_exit_steps - patience_steps;
                long_box_wait = waited_past_patience >= 0;
                const bool should_probe = waited_past_patience >= 0 &&
                    (waited_past_patience % retry_steps) < k;
                if (should_probe) {
                    const LaneRef blocked_lane = turn->to;
                    const uint32_t probes = std::max<uint32_t>(
                        1u, tuning_.blocked_exit_replan_probes);
                    for (uint32_t skip = 1; skip <= probes; ++skip) {
                        const LaneRef candidate = graph_->choose_next(
                            v.lane, map_seed_, v.decisions + skip);
                        if (!graph_->valid(candidate) ||
                            candidate == blocked_lane ||
                            !exit_has_storage(candidate))
                            continue;
                        // Publish the new plan on the next frozen step. Letting
                        // this car enter immediately would negotiate priority
                        // with the old turn while physically taking the new
                        // one, which can admit a conflicting fourth claimant.
                        v.decisions += skip;
                        v.blocked_exit_steps = 0;
                        ++stats_.jam_reroutes;
                        break;
                    }
                }
            } else {
                v.blocked_exit_steps = 0;
            }
        } else {
            v.blocked_exit_steps = 0;
        }

        const bool recovering =
            v.collision_recovery_seconds > 0.0f ||
            glm::length(v.collision_offset_xz) > 0.04f ||
            glm::length(v.collision_velocity_xz) > 0.04f ||
            std::fabs(v.collision_yaw_rad) > 0.01f;
        if (recovering) {
            // The driver lifts and regains control before merging back into
            // traffic. Never stop dead unless another rule requires it.
            target = std::min(
                target, std::min(std::max(0.0f, tuning_.collision_recovery_speed_cap_mps),
                                 std::max(3.0f,
                                 v.cruise_mps *
                                     tuning_.collision_recovery_speed_mul)));
        }

        // A committed movement is one-way state: finish it. Normal following
        // can slow a queue before the line, but cannot park a claimed car in
        // the conflict box. If it remains slow for a full second, the watchdog
        // raises the clearance speed and acceleration until it is out.
        if (clearing_intersection && leader_allows_clearance) {
            const float clear_speed = intersection_escape
                ? tuning_.intersection_escape_speed_mps
                : tuning_.intersection_min_clear_speed_mps;
            target = std::max(target,
                              std::min(v.cruise_mps, clear_speed));
            if (intersection_escape) ++stats_.intersection_escapes;
        }

        // Slow for the movement before reaching the box. This turns the lane
        // change at a junction into a deliberate maneuver instead of a full-
        // speed heading snap, and gives left/U turns visibly different intent.
        if (turning) {
            target = std::min(target, police_turn_speed_mps(
                turn_speed_limit(v.active_turn_kind), engaged_pursuit,
                tuning_.police));
        } else if (turn && to_end < 32.0f) {
            const float cap = police_turn_speed_mps(
                turn_speed_limit(turn->kind), engaged_pursuit, tuning_.police) +
                std::max(0.0f, to_end - 8.0f) * 0.25f;
            target = std::min(target, cap);
        }

        // The player is a first-class moving hazard, not just something the
        // collision solver notices after contact. The lifted Probable Cause
        // kernel catches in-path leaders plus predicted crossing/head-on hits.
        // Low-heat stops follow the suspect's bumper instead of shoving a
        // stopped driver down the road while the officer tries to dismount.
        bool honk_level = false;
        if ((!v.police_pursuit ||
             police_level_profile(police_wanted_level_).hazard_braking) && player &&
            std::fabs(player->position.y-v.pos.y)<2.5f) {
            const PlayerHazard hazard = assess_player_hazard(
                {v.pos.x, v.pos.z}, {v.fwd.x, v.fwd.z}, v.speed_mps,
                {player->position.x, player->position.z},
                {player->velocity.x, player->velocity.z},
                tuning_.player_hazard, tuning_.car_length_m);
            honk_level |= hazard.honk;
            if (hazard.active) {
                const float hazard_speed =
                    std::max(0.0f, hazard.leader_speed) +
                    traffic_follow_speed_for_gap(hazard.gap, prof);
                target = std::min(target, hazard_speed);
                ++stats_.player_hazards;
            }
        }
        // THE CONTACT CAP (PoliceTuning::contact_closing_mps). From three stars
        // a pursuer skips the block above on purpose — a cruiser that brakes
        // for the suspect as a hazard never reaches them — and nothing else
        // bounded the speed it arrived at. Capping the approach instead keeps
        // the contact and loses only the slam.
        if (engaged_pursuit && player && !police_target_on_foot_ &&
            std::fabs(player->position.y - v.pos.y) < 2.5f) {
            const glm::vec2 heading{v.fwd.x, v.fwd.z};
            const float heading_length = glm::length(heading);
            if (heading_length > 1e-4f) {
                const glm::vec2 cap_fwd = heading / heading_length;
                const glm::vec2 to_suspect{player->position.x - v.pos.x,
                                           player->position.z - v.pos.z};
                const float suspect_ahead = glm::dot(to_suspect, cap_fwd);
                const float suspect_lateral = std::fabs(
                    cap_fwd.x * to_suspect.y - cap_fwd.y * to_suspect.x);
                if (suspect_ahead > 0.0f &&
                    suspect_lateral < tuning_.player_hazard.half_width)
                    target = std::min(target, police_contact_speed_mps(
                        glm::dot(glm::vec2{player->velocity.x, player->velocity.z},
                                 cap_fwd),
                        suspect_ahead - tuning_.car_length_m, tuning_.police));
            }
        }
        for (const auto position:parked_vehicle_positions_) {
            if (std::isfinite(position.y) && std::fabs(position.y-v.pos.y)>2.5f) continue;
            const auto hazard=assess_player_hazard({v.pos.x,v.pos.z},{v.fwd.x,v.fwd.z},v.speed_mps,
                {position.x,position.z},glm::vec2{0},tuning_.player_hazard,tuning_.car_length_m);
            if (hazard.active) target=std::min(target,traffic_follow_speed_for_gap(hazard.gap,prof));
        }
        if (!v.police_pursuit && on_foot_player &&
            (!std::isfinite(on_foot_player->height_m) ||
            std::fabs(on_foot_player->height_m-v.pos.y)<2.5f)) {
            const PlayerHazard hazard = assess_player_hazard(
                {v.pos.x, v.pos.z}, {v.fwd.x, v.fwd.z}, v.speed_mps,
                on_foot_player->position, on_foot_player->velocity,
                tuning_.player_hazard_on_foot, tuning_.car_length_m);
            honk_level |= hazard.honk;
            if (hazard.active) {
                const float hazard_speed =
                    std::max(0.0f, hazard.leader_speed) +
                    traffic_follow_speed_for_gap(hazard.gap, prof);
                target = std::min(target, hazard_speed);
                ++stats_.player_hazards;
            }
        }
        // The horn at the player (PENG-51): level from the hazard kernels
        // above, one-shot from the debounce, on the sim clock. A pursuing
        // cruiser has a siren for this, and a dead engine has no horn.
        v.honk_player = honk_level && !v.police_pursuit &&
                        !vehicle_engine_failed(v.mechanical);
        v.honk_player_fire = honk_should_fire(
            v.honk_player, static_cast<double>(step) * kSimDt,
            static_cast<double>(tuning_.civilian.player_honk_interval_s),
            v.honk_debounce);

        // BEING RUN OVER. The city could flatten a pedestrian at thirty miles
        // an hour and not so much as scratch the player standing in the same
        // road, because traffic only ever BRAKED for somebody on foot — the
        // block above — and never touched them.
        //
        // This is the pedestrian knockdown test with the populations swapped:
        // the same footprint contact against the same body radius, reported
        // rather than resolved. Nothing is pushed back into the car, for the
        // same reason a pedestrian does not stop one. A PURSUING cruiser is
        // included deliberately: getting run down by the police is a way to
        // die and always was one, and exempting them would be a rule the
        // player can feel and cannot explain.
        if (on_foot_player &&
            (!std::isfinite(on_foot_player->height_m) ||
             std::fabs(on_foot_player->height_m - v.pos.y) < 2.5f)) {
            const auto footprint =
                traffic_vehicle_footprint(traffic_vehicle_kind(v));
            const float yaw = std::atan2(-v.fwd.x, -v.fwd.z);
            const BreakawayContact contact = breakaway_contact(
                v.pos, glm::angleAxis(yaw, glm::vec3{0.0f, 1.0f, 0.0f}),
                {footprint.half_width_m, footprint.half_length_m},
                {on_foot_player->position.x, v.pos.y,
                 on_foot_player->position.y},
                tuning_.ped_life.body_radius_m);
            if (contact.hit) {
                const glm::vec2 fwd_xz{v.fwd.x, v.fwd.z};
                const float fwd_len = glm::length(fwd_xz);
                const glm::vec2 travel = fwd_len > 1e-4f
                    ? fwd_xz / fwd_len : glm::vec2{1.0f, 0.0f};
                const float closing = glm::dot(
                    travel * v.speed_mps - on_foot_player->velocity, travel);
                if (closing >= tuning_.ped_life.knockdown_speed_mps)
                    on_foot_player_hits_.push_back({v.lane_key, v.slot,
                        v.police_unit, travel, closing});
            }
        }

        // Signals. Which half of the cycle is green is a pure function of the
        // STEP, like every other clock in this engine; approach_group_a() is
        // the lane graph's own answer for which approaches share a phase, and
        // the AI and the signal head must both call it or the bulb disagrees
        // with the stop decision.
        bool hold = false;
        // Set instead of `hold` when a dispatched cruiser is running a control
        // it would otherwise have stopped at. It caps the crossing speed rather
        // than parking the car on the line.
        bool run_control = false;
        const uint32_t jn = lane.junction_to;
        // A car clearing junction A may already be on an outgoing lane whose
        // next junction B is close enough to see. B's red/stop logic must not
        // park it inside A. Finish the committed movement first; normal
        // approach logic resumes on the first step after the rear clears.
        if (!clearing_intersection && jn < graph_->junction_count()) {
            const JunctionControl ctrl = graph_->junction_control(jn);
            const JunctionControl facing = graph_->approach_control(v.lane);
            if (ctrl == JunctionControl::Signal) {
                if (v.stop_junction != jn) {
                    v.stop_junction = jn;
                    v.stop_arrival_step = -1;
                }
                if (slack <= tuning_.stop_capture_m && v.stop_arrival_step < 0)
                    v.stop_arrival_step = step;
                const TrafficSignalPhase phase =
                    traffic_signal_phase(*graph_, jn, v.lane, step, tuning_);
                if (phase == TrafficSignalPhase::Red) {
                    // An uncommitted approach car is still outside the box.
                    // Keep it at the gate even if braking arithmetic put its
                    // logical nose a few centimetres past the line last step.
                    // A DISPATCHED cruiser is the exception: it runs the red
                    // at a capped speed instead of stopping. The cross-traffic
                    // negotiation further down is untouched, so it still gives
                    // way to a car in the box — it is exempt from the bulb,
                    // not from the collision.
                    if (engaged_pursuit) run_control = true;
                    else hold = true;
                } else if (phase == TrafficSignalPhase::Yellow) {
                    hold = !engaged_pursuit &&
                        (traffic_should_stop_for_yellow(
                            std::max(0.0f, slack), v.speed_mps, prof) ||
                         (slack <= 0.0f && v.speed_mps < 0.35f));
                }
            } else if (facing == JunctionControl::Stop) {
                if (v.stop_junction != jn) {
                    v.stop_junction = jn;
                    v.stop_wait_steps = 0;
                    v.stop_arrival_step = -1;
                    v.stop_completed = false;
                }
                if (slack <= tuning_.stop_capture_m &&
                    v.stop_arrival_step < 0) {
                    v.stop_arrival_step = step;
                }
                const TrafficStopDecision stop = traffic_stop_decision(
                    slack, v.speed_mps, v.stop_wait_steps, k,
                    required_stop_steps(prof), tuning_.stop_capture_m);
                v.stop_wait_steps = stop.wait_steps;
                // A pursuer never serves the dwell, and is treated as having
                // completed it so the all-way-stop arrival order below still
                // has an eligible approach to reason about instead of a car
                // that can never take its turn.
                v.stop_completed = stop.completed || engaged_pursuit;
                hold = stop.hold && !engaged_pursuit;
                if (stop.hold && engaged_pursuit) run_control = true;
                if (hold) ++stats_.stop_holds;
            } else if (v.stop_junction != 0xFFFFFFFFu) {
                v.stop_junction = 0xFFFFFFFFu;
                v.stop_wait_steps = 0;
                v.stop_arrival_step = -1;
                v.stop_completed = false;
            }
            if (facing == JunctionControl::Yield && !engaged_pursuit) {
                // Look before merging. With an open gap this is a rolling
                // approach, with no synthetic stop-sign dwell.
                const float rolling_cap = lane.speed_limit_mps > 20.0f ? 12.0f : 6.0f;
                target = std::min(target, rolling_cap + std::max(0.0f, slack) * 0.3f);
            } else if (facing == JunctionControl::Yield) {
                run_control = true;
            }

            // Cross-traffic negotiation uses the frozen head car on each
            // approach. Signals admit only the active phase; all-way stops use
            // completed-stop arrival order; uncontrolled junctions and left
            // turns use movement priority, ETA, then stable identity.
            const bool eligible = facing != JunctionControl::Stop ||
                                  v.stop_completed;
            float conflict_lookahead = tuning_.junction_lookahead_m;
            if (lane.speed_limit_mps > 20.0f) {
                conflict_lookahead = std::max(
                    conflict_lookahead,
                    v.speed_mps * v.speed_mps /
                            (2.0f * std::max(0.1f, prof.brake)) +
                        tuning_.junction_storage_margin_m);
            }
            if (eligible && slack <= conflict_lookahead &&
                jn < junction_heads_.size()) {
                TrafficApproachView mine = approach_view(
                    i, jn, v.stop_arrival_step);
                mine.arrival_step = v.stop_arrival_step;
                for (const uint32_t oi : junction_heads_[jn]) {
                    if (oi == i || oi >= junction_frozen_.size()) continue;
                    const JunctionSnapshot& os = junction_frozen_[oi];
                    bool other_committed = false;
                    if (os.committed_junction == jn &&
                        graph_->valid(os.lane)) {
                        const Lane& claim_lane = graph_->lane(os.lane);
                        other_committed = (claim_lane.junction_to == jn &&
                            os.lane == os.committed_approach_lane) ||
                            (claim_lane.junction_from == jn &&
                             os.dist_along_m < junction_clearance(jn)) ||
                            os.active_turn;
                    }
                    const LaneRef mine_exit = turn ? turn->to : kInvalidLane;
                    const LaneRef other_approach = other_committed
                        ? os.committed_approach_lane : os.lane;
                    const LaneRef other_exit = other_committed
                        ? os.committed_exit_lane
                        : os.planned_exit_lane;
                    // A car with nowhere to go has no right-of-way claim yet.
                    // Otherwise a blocked major turn can veto the very minor
                    // movement that would drain its downstream queue.
                    if (!other_committed &&
                        (os.engine_failed ||
                         !exit_ready(oi, other_exit))) continue;
                    if (!movements_conflict(v.lane, mine_exit,
                                            other_approach, other_exit)) continue;
                    if (other_committed && traffic_same_committed_movement(
                            v.lane, mine_exit, os.committed_approach_lane,
                            os.committed_exit_lane)) {
                        // The leader is still on the shared turn curve. Lane
                        // bookkeeping already calls it an outgoing car, but
                        // its body can still overlap the incoming stop gate.
                        // A sum of incoming/outgoing lane distances is not the
                        // curve arclength and let Route 1 followers drive into
                        // the leader's rear quarter. Admit the next car after
                        // the first releases its movement claim.
                        hold = true;
                        ++stats_.junction_yields;
                        break;
                    }
                    if (!other_committed) {
                        if (!graph_->valid(os.lane) || os.lane == v.lane)
                            continue;
                        const Lane& other_lane = graph_->lane(os.lane);
                        if (other_lane.junction_to != jn) continue;
                        if (ctrl == JunctionControl::Signal &&
                            traffic_signal_phase(*graph_, jn, os.lane, step,
                                                 tuning_) ==
                                TrafficSignalPhase::Red) {
                            // Red traffic cannot veto the active phase. The
                            // old missed-cycle exception let every one of six
                            // queued Rimway lanes reserve Apron Spine's green,
                            // starving its lead car indefinitely.
                            continue;
                        }
                    }

                    int64_t other_arrival = os.stop_arrival_step;
                    if (!other_committed &&
                        graph_->approach_control(os.lane) == JunctionControl::Stop) {
                        const Lane& ol = graph_->lane(os.lane);
                        const float other_slack =
                            ol.length_m - os.dist_along_m -
                            junction_clearance(ol.junction_to);
                        if (other_arrival < 0 &&
                            other_slack <= tuning_.stop_capture_m)
                            other_arrival = step;
                        const TrafficStopDecision predicted =
                            traffic_stop_decision(
                                other_slack, os.speed_mps,
                                os.stop_wait_steps, k,
                                required_stop_steps(vehicles_[oi].profile),
                                tuning_.stop_capture_m);
                        if (!predicted.completed) continue;
                    }

                    const TrafficApproachView other = approach_view(
                        oi, jn, other_arrival);
                    if (traffic_approach_yields(
                            mine, other, ctrl == JunctionControl::Stop,
                            tuning_.junction_eta_tie_s)) {
                        // Right of way is a RULE, and a dispatched cruiser is
                        // exempt from rules. It is not exempt from the car: a
                        // unit already committed to the box is a body, and the
                        // pursuer stops for it exactly like anyone else. Giving
                        // way to merely-has-priority traffic is what left the
                        // nearest pursuer held at a junction for a third of
                        // every chase, and worse, it deadlocks — a stopped car
                        // has a long ETA, a long ETA loses the compare, and
                        // losing keeps it stopped.
                        if (engaged_pursuit && !other_committed)
                            run_control = true;
                        else hold = true;
                        ++stats_.junction_yields;
                        break;
                    }
                }
            }
        }
        if (hold) {
            // Same shape as the maneuver governor: approach speed proportional
            // to remaining distance, capped by the speed this driver's brakes
            // can actually shed before the line. The old linear cap did
            // nothing at freeway range, then hard-stopped the lead car at the
            // gate and let its following queue hit it.
            const float room = std::max(0.0f, slack);
            const float braking_speed = std::sqrt(
                2.0f * std::max(0.1f, prof.brake) * room);
            target = std::min(target,
                std::min(room * 0.8f, braking_speed));
        }
        if (run_control)
            target = std::min(target,
                police_control_override_speed(true, slack, tuning_.police));
        if (box_blocked) {
            hold = true;
            target = std::min(target, std::max(0.0f, slack * 0.8f));
            ++stats_.junction_box_holds;
        }
        // Facing away from the suspect on a two-way street: shed speed so the
        // mid-block turnaround can fire. Pinned at 4 m/s this was a cruiser
        // closing on the player at walking pace; 7 still triggers the
        // turnaround's own speed gate without being a crawl.
        if (v.police_pursuit && !clearing_intersection &&
            lane.cls != RoadClass::Freeway && !lane.one_way &&
            glm::distance(glm::vec2{v.pos.x, v.pos.z}, police_search_centre_) < 100.0f &&
            glm::dot(police_search_centre_ - glm::vec2{v.pos.x, v.pos.z},
                     glm::vec2{v.fwd.x, v.fwd.z}) < -12.0f)
            target = std::min(target, 7.0f);
        target = std::min(target, emergency_obstacle_speed(i));
        if (!clearing_intersection && v.emergency_yield != EmergencyYield::None)
            target = std::min(target, 3.0f);
        if (vehicle_engine_failed(v.mechanical)) target=0.f;
        if (v.police_unit && !police_officer_driving_allowed(v.officer))
            target = 0.0f;
        if (!vehicle_engine_failed(v.mechanical) &&
            v.speed_mps < 0.5f && target < 0.5f)
            v.delay_seconds = std::min(60.0f, v.delay_seconds + dt);
        else if (v.speed_mps > 2.0f)
            v.delay_seconds = std::max(0.0f, v.delay_seconds - dt * 0.5f);
        // Behind an obstruction (PENG-47)? Not while legally held at a control
        // or waiting for a blocked exit — those are queues, and a queue is not
        // overtaken. What this measures is time spent behind something well
        // under the driver's own cruise on open road: a wreck, the player's
        // stopped car, a dawdler. The planners in emergency.cpp read it.
        {
            const CivilianManeuverTuning& civ = tuning_.civilian;
            bool behind_obstruction = false;
            if (!v.snowplow_unit && !hold && !box_blocked &&
                !clearing_intersection && !vehicle_engine_failed(v.mechanical)) {
                const ObstructionView ob = classify_obstruction(i, player);
                // A body no lane bucket knows about — a parked car reaching
                // into the lane, the player's car — is followed like a
                // leader, by everyone including a cruiser. The buckets only
                // carry AI traffic; without this a cruiser drives through a
                // parked car it would happily swerve round.
                if (ob.present && !ob.ai)
                    target = std::min(target, std::min(
                        ob.speed_mps + traffic_follow_speed_for_gap(ob.gap_m, prof),
                        std::sqrt(ob.speed_mps * ob.speed_mps +
                                  2.0f * std::max(0.1f, prof.brake) *
                                      std::max(0.0f, ob.gap_m - prof.min_gap))));
                if (!v.police_pursuit) {
                    behind_obstruction = ob.present && !ob.queue &&
                        ob.gap_m < civ.trigger_gap_m &&
                        ob.speed_mps < v.cruise_mps - civ.slow_leader_margin_mps;
                    // Leave room to pull out (CivilianManeuverTuning::standoff_m).
                    if (ob.present && ob.stationary && !ob.queue)
                        target = std::min(target, std::sqrt(
                            2.0f * std::max(0.1f, prof.brake) *
                            std::max(0.0f, ob.gap_m - civ.standoff_m)));
                    // Deep-blocked: stopped at the stand-off behind something
                    // that is not going anywhere. The recovery ladder's clock.
                    if (ob.present && ob.stationary && !ob.queue &&
                        ob.gap_m < civ.deep_block_gap_m &&
                        v.speed_mps < civ.stationary_mps)
                        v.jam_steps += k;
                    else if (v.speed_mps > 2.0f)
                        v.jam_steps = 0;
                }
            }
            v.obstruction_wait_s = behind_obstruction
                ? std::min(60.0f, v.obstruction_wait_s + dt) : 0.0f;
            v.lane_change_cooldown_s = std::max(0.0f, v.lane_change_cooldown_s - dt);
        }
        const bool perturbed = target < v.cruise_mps - 1e-4f;

        if (v.mode == AgentMode::Analytic && !perturbed) {
            // REPRODUCED, NOT ADVANCED. The closed form is evaluated at the
            // absolute step, so this agent's state does not remember the step
            // it was instantiated at — which is the entire claim analytic
            // ambient traffic is making.
            const PhantomState ph = phantom_vehicle(map_seed_, lane,
                                                    veh_sched_[v.lane], v.slot,
                                                    step, ambient_);
            if (ph.dist_along_m + 1e-3f < v.last_dist_m) {
                // The schedule wrapped. A phantom may teleport to the start of
                // its lane because nobody is looking at it; an ACTIVE car may
                // not, so this is where it stops being a phantom and starts
                // being a car with a history.
                v.mode = AgentMode::Integrating;
            } else {
                v.dist_along_m = ph.dist_along_m;
                v.speed_mps = ph.speed_mps;
                v.last_dist_m = ph.dist_along_m;
                const LanePose p = graph_->pose(v.lane, v.dist_along_m);
                v.pos = p.position;
                v.fwd = p.tangent;
                continue;
            }
        }

        v.mode = AgentMode::Integrating;

        const float dv = target - v.speed_mps;
        const float rate = dv >= 0.0f
            ? (intersection_escape && leader_allows_clearance
                   ? std::max(prof.accel,
                              tuning_.intersection_escape_accel_mps2)
                   : prof.accel)
            : (vehicle_engine_failed(v.mechanical) && !hold && !box_blocked &&
               gap>prof.min_gap+v.speed_mps*2.f ? std::min(prof.brake,1.f):prof.brake);
        v.speed_mps += std::clamp(dv, -rate * dt, rate * dt);
        v.speed_mps = std::max(0.0f, v.speed_mps);
        if (long_box_wait && box_blocked && v.speed_mps < 0.35f)
            ++stats_.stalled_vehicles;

        // Drive along the same heading that apply_collision_pose renders.
        // Projecting only the forward component onto the route prevents the
        // old full-lane-speed-plus-sideways-drift motion. Use the freshly
        // governed speed so a stopped/blocked car cannot slide back to lane.
        const float driven_m = v.speed_mps * dt;
        // During the short impact skid, momentum still follows the previous
        // route velocity while the shell can spin. Once the driver regains
        // control, the tyres carry motion along the current body heading.
        const float driving_yaw = v.collision_recovery_seconds <= 0.0f
            ? v.collision_yaw_rad : 0.0f;
        const float travel_m = driven_m * std::cos(driving_yaw);
        const glm::vec2 recovery_forward = glm::normalize(
            glm::vec2{recovery_pose.tangent.x, recovery_pose.tangent.z});
        const glm::vec2 recovery_right{-recovery_forward.y, recovery_forward.x};
        v.collision_offset_xz -= recovery_right *
            (driven_m * std::sin(driving_yaw));
        if (turning) {
            const TrafficTurnCurve curve = agent_turn_curve(*graph_, v);
            v.turn_progress_m = std::min(
                v.turn_length_m, v.turn_progress_m + travel_m);
            v.dist_along_m = v.turn_exit_m *
                (v.turn_progress_m / v.turn_length_m);
            v.turn_steer_rad = curve_steer(curve, v.turn_progress_m);
            if (v.turn_progress_m >= v.turn_length_m - 1e-4f) {
                v.dist_along_m = v.turn_exit_m;
                v.turn_from_lane = kInvalidLane;
                v.turn_progress_m = 0.0f;
                v.turn_length_m = 0.0f;
                v.turn_entry_m = 0.0f;
                v.turn_exit_m = 0.0f;
                v.turn_steer_rad = 0.0f;
            }
        } else {
            const float follow_limit =
                std::isfinite(leader_gap_[i])
                    ? std::max(0.0f, v.dist_along_m +
                          gap - prof.min_gap)
                    : kInf;
            v.dist_along_m += travel_m;
            if (v.committed_junction == 0xFFFFFFFFu &&
                v.dist_along_m > follow_limit) {
                v.dist_along_m = follow_limit;
                const float leader_speed =
                    i < leader_speed_.size() &&
                            std::isfinite(leader_speed_[i])
                        ? std::max(0.0f, leader_speed_[i])
                        : 0.0f;
                v.speed_mps = std::min(v.speed_mps, leader_speed);
            }
            if (hold) {
                const float gate = std::max(0.0f, lane.length_m - stop_distance);
                if (v.dist_along_m > gate) {
                    v.dist_along_m = gate;
                    v.speed_mps = 0.0f;
                }
            }
            // Crossing the outer stop gate owns the movement immediately.
            // Acute junctions can have a long straight reservation corridor
            // before the compact turn curve. Without this claim, a car that
            // entered on green was still called uncommitted when the phase
            // changed and got snapped tens of metres backward to the gate.
            const float reservation_entry =
                std::max(0.0f, lane.length_m - stop_distance);
            const bool reserve_movement =
                !seamless_continuation && !hold && turn &&
                graph_->valid(turn->to) &&
                v.committed_junction == 0xFFFFFFFFu &&
                lane.junction_to < graph_->junction_count() &&
                v.dist_along_m >= reservation_entry;
            if (reserve_movement) {
                v.committed_junction = lane.junction_to;
                v.committed_approach_lane = v.lane;
                v.committed_exit_lane = turn->to;
                v.stop_junction = 0xFFFFFFFFu;
                v.stop_wait_steps = 0;
                v.stop_arrival_step = -1;
                v.stop_completed = false;
                v.intersection_stall_steps = 0;
            }
            // Reserve the whole acute merge mouth, but stay on the real lane
            // until the compact corner. Stretching the cubic out to a distant
            // stop gate cuts across neighbouring lanes and creates new crashes.
            const float core_clearance = junction_turn_clearance(lane.junction_to);
            const float entry_m = std::min(core_clearance, lane.length_m);
            const float entry_dist = lane.length_m - entry_m;
            const bool begin_turn =
                !seamless_continuation && !hold && turn &&
                graph_->valid(turn->to) &&
                lane.junction_to < graph_->junction_count() &&
                v.dist_along_m >= entry_dist &&
                v.committed_junction == lane.junction_to &&
                v.committed_approach_lane == v.lane &&
                v.committed_exit_lane == turn->to;
            if (begin_turn) {
                const LaneRef incoming = v.lane;
                const LaneRef outgoing = turn->to;
                const float overshoot_m =
                    std::max(0.0f, v.dist_along_m - entry_dist);
                const float exit_m = std::min(
                    core_clearance, graph_->lane(outgoing).length_m);
                const TrafficTurnCurve curve = traffic_turn_curve(
                    *graph_, incoming, outgoing, entry_m, exit_m);

                // Move logical ownership to the exit lane at the stop line so
                // queues and storage still use the cheap lane buckets. The
                // body itself stays on this curve until it reaches the far
                // clearance line.
                v.committed_junction = lane.junction_to;
                v.committed_approach_lane = incoming;
                v.committed_exit_lane = outgoing;
                v.lane = outgoing;
                if (v.police_pursuit &&
                    v.police_route_index + 1u < v.police_route.size() &&
                    v.police_route[v.police_route_index + 1u] == outgoing) {
                    ++v.police_route_index;
                }
                ++v.decisions;
                v.turn_from_lane = incoming;
                v.active_turn_kind = turn->kind;
                v.turn_entry_m = entry_m;
                v.turn_exit_m = exit_m;
                v.turn_length_m = curve.length_m;
                v.turn_progress_m = std::min(overshoot_m, curve.length_m);
                v.dist_along_m = curve.length_m > 1e-5f
                    ? exit_m * (v.turn_progress_m / curve.length_m)
                    : exit_m;
                v.turn_steer_rad = curve_steer(curve, v.turn_progress_m);
                v.stop_junction = 0xFFFFFFFFu;
                v.stop_wait_steps = 0;
                v.stop_arrival_step = -1;
                v.stop_completed = false;
                v.intersection_stall_steps = 0;
                v.cruise_mps = std::min(
                    v.cruise_mps, graph_->lane(outgoing).speed_limit_mps);
                if (!(curve.length_m > 1e-5f) ||
                    v.turn_progress_m >= curve.length_m - 1e-4f) {
                    v.turn_from_lane = kInvalidLane;
                    v.turn_progress_m = 0.0f;
                    v.turn_length_m = 0.0f;
                    v.turn_entry_m = 0.0f;
                    v.turn_exit_m = 0.0f;
                    v.turn_steer_rad = 0.0f;
                }
            } else if (seamless_continuation &&
                       v.dist_along_m >= lane.length_m) {
                const float overshoot = v.dist_along_m - lane.length_m;
                v.lane = turn->to;
                if (v.police_pursuit &&
                    v.police_route_index + 1u < v.police_route.size() &&
                    v.police_route[v.police_route_index + 1u] == v.lane) {
                    ++v.police_route_index;
                }
                ++v.decisions;
                v.dist_along_m = std::min(
                    overshoot, graph_->lane(v.lane).length_m);
                v.cruise_mps = std::min(
                    v.cruise_mps, graph_->lane(v.lane).speed_limit_mps);
                v.committed_junction = 0xFFFFFFFFu;
                v.committed_approach_lane = kInvalidLane;
                v.committed_exit_lane = kInvalidLane;
                v.stop_junction = 0xFFFFFFFFu;
                v.stop_wait_steps = 0;
                v.stop_arrival_step = -1;
                v.stop_completed = false;
                v.intersection_stall_steps = 0;
            } else if (v.dist_along_m >= lane.length_m) {
                // A held car can coast slightly past its line while braking,
                // but it cannot silently switch tangents. Dead ends stop at
                // their endpoint; admitted movements switch through the curve
                // above.
                v.dist_along_m = lane.length_m;
                if (!turn || !graph_->valid(turn->to)) v.speed_mps = 0.0f;
            }
        }
        v.last_dist_m = v.dist_along_m;

        const LanePose p = route_pose(*graph_, v);
        apply_collision_pose(v, p);
    }

    resolve_vehicle_collisions();
    // Prepare response state and routes for the next frozen step. Doing this
    // after all movement keeps the turn stored in tomorrow's lane buckets and
    // the turn actually driven tomorrow identical; a mid-step conversion at a
    // junction would otherwise negotiate one exit and take another.
    update_police_response(step);
    step_police_officers(player, on_foot_player, step);

    // EVAPORATION (PENG-50): cars the ladder gave up on. Marked inside the
    // loop, swept here — the loop indexes the vector and the frozen buckets
    // by position — with the same sequence take_vehicle() uses. Permanent:
    // this departure never comes back.
    bool any_retired = false;
    for (VehicleAgent& v : vehicles_) {
        if (!v.jam_retire) continue;
        if (v.node != kInvalidId) dead_nodes_.push_back(v.node);
        retire(v.lane_key, v.slot, v.generation);
        ++stats_.retired;
        ++stats_.jam_despawns;
        any_retired = true;
    }
    if (any_retired) {
        vehicles_.erase(std::remove_if(vehicles_.begin(), vehicles_.end(),
            [](const VehicleAgent& v) { return v.jam_retire; }), vehicles_.end());
        stats_.vehicles = vehicles_.size();
        rebuild_buckets();
    }
}

void Crowd::resolve_vehicle_collisions() {
    struct CellEntry {
        int32_t x = 0;
        int32_t z = 0;
        uint32_t agent = 0;
    };
    auto less = [](const CellEntry& a, const CellEntry& b) {
        if (a.x != b.x) return a.x < b.x;
        if (a.z != b.z) return a.z < b.z;
        return a.agent < b.agent;
    };

    const float cell_m = std::max(1.0f, tuning_.traffic_collision_cell_m);
    const int cell_reach = snowplow_unit_count() > 0
        ? std::max(1, static_cast<int>(std::ceil(7.2f / cell_m))) : 1;
    std::vector<CellEntry> cells;
    std::vector<int32_t> agent_cell_x(vehicles_.size());
    std::vector<int32_t> agent_cell_z(vehicles_.size());
    cells.reserve(vehicles_.size());
    for (uint32_t i = 0; i < vehicles_.size(); ++i) {
        const VehicleAgent& vehicle = vehicles_[i];
        agent_cell_x[i] = floor_div(vehicle.pos.x, cell_m);
        agent_cell_z[i] = floor_div(vehicle.pos.z, cell_m);
        cells.push_back(CellEntry{agent_cell_x[i], agent_cell_z[i], i});
    }
    std::sort(cells.begin(), cells.end(), less);

    // The active vector is identity-sorted, and each pair is visited once as
    // (lower identity, higher identity). Broadphase cell order therefore never
    // leaks hash-table iteration or spawn order into a collision result.
    for (uint32_t i = 0; i < vehicles_.size(); ++i) {
        const int32_t cx = agent_cell_x[i];
        const int32_t cz = agent_cell_z[i];
        for (int dz = -cell_reach; dz <= cell_reach; ++dz) {
            for (int dx = -cell_reach; dx <= cell_reach; ++dx) {
                const CellEntry key{cx + dx, cz + dz, 0u};
                auto it = std::lower_bound(cells.begin(), cells.end(), key, less);
                while (it != cells.end() && it->x == key.x && it->z == key.z) {
                    const uint32_t j = it->agent;
                    ++it;
                    if (j <= i) continue;
                    ++stats_.ai_collision_pairs;
                    const TrafficBodyCollision contact =
                        resolve_traffic_body_collision(
                            vehicles_[i], vehicles_[j], tuning_);
                    if (contact.collided) ++stats_.ai_collisions;
                }
            }
        }
    }
}

bool Crowd::resolve_player_collision(VehicleState& player,
                                     float player_half_width_m,
                                     float player_half_length_m,
                                     float player_mass_kg,
                                  float player_body_damage_gain) {
    police_player_contacts_.clear();
    constexpr float kDamageThreshold = 2.5f;
    constexpr int kSolverPasses = 4;

    const float player_half_width = std::max(0.1f, player_half_width_m);
    const float player_half_length = std::max(0.1f, player_half_length_m);

    glm::vec3 player_fwd3 = vehicle_forward(player);
    player_fwd3.y = 0.0f;
    const float player_fwd_length = glm::length(player_fwd3);
    if (!(player_fwd_length > 1e-5f)) return false;
    player_fwd3 /= player_fwd_length;
    const glm::vec2 player_fwd{player_fwd3.x, player_fwd3.z};
    // Project roll/pitch away: car-car contact is a road-plane footprint.
    // Deriving right from forward also keeps a tipped car's footprint valid
    // when its actual +X axis points mostly upward.
    const glm::vec2 player_right{-player_fwd.y, player_fwd.x};

    const float inv_player_mass =
        1.0f / std::max(1.0f, player_mass_kg);
    const float inv_traffic_mass =
        1.0f / std::max(1.0f, tuning_.traffic_mass_kg);
    const float inv_mass_sum = inv_player_mass + inv_traffic_mass;
    const float player_share = inv_player_mass / inv_mass_sum;
    const float traffic_share = inv_traffic_mass / inv_mass_sum;

    // A pile-up can push the player back into a car already visited earlier in
    // the loop. A few deterministic Gauss-Seidel passes close those secondary
    // overlaps instead of leaving interlocked meshes until the next frame.
    std::vector<uint8_t> damage_recorded(vehicles_.size(), 0u);
    std::vector<uint8_t> police_contact_recorded(vehicles_.size(), 0u);

    bool collided = false;
    for (int pass = 0; pass < kSolverPasses; ++pass) {
        bool pass_collided = false;
        for (std::size_t traffic_index = 0;
             traffic_index < vehicles_.size(); ++traffic_index) {
            VehicleAgent& traffic = vehicles_[traffic_index];
            if (std::fabs(player.position.y - traffic.pos.y) > 2.5f) continue;

            glm::vec2 traffic_fwd{traffic.fwd.x, traffic.fwd.z};
            const float traffic_fwd_length = glm::length(traffic_fwd);
            if (!(traffic_fwd_length > 1e-5f)) continue;
            traffic_fwd /= traffic_fwd_length;
            const glm::vec2 traffic_right{-traffic_fwd.y, traffic_fwd.x};
            const TrafficVehicleFootprint traffic_footprint =
                traffic_vehicle_footprint(traffic_vehicle_kind(traffic));
            const float traffic_half_width = std::max(
                0.1f, traffic_footprint.half_width_m);
            const float traffic_half_length = std::max(
                0.1f, traffic_footprint.half_length_m);
            const glm::vec2 traffic_base_centre{
                traffic.pos.x, traffic.pos.z};
            const glm::vec2 player_base_centre{
                player.position.x, player.position.z};
            const DamageAdjustedPlanarBody traffic_body =
                damage_adjusted_planar_body(
                    traffic_base_centre, traffic_right, traffic_fwd,
                    traffic_half_width, traffic_half_length,
                    traffic.body_damage, player_base_centre);
            const DamageAdjustedPlanarBody player_body =
                damage_adjusted_planar_body(
                    player_base_centre, player_right, player_fwd,
                    player_half_width, player_half_length,
                    player.body_damage, traffic_base_centre);
            const PlanarBodyContact contact = planar_body_contact(
                traffic_body.centre, traffic_right, traffic_fwd,
                traffic_body.half_width, traffic_body.half_length,
                player_body.centre, player_right, player_fwd,
                player_body.half_width, player_body.half_length);
            if (!contact.collided) continue;

            collided = true;
            pass_collided = true;
            const glm::vec3 normal{
                contact.normal.x, 0.0f, contact.normal.y};
            const float separation = contact.penetration_m + 0.01f;
            player.position += normal * (separation * player_share);
            const glm::vec2 traffic_separation =
                -contact.normal * (separation * traffic_share);
            traffic.collision_offset_xz += traffic_separation;
            traffic.pos.x += traffic_separation.x;
            traffic.pos.z += traffic_separation.y;

            const glm::vec3 traffic_velocity =
                glm::vec3{traffic_fwd.x, 0.0f, traffic_fwd.y} *
                    traffic.speed_mps +
                glm::vec3{traffic.collision_velocity_xz.x, 0.0f,
                          traffic.collision_velocity_xz.y};
            const glm::vec3 relative_motion_world =
                player.velocity - traffic_velocity;
            const float inward = glm::dot(relative_motion_world, normal);
            const float impact = std::max(0.0f, -inward);
            player.car_contact_speed=std::max(player.car_contact_speed,impact);
            const glm::vec2 traffic_contact =
                traffic_body.centre - traffic_base_centre +
                body_surface_offset(
                    traffic_right, traffic_fwd, contact.normal,
                    player_body.centre - traffic_body.centre,
                    traffic_body.half_width, traffic_body.half_length);
            const glm::vec2 player_contact =
                player_body.centre - player_base_centre +
                body_surface_offset(
                    player_right, player_fwd, -contact.normal,
                    traffic_body.centre - player_body.centre,
                    player_body.half_width, player_body.half_length);

            if (traffic.police_unit && !police_contact_recorded[traffic_index]) {
                police_contact_recorded[traffic_index] = 1u;
                const glm::vec3 player_contact_velocity = player.velocity +
                    glm::cross(player.angular_velocity,
                               glm::vec3{player_contact.x, 0.0f, player_contact.y});
                const glm::vec3 police_contact_velocity = traffic_velocity +
                    glm::cross(glm::vec3{0.0f, traffic.collision_yaw_velocity, 0.0f},
                               glm::vec3{traffic_contact.x, 0.0f, traffic_contact.y});
                police_player_contacts_.push_back({traffic.lane_key, traffic.slot,
                    player_contact_velocity, police_contact_velocity, normal});
            }

            if (inward < 0.0f) {
                const float impulse =
                    -(1.0f + tuning_.collision_restitution) * inward /
                    inv_mass_sum;
                player.velocity += normal * (impulse * inv_player_mass);
                const glm::vec3 traffic_delta_velocity =
                    -normal * (impulse * inv_traffic_mass);
                traffic.collision_velocity_xz += glm::vec2{
                    traffic_delta_velocity.x, traffic_delta_velocity.z};
                traffic.collision_velocity_xz = clamp_length(
                    traffic.collision_velocity_xz,
                    tuning_.collision_max_speed_mps);

                // An off-centre hit spins the traffic body as well as moving
                // it, using the same actual perimeter point as damage mapping.
                const glm::vec2 traffic_impulse{
                    traffic_delta_velocity.x, traffic_delta_velocity.z};
                const float torque =
                    traffic_contact.x * traffic_impulse.y -
                    traffic_contact.y * traffic_impulse.x;
                traffic.collision_yaw_velocity +=
                    std::clamp(torque * 0.16f, -2.2f, 2.2f);
            }
            traffic.mode = AgentMode::Integrating;
            traffic.collision_recovery_seconds = std::max(
                traffic.collision_recovery_seconds,
                tuning_.collision_recovery_delay_s);

            if (impact > kDamageThreshold &&
                damage_recorded[traffic_index] == 0u) {
                damage_recorded[traffic_index] = 1u;
                const float damage = std::min(
                    (impact - kDamageThreshold) * 2.2f, 35.0f);
                const glm::vec3 player_local_contact =
                    glm::conjugate(player.orientation) *
                    glm::vec3{player_contact.x, 0.0f, player_contact.y};
                const glm::vec3 player_motion_local3 =
                    glm::conjugate(player.orientation) * relative_motion_world;
                const glm::vec3 traffic_motion_local3 =
                    traffic_body_local_contact(
                        traffic_fwd,
                        glm::vec2{-relative_motion_world.x,
                                  -relative_motion_world.z});
                const float relative_speed = glm::length(glm::vec2{
                    relative_motion_world.x, relative_motion_world.z});
                const float glancing = relative_speed > 1e-5f
                    ? std::clamp(1.0f - impact / relative_speed,
                                 0.0f, 1.0f)
                    : 0.0f;
                apply_vehicle_impact(
                    player.body_damage, player_local_contact, damage,
                    player_half_width, player_half_length,
                    {player_motion_local3.x, player_motion_local3.z},
                    0.43f, 0.58f, glancing, player_body_damage_gain);
                apply_vehicle_impact(
                    traffic.body_damage,
                    traffic_body_local_contact(traffic_fwd, traffic_contact),
                    damage, traffic_half_width, traffic_half_length,
                    {traffic_motion_local3.x, traffic_motion_local3.z},
                    0.43f, 0.58f, glancing);
                player.last_impact_speed = impact;
                player.last_impact_damage = damage;
                player.health = std::max(0.0f, player.health - damage);
                ++player.impact_count;
            }
        }
        if (!pass_collided) break;
    }
    return collided;
}

// ---------------------------------------------------------------------------
//  step_peds
// ---------------------------------------------------------------------------

namespace {

// A keyed [0, 1) roll for ONE pedestrian decision, with no stream left over.
//
// The identity goes in exactly as lane_graph.h prescribes — the lane's stable
// key split across the two coordinate axes — and the slot and the person's own
// decision ordinal are folded into the channel. The ordinal is a count of THIS
// PERSON'S decisions and never a count of the population, which is the whole
// difference between a keyed draw and a sequential one: how many other people
// were made first is a fact about which way the player drove in.
float ped_roll(uint64_t seed, uint64_t lane_key, uint32_t slot,
               uint32_t ordinal, uint32_t channel) {
    Rng r{hash_coord3(
        seed, static_cast<int32_t>(static_cast<uint32_t>(lane_key)),
        static_cast<int32_t>(static_cast<uint32_t>(lane_key >> 32)),
        channel ^ (slot * 0x9E3779B9u) ^ (ordinal * 0x85EBCA6Bu))};
    return r.next_float();
}

// Turn a [0, 1) roll into a duration in STEPS. Never seconds of wall time: a
// duration measured in steps is a property of the drive, and a duration
// measured in anything else is a property of the machine it ran on.
int64_t ped_roll_steps(float roll, int64_t lo, int64_t hi) {
    if (hi < lo) hi = lo;
    const double span = static_cast<double>(hi - lo);
    return lo + static_cast<int64_t>(static_cast<double>(roll) * span);
}

// Who panics at what. There is ONE definition of "reckless" — the panic
// kernel's own gate, shared with traffic — and the disposition only says who
// agrees with it. Scaling the closing-speed floor rather than inventing a
// second trigger is what keeps that true: a coward spooks at a speed a
// die-hard shrugs off, and both are asking the same question.
PanicTuning ped_panic_for(const PedLifeTuning& life,
                          ped_react::Disposition d) {
    PanicTuning t = life.panic;
    if (d == ped_react::Disposition::Coward)
        t.trigger_closing *= life.coward_closing_mul;
    else if (d == ped_react::Disposition::DieHard)
        t.trigger_closing *= life.diehard_closing_mul;
    return t;
}

// How fast this person moves, as a multiple of their walking speed.
float ped_activity_speed_mul(PedActivity a, const PedLifeTuning& life) {
    switch (a) {
        case PedActivity::Walking: return 1.0f;
        case PedActivity::Idling:
        case PedActivity::Waiting:
        case PedActivity::Downed: return 0.0f;
        case PedActivity::Rising: return 0.0f;
        case PedActivity::Dead: return 0.0f;
        case PedActivity::Alarmed: return life.alarm_speed_mul;
        case PedActivity::Fleeing: return life.flee_speed_mul;
    }
    return 1.0f;
}

}  // namespace

const char* ped_activity_name(PedActivity a) {
    switch (a) {
        case PedActivity::Walking: return "walking";
        case PedActivity::Idling: return "idling";
        case PedActivity::Waiting: return "waiting";
        case PedActivity::Alarmed: return "alarmed";
        case PedActivity::Fleeing: return "fleeing";
        case PedActivity::Downed: return "downed";
        case PedActivity::Rising: return "rising";
        case PedActivity::Dead: return "dead";
    }
    return "walking";
}

PedShotHit Crowd::raycast_ped(glm::vec3 origin,glm::vec3 unit_direction,
                             float max_distance) const {
    PedShotHit result;
    float nearest=max_distance;
    for (const PedAgent& ped:peds_) {
        if (ped_is_floored(ped.activity)) continue;
        const float distance=weapon_ped_hit_distance(origin,unit_direction,ped.pos,nearest);
        if (distance<0.f) continue;
        nearest=distance;
        result={true,origin+unit_direction*distance,distance,ped.lane_key,ped.slot,
                false,false};
    }
    // Officers stand in the same street and use the same silhouette. Scanning
    // them AFTER the civilians with the running `nearest` is what keeps one
    // bullet to one body: whichever population owns the closer torso wins.
    for (const VehicleAgent& car:vehicles_) {
        if (!car.police_unit || !police_officer_shootable(car.officer)) continue;
        const float distance=weapon_ped_hit_distance(origin,unit_direction,
                                                     car.officer.pos,nearest);
        if (distance<0.f) continue;
        nearest=distance;
        result={true,origin+unit_direction*distance,distance,car.lane_key,car.slot,
                true,false};
    }
    return result;
}

PedShotHit Crowd::shoot_ped(glm::vec3 origin,glm::vec3 unit_direction,
                           float max_distance,int64_t step) {
    return strike_ped(origin,unit_direction,max_distance,kPistolBodyDamage,
                      /*from_bullet=*/true,step);
}

// A fist reaches about as far as an arm, and it is the SAME query a bullet
// makes — same silhouette, same nearest-body rule, same two populations — so
// there is no second answer to "who did that land on". Only the damage and the
// way the body falls differ, which is the whole of what a punch is.
PedShotHit Crowd::punch_ped(glm::vec3 origin,glm::vec3 unit_direction,
                            float reach_m,int64_t step) {
    return strike_ped(origin,unit_direction,reach_m,kPunchBodyDamage,
                      /*from_bullet=*/false,step);
}

PedShotHit Crowd::strike_ped(glm::vec3 origin,glm::vec3 unit_direction,
                             float max_distance,float damage,bool from_bullet,
                             int64_t step) {
    if (step<0) return {};
    PedShotHit hit=raycast_ped(origin,unit_direction,max_distance);
    if (!hit.hit) return hit;
    if (hit.officer) {
        for (VehicleAgent& car:vehicles_) {
            if (!car.police_unit || car.lane_key!=hit.lane_key || car.slot!=hit.slot)
                continue;
            hit.officer_downed=police_officer_take_bullet(car.officer,
                damage,{unit_direction.x,unit_direction.z});
            if (!from_bullet) car.officer.impact_from_bullet=false;
            // An officer out of health is dead, not resting, so the two flags
            // report the same edge. They stay separate fields because the
            // caller prices a dead officer and a dead civilian differently.
            hit.killed=hit.officer_downed;
            break;
        }
        return hit;
    }
    for (PedAgent& ped:peds_) {
        if (ped.lane_key!=hit.lane_key || ped.slot!=hit.slot) continue;
        const PedLifeTuning& life=tuning_.ped_life;
        const glm::vec2 travel{unit_direction.x,unit_direction.z};
        const float planar_length=glm::length(travel);
        const glm::vec2 fall=planar_length>1e-5f ? travel/planar_length
                                                 : glm::vec2{0.f,-1.f};
        hit.killed=apply_body_damage(ped.health,damage);
        if (!hit.killed) {
            // Still standing, and now running. The round is recorded in the
            // health pool and in the panic TRIGGER, and nowhere else: a wounded
            // person keeps their walk line, their lane and their identity, so
            // the next round finds the same body with less of it left.
            ped.wounded_steps=std::max(ped.wounded_steps,
                static_cast<int64_t>(life.wounded_panic_seconds/kSimDtF));
            break;
        }
        // Out of health. The fall is held forever.
        ped.activity=PedActivity::Dead;
        ped.activity_steps=0;
        ped.panic_seconds=0.f;
        ped.wounded_steps=0;
        ped.speed_mps=0.f;
        ped.impact_dir_xz=fall;
        // Small displacement accompanies the authored bullet fall. Vehicle
        // impacts retain their separate airborne ragdoll response, and a fist
        // borrows that one: a punch does not throw somebody the way a round
        // does, and playing the bullet clip for it looks like a gunshot with
        // no gun.
        ped.impact_speed_mps=3.f;
        ped.impact_from_bullet=from_bullet;
        ped.impact_velocity={fall.x*.65f,.25f,fall.y*.65f};
        break;
    }
    return hit;
}

bool Crowd::ped_crossing_is_clear(LaneRef foot_lane, int64_t step) const {
    if (!graph_ || !graph_->valid(foot_lane)) return true;
    const Lane& lane = graph_->lane(foot_lane);
    const uint32_t jn = lane.junction_to;
    if (jn >= graph_->junction_count()) return true;
    const PedLifeTuning& life = tuning_.ped_life;

    // THE SIGNAL IS THE SAME ONE THE DRIVERS AND THE BULBS READ. Two
    // implementations of a signal phase eventually disagree, and the way that
    // one presents is a pedestrian walking out under a green light.
    if (graph_->junction_control(jn) == JunctionControl::Signal &&
        traffic_signal_phase(*graph_, jn, foot_lane, step, tuning_) !=
            TrafficSignalPhase::Red)
        return false;

    // Whatever the signal says, look. Reading the FROZEN buckets and not live
    // agent state is what makes this answer a property of the population
    // rather than of which agent happened to update first.
    auto occupied = [&](LaneRef lr, bool leaving) {
        if (!graph_->valid(lr) ||
            static_cast<std::size_t>(lr) >= lane_buckets_.size())
            return false;
        const Lane& l = graph_->lane(lr);
        for (const BucketEntry& e : lane_buckets_[lr]) {
            if (vehicles_[e.agent].speed_mps < life.crossing_ignore_speed_mps)
                continue;
            const float to_mouth = leaving ? e.dist : l.length_m - e.dist;
            if (to_mouth <= life.crossing_clear_m) return true;
        }
        return false;
    };
    // The crossing spans the whole carriageway, so both directions of this one
    // road matter: traffic still approaching the mouth on this lane, and
    // traffic that has just left it on the opposite one.
    if (occupied(foot_lane, false)) return false;
    for (LaneRef out : graph_->junction(jn).outgoing)
        if (graph_->valid(out) && graph_->lane(out).edge == lane.edge &&
            occupied(out, true))
            return false;
    return true;
}

void Crowd::step_peds(int64_t step, const VehicleState* player) {
    if (!graph_) return;
    ped_run_downs_.clear();
    const int k = std::max(1, tuning_.ped_sub_rate);
    const float dt = static_cast<float>(k) * kSimDtF;
    const std::size_t table = ped_table_;
    const PedLifeTuning& life = tuning_.ped_life;

    // The player's car, resolved once. Doing it per pedestrian would be the
    // same arithmetic a hundred thousand times for one answer.
    glm::vec2 player_xz{0.0f};
    glm::vec2 player_fwd{1.0f, 0.0f};
    glm::vec2 player_vel{0.0f};
    float player_speed = 0.0f;
    if (player) {
        player_xz = {player->position.x, player->position.z};
        player_vel = {player->velocity.x, player->velocity.z};
        player_speed = glm::length(player_vel);
        const glm::vec3 heading = player->orientation * glm::vec3{0.0f, 0.0f, -1.0f};
        const glm::vec2 flat{heading.x, heading.z};
        if (glm::length(flat) > 1e-5f) player_fwd = glm::normalize(flat);
    }

    stats_.peds_stepped = 0;
    stats_.peds_analytic = 0;
    stats_.ped_neighbour_tests = 0;
    stats_.peds_walking = 0;
    stats_.peds_idling = 0;
    stats_.peds_waiting = 0;
    stats_.peds_alarmed = 0;
    stats_.peds_fleeing = 0;
    stats_.peds_downed = 0;
    stats_.peds_dead = 0;

    for (uint32_t i = 0; i < peds_.size(); ++i) {
        PedAgent& p = peds_[i];
        if (p.mode == AgentMode::Analytic) ++stats_.peds_analytic;
        if (!graph_->valid(p.lane)) continue;
        if (static_cast<int>(step % k) !=
            sub_rate_phase(p.lane_key, p.slot, i, p.spawn_ordinal, k))
            continue;
        ++stats_.peds_stepped;

        // --- neighbours, from the frozen grid -------------------------------
        const glm::vec2 self = ped_pos_frozen_[i];
        const int32_t cx = floor_div(self.x, PED_SEPARATION_RADIUS);
        const int32_t cz = floor_div(self.y, PED_SEPARATION_RADIUS);
        ped_scratch_.clear();
        if (table != 0) {
            for (int dz = -1; dz <= 1; ++dz) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int32_t qx = cx + dx;
                    const int32_t qz = cz + dz;
                    const int64_t want = (static_cast<int64_t>(qx) << 32) |
                                         static_cast<int64_t>(static_cast<uint32_t>(qz));
                    const uint32_t b =
                        static_cast<uint32_t>(cell_hash(qx, qz) & (table - 1u));
                    const uint32_t lo = ped_bucket_start_[b];
                    const uint32_t hi = ped_bucket_start_[b + 1];
                    for (uint32_t e = lo; e < hi; ++e) {
                        const uint32_t j = ped_bucket_items_[e];
                        // The bucket is a SUPERSET of the cell — two cells can
                        // hash to it. Comparing the real cell coordinates is
                        // what stops a colliding cell being counted twice, and
                        // a double-counted neighbour is a ped that sidesteps
                        // twice as hard for no visible reason.
                        if (ped_cell_keys_[j] != want) continue;
                        if (j == i) continue;
                        ++stats_.ped_neighbour_tests;
                        glm::vec2 neighbour = ped_pos_frozen_[j];
                        if (glm::length(neighbour - self) < 1e-4f) {
                            // Separation ignores exact coincidences. Break
                            // those ties using the stable identity ordering.
                            neighbour += glm::vec2{0.013f, 0.017f} * (i < j ? 1.0f : -1.0f);
                        }
                        ped_scratch_.push_back(neighbour);
                    }
                }
            }
        }

        const PedWalkPath& walk = ped_paths_.path(p.walk_path);
        const PedWalkLine& active_line = p.walk_link == PedestrianPaths::invalid
            ? walk.line : walk.links[p.walk_link].line;
        const LanePose base = active_line.pose(p.dist_along_m);
        const glm::vec2 fwd{base.tangent.x, base.tangent.z};

        // --- what is this person doing this step ----------------------------
        //
        // The order below IS the priority order, top to bottom, and it is
        // written as a single fall-through rather than as a table of pairwise
        // transitions on purpose: a state machine with six states has thirty
        // edges and nobody maintains thirty edges correctly.

        // 1. On the floor outranks everything, and every timer here runs
        //    down, so nobody can be stuck in a state they could leave. Dead is
        //    the one state with no timer, and it is deliberately not listed:
        //    it is not something this person is waiting out.
        if (p.activity == PedActivity::Downed) {
            p.activity_steps -= k;
            if (p.activity_steps <= 0) {
                // Not up yet — GETTING up, which takes as long as it takes and
                // is spent standing still. See PedLifeTuning::rising_steps.
                p.activity = PedActivity::Rising;
                p.activity_steps = life.rising_steps;
            }
        } else if (p.activity == PedActivity::Rising) {
            p.activity_steps -= k;
            if (p.activity_steps <= 0) {
                p.activity_steps = 0;
                p.activity = PedActivity::Walking;
            }
        }

        // The thrown body. A point mass over the walk line's own surface —
        // `impact_offset.y` is height ABOVE that surface, so this needs no
        // collider and stays pure. It is the whole of the sim's opinion about
        // where a knocked-down person is; the ragdoll makes it look like a
        // person while it happens.
        //
        // A BODY FALLS THE SAME WAY WHETHER OR NOT IT GETS UP AGAIN, so this
        // runs for Dead as well — and then keeps running, because the drag and
        // the ground skid are what bring it to rest. Gate it on Downed alone
        // and a corpse hangs in the air at the height it was shot at.
        if (ped_holds_impact_pose(p.activity)) {
            p.impact_velocity.y -= life.gravity_mps2 * dt;
            const float air = std::max(0.0f, 1.0f - life.launch_drag * dt);
            p.impact_velocity.x *= air;
            p.impact_velocity.z *= air;
            p.impact_offset += p.impact_velocity * dt;
            if (p.impact_offset.y <= 0.0f) {
                p.impact_offset.y = 0.0f;
                // Down. Keep a little of the bounce so a fast body tumbles on
                // instead of stopping dead on first contact, and scrub the
                // rest off along the ground.
                if (p.impact_velocity.y < 0.0f)
                    p.impact_velocity.y *= -life.ground_bounce;
                const float speed = std::sqrt(
                    p.impact_velocity.x * p.impact_velocity.x +
                    p.impact_velocity.z * p.impact_velocity.z);
                const float left = std::max(0.0f, speed - life.ground_skid * dt);
                const float scale = speed > 1e-4f ? left / speed : 0.0f;
                p.impact_velocity.x *= scale;
                p.impact_velocity.z *= scale;
            }
        } else if (p.activity == PedActivity::Rising) {
            // Held. Somebody pushing themselves up off the road is not also
            // travelling across it, and moving them here is the slide this
            // state exists to remove.
            p.impact_velocity = glm::vec3{0.0f};
        } else if (p.impact_offset != glm::vec3{0.0f}) {
            // Back on their feet, in the middle of the road. Walk it off
            // rather than slide it off: the offset shrinks at a walking pace,
            // so what a player sees is somebody picking themselves up and
            // returning to the pavement.
            p.impact_velocity = glm::vec3{0.0f};
            const float length = glm::length(p.impact_offset);
            const float step_m = life.offset_recover_mps * dt;
            p.impact_offset = length <= step_m
                ? glm::vec3{0.0f}
                : p.impact_offset * ((length - step_m) / length);
        }

        // 2. The player's car. Assessed from the CAR's heading, asking whether
        //    this person is in front of it — a cone drawn from the pedestrian
        //    would never see a car coming up behind them, which is the case
        //    that actually kills people.
        bool panicking = false;
        if (player && !ped_is_floored(p.activity) &&
            std::fabs(player->position.y - p.pos.y) < 2.5f) {
            const glm::vec2 here{p.pos.x, p.pos.z};
            const glm::vec2 my_vel = fwd * p.speed_mps;
            const glm::vec2 to_me = here - player_xz;
            const float range = glm::length(to_me);
            // The car's actual yaw footprint, not a circle around its
            // centre: the bumper is most of a car length ahead of the middle,
            // and a centre circle lets the nose sweep through somebody
            // untouched. Same function, same argument, as the pole guard.
            const BreakawayContact hit = breakaway_contact(
                player->position, player->orientation,
                {kPlayerHalfWidthM, kPlayerHalfLengthM}, p.pos,
                life.body_radius_m);
            if (hit.hit && player_speed >= life.knockdown_speed_mps) {
                // Knocked over. This changes the PERSON and nothing else: no
                // impulse goes back into the car, because a pedestrian that
                // stops a vehicle is a worse bug than one that ignores it.

                // The blow, recorded for the presentation layer. Direction is
                // the car's travel, so a body is thrown down the road rather
                // than toward wherever the car has since driven; speed is the
                // closing speed, which is what a person standing still feels
                // and what a ragdoll will launch from.
                const glm::vec2 travel = player_speed > 1e-4f
                    ? player_vel / player_speed : player_fwd;
                p.impact_dir_xz = travel;
                p.impact_speed_mps =
                    std::max(0.0f, glm::dot(player_vel - my_vel, travel));
                p.impact_from_bullet = false;

                // And what it COST, on the same hundred points a bullet spends
                // from. The closing speed decides both how far this person is
                // thrown and whether they get up, which is the one thing that
                // made the knockdown read as a real event rather than a
                // stumble: a kerb-speed nudge floors somebody unhurt, and
                // thirty miles an hour does not.
                const bool killed = apply_body_damage(
                    p.health, vehicle_impact_damage(p.impact_speed_mps,
                                                    life.knockdown_speed_mps));
                if (killed) {
                    p.activity = PedActivity::Dead;
                    p.activity_steps = 0;
                    ped_run_downs_.push_back({p.lane_key, p.slot, p.pos,
                                              p.impact_speed_mps});
                    // Zeroed, not merely ignored. PedActivity::Dead already
                    // multiplies the walk speed out, but `speed_mps` is also
                    // what the presentation layer reads to pick a walk or a
                    // run cycle, and a corpse must not be carrying a sprint.
                    p.speed_mps = 0.0f;
                } else {
                    p.activity = PedActivity::Downed;
                    p.activity_steps = ped_roll_steps(
                        ped_roll(map_seed_, p.lane_key, p.slot,
                                 p.activity_decisions, kChannelPedDowned),
                        life.downed_min_steps, life.downed_max_steps);
                    ++p.activity_decisions;
                }
                p.panic_seconds = 0.0f;

                // Thrown, from wherever they were standing. The offset is not
                // cleared: a person knocked down while still carrying the last
                // knockdown's offset is thrown on from THERE, not from the
                // lane, or being hit twice teleports them back to the kerb
                // between the two.
                p.impact_velocity = {
                    travel.x * p.impact_speed_mps * life.launch_forward,
                    p.impact_speed_mps * life.launch_lift,
                    travel.y * p.impact_speed_mps * life.launch_forward};
            } else {
                const PlayerHazard hazard = assess_player_hazard(
                    player_xz, player_fwd, player_speed, here, my_vel,
                    life.threat, tuning_.car_length_m);
                const float closing =
                    range > 1e-4f
                        ? glm::dot(player_vel - my_vel, to_me / range)
                        : player_speed;
                panicking = panic_should_trigger(
                    hazard, closing, ped_panic_for(life, p.disposition));
            }
        }

        // 3. Startle and flee are city/traffic_ai.h's panic kernel, unchanged.
        //    Its timer always decays, which is what guarantees that a person
        //    who was frightened once does not sprint for the rest of the run.
        //
        //    A GUNSHOT IS A TRIGGER LIKE ANY OTHER. Holding it down for a few
        //    seconds gets the kernel's sustained-menace behaviour for free —
        //    one flinch, then a run that ends on its own — and keeps a single
        //    definition of what a panic is. Writing the seconds into the timer
        //    instead put the victim in a four-second startle; see
        //    PedLifeTuning::wounded_panic_seconds.
        if (p.wounded_steps > 0) {
            p.wounded_steps = std::max<int64_t>(0, p.wounded_steps - k);
            panicking = true;
        }
        const PanicPhase phase =
            panic_tick(p.panic_seconds, panicking, dt, life.panic);
        if (!ped_is_floored(p.activity)) {
            if (phase == PanicPhase::Flee) {
                p.activity = PedActivity::Fleeing;
                p.activity_steps = 0;
            } else if (phase == PanicPhase::Startle) {
                p.activity = PedActivity::Alarmed;
                p.activity_steps = 0;
            } else if (p.activity == PedActivity::Fleeing ||
                       p.activity == PedActivity::Alarmed) {
                p.activity = PedActivity::Walking;
            } else if (p.activity == PedActivity::Idling) {
                p.activity_steps -= k;
                if (p.activity_steps <= 0) {
                    p.activity_steps = 0;
                    p.activity = PedActivity::Walking;
                }
            } else if (p.activity == PedActivity::Waiting) {
                // A kerb hesitation runs down here; WHO RELEASES IT IS THE
                // CROSSING GATE BELOW, never this timer. Clearing the state
                // when the timer expires instead reads as an obvious
                // simplification and is a loop: the gate would see somebody
                // walking at a kerb, hold them again, and roll a fresh
                // hesitation every single step.
                if (p.activity_steps > 0) p.activity_steps -= k;
                if (p.activity_steps < 0) p.activity_steps = 0;
            }
        }

        // Per-ped variation, keyed on identity. Without the preferred offset
        // every uncrowded ped targets the same line and the crowd walks single
        // file; without the space scale they all defend the same bubble.
        const int32_t kx = static_cast<int32_t>(static_cast<uint32_t>(p.lane_key));
        const int32_t kz =
            static_cast<int32_t>(static_cast<uint32_t>(p.lane_key >> 32));
        const bool on_link = p.walk_link != PedestrianPaths::invalid;
        float pref =
            (city_unit_roll(map_seed_, kx, kz, p.slot, kChannelPedPreferred) *
                 2.0f - 1.0f) * PED_PREFERRED_OFFSET_MAX;
        // Fleeing pulls AWAY FROM THE ROAD. The walking line already sits
        // `side * (half width + offset)` along the lane's right axis and the
        // reversed footway carries the negated side with a negated tangent, so
        // in both directions +side is the direction away from the carriageway.
        // ped_separation() clamps the result to the pavement, so this can
        // never push somebody off the far edge.
        if (p.activity == PedActivity::Fleeing && !on_link)
            pref = std::clamp(pref + walk.side * life.flee_lateral_m,
                              -PED_SEPARATION_MAX_OFFSET,
                              PED_SEPARATION_MAX_OFFSET);
        const float space =
            PED_SPACE_SCALE_MIN +
            city_unit_roll(map_seed_, kx, kz, p.slot, kChannelPedSpace) *
                (PED_SPACE_SCALE_MAX - PED_SPACE_SCALE_MIN);

        const PedSeparation sep =
            ped_separation(self, fwd, ped_scratch_.data(), ped_scratch_.size(),
                           pref, space);
        p.blocked = sep.blocked;
        // Links keep a narrower passing strip; forcing all link offsets to
        // zero would make opposing pedestrians walk through one another.
        const float target = on_link ? std::clamp(sep.lateral_target, -0.65f, 0.65f)
                                     : sep.lateral_target;
        // Nobody sidesteps off the floor. Separation is a walking behaviour —
        // it steers around the people you are about to meet — and a body that
        // is down or pushing itself up is not walking anywhere. Leaving it on
        // drifted them sideways across the pavement the whole time they were
        // on the ground, which is a slide with no animation under it.
        if (!ped_is_floored(p.activity)) {
            p.walk_offset_m +=
                std::clamp(target - p.walk_offset_m, -0.9f * dt, 0.9f * dt);
        }

        // Slow while passing, but keep a little progress: hard stopping both
        // members of a head-on pair made a permanent pavement deadlock.
        const float before_m = p.dist_along_m;
        p.dist_along_m += p.speed_mps * ped_activity_speed_mul(p.activity, life) *
                          (sep.blocked ? 0.18f : 1.0f) * dt;

        // 4. Loitering part way along a stretch of pavement, so people stop at
        //    shopfronts and not only at corners. The mark is keyed to the same
        //    ordinal the arrival roll uses, so it does not move under the
        //    person while they walk toward it, and the strict crossing test
        //    fires it at most once per stretch.
        if (p.activity == PedActivity::Walking && !on_link) {
            const float mark =
                (0.15f + 0.70f * ped_roll(map_seed_, p.lane_key, p.slot,
                                          p.activity_decisions,
                                          kChannelPedLoiterAt)) *
                walk.line.length;
            if (before_m < mark && p.dist_along_m >= mark &&
                ped_roll(map_seed_, p.lane_key, p.slot, p.activity_decisions,
                         kChannelPedIdleChance) < life.idle_chance) {
                p.dist_along_m = mark;
                p.activity = PedActivity::Idling;
                p.activity_steps = ped_roll_steps(
                    ped_roll(map_seed_, p.lane_key, p.slot, p.activity_decisions,
                             kChannelPedIdleLength),
                    life.idle_min_steps, life.idle_max_steps);
                ++p.activity_decisions;
            }
        }

        for (int transition = 0; transition < 16; ++transition) {
            const PedWalkPath& current = ped_paths_.path(p.walk_path);
            const float length = p.walk_link == PedestrianPaths::invalid
                ? current.line.length : current.links[p.walk_link].line.length;
            if (p.dist_along_m < length) break;
            if (p.walk_link == PedestrianPaths::invalid) {
                // Which way this person goes is chosen BEFORE the kerb test and
                // the decision counter is spent only when they actually step
                // off, so a person held at a red does not re-roll their route
                // every step and end up facing a different way each tick.
                const uint32_t link = ped_paths_.choose(
                    p.walk_path, map_seed_, p.lane_key, p.slot, p.walk_decisions);
                if (current.links[link].crossing) {
                    const bool composed =
                        ped_is_floored(p.activity) ||
                        p.activity == PedActivity::Fleeing ||
                        p.activity == PedActivity::Alarmed;
                    // First touch of this kerb: look, always, even when the
                    // road is empty. Stepping straight off without a pause is
                    // the tell that nobody decided anything. Somebody already
                    // panicking or on the floor skips the courtesy.
                    if (!composed && p.activity != PedActivity::Waiting) {
                        p.dist_along_m = length;
                        p.activity = PedActivity::Waiting;
                        p.activity_steps = ped_roll_steps(
                            ped_roll(map_seed_, p.lane_key, p.slot,
                                     p.activity_decisions, kChannelPedKerbWait),
                            life.kerb_wait_min_steps, life.kerb_wait_max_steps);
                        ++p.activity_decisions;
                        break;
                    }
                    if ((p.activity == PedActivity::Waiting &&
                         p.activity_steps > 0) ||
                        !ped_crossing_is_clear(current.lane, step)) {
                        // Held. Clamped rather than left past the end, so the
                        // pose stays put instead of jittering forward and being
                        // pulled back on every step.
                        p.dist_along_m = length;
                        break;
                    }
                }
                p.dist_along_m -= length;
                p.walk_link = link;
                ++p.walk_decisions;
                if (p.activity == PedActivity::Waiting)
                    p.activity = PedActivity::Walking;
            } else {
                p.dist_along_m -= length;
                p.walk_path = current.links[p.walk_link].to;
                p.walk_link = PedestrianPaths::invalid;
                const PedWalkPath& next = ped_paths_.path(p.walk_path);
                p.lane = next.lane;
                const Lane& lane = graph_->lane(p.lane);
                p.base_lateral_m = next.side * (lane.width_m * 0.5f + ambient_.sidewalk_offset_m)
                                   - lane.lateral_offset_m;
                // 5. Arriving on a new stretch of pavement is the other place a
                //    person decides to stop for a while.
                if (p.activity == PedActivity::Walking &&
                    ped_roll(map_seed_, p.lane_key, p.slot, p.activity_decisions,
                             kChannelPedIdleChance) < life.idle_chance) {
                    p.activity = PedActivity::Idling;
                    p.activity_steps = ped_roll_steps(
                        ped_roll(map_seed_, p.lane_key, p.slot,
                                 p.activity_decisions, kChannelPedIdleLength),
                        life.idle_min_steps, life.idle_max_steps);
                }
                ++p.activity_decisions;
            }
        }
        p.last_dist_m = p.dist_along_m;
        const PedWalkPath& current = ped_paths_.path(p.walk_path);
        const PedWalkLine& line = p.walk_link == PedestrianPaths::invalid
            ? current.line : current.links[p.walk_link].line;
        p.lateral_m = p.base_lateral_m + p.walk_offset_m;
        const LanePose pose = line.pose(p.dist_along_m, p.walk_offset_m);
        p.pos = pose.position + p.impact_offset;
        p.fwd = pose.tangent;

        // Somebody walking back to the pavement FACES the pavement.
        //
        // The offset shrinks along its own direction while the walk line
        // carries them down the street, so their real velocity is the sum of
        // the two — and pointing them down the lane instead means a walk cycle
        // aimed one way while the body travels another. That is a slide, and
        // it is the same slide the get-up had, arriving a few seconds later.
        if (p.impact_offset != glm::vec3{0.0f} &&
            !ped_is_floored(p.activity)) {
            const glm::vec2 back{-p.impact_offset.x, -p.impact_offset.z};
            const float reach = glm::length(back);
            if (reach > 1e-3f) {
                const glm::vec2 along{pose.tangent.x, pose.tangent.z};
                const glm::vec2 travel =
                    along * std::max(p.speed_mps, 0.0f) +
                    (back / reach) * life.offset_recover_mps;
                const float len = glm::length(travel);
                if (len > 1e-4f)
                    p.fwd = glm::vec3{travel.x / len, 0.0f, travel.y / len};
            }
        }
    }

    // The activity census is a second pass over the WHOLE active set, not a
    // tally inside the loop above: the loop only visits the sub-rate slice, so
    // counting there would report the schedule instead of the street.
    for (const PedAgent& p : peds_) {
        switch (p.activity) {
            case PedActivity::Walking: ++stats_.peds_walking; break;
            case PedActivity::Idling: ++stats_.peds_idling; break;
            case PedActivity::Waiting: ++stats_.peds_waiting; break;
            case PedActivity::Alarmed: ++stats_.peds_alarmed; break;
            case PedActivity::Fleeing: ++stats_.peds_fleeing; break;
            case PedActivity::Downed: ++stats_.peds_downed; break;
            case PedActivity::Rising: ++stats_.peds_downed; break;
            case PedActivity::Dead: ++stats_.peds_dead; break;
        }
    }
}

// ---------------------------------------------------------------------------
//  publish
// ---------------------------------------------------------------------------

namespace {

Transform agent_transform(const glm::vec3& pos, const glm::vec3& fwd) {
    Transform t;
    t.position = pos;
    // Transform::forward() is -Z, so the yaw that maps -Z onto `fwd` is
    // atan2(-fwd.x, -fwd.z). Getting the signs wrong here puts every car in
    // the city in reverse, which is exactly the sort of thing that looks like
    // a physics bug for a day.
    const float yaw = std::atan2(-fwd.x, -fwd.z);
    t.rotation = glm::quat(glm::vec3{0.0f, yaw, 0.0f});
    return t;
}

const AABB kCarBounds{glm::vec3{-0.9f, 0.0f, -2.2f}, glm::vec3{0.9f, 1.5f, 2.2f}};
const AABB kPedBounds{glm::vec3{-0.3f, 0.0f, -0.3f}, glm::vec3{0.3f, 1.8f, 0.3f}};

}  // namespace

void Crowd::publish(Scene& scene) {
    if (!dead_nodes_.empty()) {
        scene.remove_many(dead_nodes_);
        dead_nodes_.clear();
    }
    for (VehicleAgent& v : vehicles_) {
        const Transform t = agent_transform(v.pos, v.fwd);
        if (v.node == kInvalidId) {
            Renderable r;
            r.mesh = 1;
            r.material = 1;
            v.node = scene.create(r, t, kCarBounds);
        } else {
            scene.set_transform(v.node, t);
        }
    }
    for (PedAgent& p : peds_) {
        const Transform t = agent_transform(p.pos, p.fwd);
        if (p.node == kInvalidId) {
            Renderable r;
            r.mesh = 2;
            r.material = 2;
            p.node = scene.create(r, t, kPedBounds);
        } else {
            scene.set_transform(p.node, t);
        }
    }
}

// ---------------------------------------------------------------------------
//  digests
// ---------------------------------------------------------------------------

uint64_t Crowd::population_hash() const {
    // THE LANE GOES IN AS ITS KEY, NEVER AS ITS LaneRef. A LaneRef is an index
    // into one build; hashing it makes this digest report a divergence every
    // time the spine table is reordered, which is precisely the case the
    // digest exists to prove is FINE. (It did exactly that, once, and the
    // failure read as "state diverged under reordering" for twenty minutes.)
    uint64_t h = 0xA5A5A5A5DEADBEEFull;
    for (const VehicleAgent& v : vehicles_) {
        h = mix_bits(h, v.lane_key);
        h = mix_bits(h, v.slot);
        h = mix_bits(h, static_cast<uint64_t>(v.mode));
        h = mix_bits(h, v.snowplow_unit ? 1u : 0u);
        h = mix_bits(h, v.police_unit ? 1u : 0u);
        h = mix_bits(h, v.police_pursuit ? 1u : 0u);
        h = mix_bits(h, static_cast<uint64_t>(v.emergency_yield));
        h = mix_f32(h, v.emergency_resume_s);
        h = mix_f32(h, v.roadside_offset_m);
        h = mix_f32(h, v.maneuver_steer_rad);
        h = mix_bits(h, static_cast<uint64_t>(v.maneuver.kind));
        h = mix_bits(h, v.maneuver.count);
        h = mix_bits(h, graph_ && graph_->valid(v.maneuver.destination)
            ? graph_->lane(v.maneuver.destination).key : ~uint64_t{0});
        h = mix_f32(h, v.maneuver.progress_m);
        h = mix_f32(h, v.maneuver.length_m);
        h = mix_f32(h, v.maneuver.speed_limit_mps);
        h = mix_f32(h, v.maneuver.end_station_m);
        h = mix_f32(h, v.maneuver.end_offset_m);
        for (uint8_t c = 0; c < v.maneuver.count; ++c) {
            for (auto point : {v.maneuver.curves[c].p0, v.maneuver.curves[c].p1,
                               v.maneuver.curves[c].p2, v.maneuver.curves[c].p3})
                for (int axis = 0; axis < 3; ++axis) h = mix_f32(h, point[axis]);
        }
        h = mix_bits(h, static_cast<uint64_t>(v.officer.phase));
        h = mix_bits(h, static_cast<uint64_t>(v.officer.transition.direction));
        h = mix_bits(h, v.officer.transition.tick);
        h = mix_bits(h, v.officer.stationary_ticks);
        h = mix_bits(h, v.officer.armed ? 1u : 0u);
        h = mix_f32(h, v.officer.health);
        h = mix_bits(h, v.officer.dead ? 1u : 0u);
        h = mix_bits(h, v.officer.impact_from_bullet ? 1u : 0u);
        h = mix_f32(h, v.officer.impact_dir_xz.x);
        h = mix_f32(h, v.officer.impact_dir_xz.y);
        h = mix_bits(h, static_cast<uint64_t>(v.officer.next_shot_step));
        h = mix_bits(h, v.officer.shots_fired);
        h = mix_bits(h, v.officer.weapon_flash_ticks);
        for (int axis = 0; axis < 3; ++axis) {
            h = mix_f32(h, v.officer.pos[axis]);
            h = mix_f32(h, v.officer.previous_pos[axis]);
            h = mix_f32(h, v.officer.door_pos[axis]);
        }
        h = mix_f32(h, v.officer.heading);
        h = mix_f32(h, v.officer.previous_heading);
        h = mix_f32(h, v.officer.door_heading);
        h = mix_f32(h, v.officer.distance_walked_m);
        h = mix_bits(h, v.police_route_index);
        h = mix_bits(h, static_cast<uint64_t>(v.police_last_replan_step));
        h = mix_f32(h, v.police_last_target.x);
        h = mix_f32(h, v.police_last_target.y);
        h = mix_bits(h, v.police_route.size());
        for (LaneRef route_lane : v.police_route) {
            h = mix_bits(h, graph_ && graph_->valid(route_lane)
                                ? graph_->lane(route_lane).key
                                : 0xFFFFFFFFFFFFFFFFull);
        }
        h = mix_bits(h, graph_ && graph_->valid(v.lane)
                            ? graph_->lane(v.lane).key
                            : 0xFFFFFFFFFFFFFFFFull);
        h = mix_f32(h, v.dist_along_m);
        h = mix_f32(h, v.speed_mps);
        h = mix_bits(h, v.stop_junction);
        h = mix_f32(h, v.delay_seconds);
        h = mix_f32(h, v.obstruction_wait_s);
        h = mix_bits(h, v.maneuver_decisions);
        h = mix_f32(h, v.lane_change_cooldown_s);
        h = mix_bits(h, static_cast<uint64_t>(v.jam_steps));
        h = mix_bits(h, static_cast<uint64_t>(v.recovery_action));
        h = mix_bits(h, static_cast<uint64_t>(v.recovery_action_start_steps));
        h = mix_bits(h, v.jam_retire ? 1u : 0u);
        h = mix_bits(h, v.honk_player ? 1u : 0u);
        h = mix_bits(h, v.honk_player_fire ? 1u : 0u);
        h = mix_bits(h, v.honk_debounce.was_honking ? 1u : 0u);
        {
            uint64_t bits = 0;
            std::memcpy(&bits, &v.honk_debounce.last_honk_time, sizeof bits);
            h = mix_bits(h, bits);
        }
        h = mix_bits(h, static_cast<uint64_t>(v.stop_wait_steps));
        h = mix_bits(h, static_cast<uint64_t>(v.stop_arrival_step));
        h = mix_bits(h, v.stop_completed ? 1u : 0u);
        h = mix_bits(h, v.committed_junction);
        h = mix_bits(h, v.committed_approach_lane);
        h = mix_bits(h, v.committed_exit_lane);
        h = mix_bits(h, v.turn_from_lane);
        h = mix_bits(h, static_cast<uint64_t>(v.active_turn_kind));
        h = mix_f32(h, v.turn_progress_m);
        h = mix_f32(h, v.turn_length_m);
        h = mix_f32(h, v.turn_entry_m);
        h = mix_f32(h, v.turn_exit_m);
        h = mix_f32(h, v.turn_steer_rad);
        h = mix_bits(h, static_cast<uint64_t>(v.blocked_exit_steps));
        h = mix_bits(h, static_cast<uint64_t>(v.intersection_stall_steps));
        h = mix_f32(h, v.collision_offset_xz.x);
        h = mix_f32(h, v.collision_offset_xz.y);
        h = mix_f32(h, v.collision_velocity_xz.x);
        h = mix_f32(h, v.collision_velocity_xz.y);
        h = mix_f32(h, v.collision_yaw_rad);
        h = mix_f32(h, v.collision_yaw_velocity);
        h = mix_f32(h, v.collision_recovery_seconds);
        h = mix_f32(h, v.collision_steer_rad);
        h = mix_f32(h, v.mechanical.oil_remaining);
        h = mix_f32(h, v.mechanical.fuel_remaining);
        h = mix_f32(h, v.mechanical.oil_lifetime_s);
        h = mix_f32(h, v.mechanical.fuel_lifetime_s);
        h = mix_bits(h, v.mechanical.engine_failed ? 1u:0u);
        for (float damage : v.body_damage.zones) h = mix_f32(h, damage);
        for (const VehicleDentStamp& stamp : v.body_damage.stamps) {
            h = mix_f32(h, stamp.contact_xz.x);
            h = mix_f32(h, stamp.contact_xz.y);
            h = mix_f32(h, stamp.severity);
            h = mix_f32(h, stamp.motion_angle);
            h = mix_f32(h, stamp.radius);
            h = mix_f32(h, stamp.height);
            h = mix_f32(h, stamp.glancing);
        }
        h = mix_f32(h, v.pos.x);
        h = mix_f32(h, v.pos.y);
        h = mix_f32(h, v.pos.z);
    }
    h = mix_f32(h, police_search_centre_.x);
    h = mix_f32(h, police_search_centre_.y);
    h = mix_f32(h, police_search_velocity_.x);
    h = mix_f32(h, police_search_velocity_.y);
    h = mix_bits(h, static_cast<uint64_t>(police_last_seen_step_));
    h = mix_bits(h, police_searching_ ? 1u : 0u);
    h = mix_bits(h, police_level_rose_ ? 1u : 0u);
    h = mix_bits(h, police_dispatch_serial_);
    h = mix_bits(h, police_response_gate_.pending ? 1u : 0u);
    h = mix_f32(h, police_response_gate_.delay);
    h = mix_bits(h, static_cast<uint64_t>(police_responding_level_));
    h = mix_bits(h, static_cast<uint64_t>(police_dispatch_radio_step_));
    h = mix_f32(h, police_armed_delay_s_);
    h = mix_bits(h, police_escalated_from_zero_ ? 1u : 0u);
    h = mix_bits(h, police_spawn_pending_ ? 1u : 0u);
    for (const PedAgent& p : peds_) {
        h = mix_bits(h, p.lane_key);
        h = mix_bits(h, p.slot);
        h = mix_bits(h, static_cast<uint64_t>(p.mode));
        h = mix_bits(h, graph_ && graph_->valid(p.lane)
                            ? graph_->lane(p.lane).key
                            : 0xFFFFFFFFFFFFFFFFull);
        h = mix_f32(h, p.dist_along_m);
        h = mix_f32(h, p.lateral_m);
        h = mix_bits(h, p.walk_link);
        h = mix_bits(h, p.walk_decisions);
        h = mix_bits(h, ped_paths_.valid(p.walk_path)
            ? static_cast<uint64_t>(ped_paths_.path(p.walk_path).side > 0) : 0);
        h = mix_f32(h, p.walk_offset_m);
        // The activity machine is in the digest for the same reason everything
        // else here is: a state the digest cannot see is a state the scan-order
        // and reordering suites are not actually proving anything about.
        h = mix_bits(h, static_cast<uint64_t>(p.activity));
        h = mix_bits(h, static_cast<uint64_t>(p.disposition));
        h = mix_bits(h, static_cast<uint64_t>(p.activity_steps));
        h = mix_bits(h, p.activity_decisions);
        h = mix_f32(h, p.panic_seconds);
        // Damage, for exactly the reason the activity machine is here: a state
        // the digest cannot see is a state the scan-order and reordering
        // suites are not actually proving anything about. Two crowds that
        // agree about who is standing and disagree about how much health they
        // have left are not the same crowd.
        h = mix_f32(h, p.health);
        h = mix_bits(h, static_cast<uint64_t>(p.wounded_steps));
        h = mix_f32(h, p.pos.x);
        h = mix_f32(h, p.pos.y);
        h = mix_f32(h, p.pos.z);
    }
    return h;
}

uint64_t Crowd::membership_hash() const {
    uint64_t h = 0x1234567898765432ull;
    for (const VehicleAgent& v : vehicles_) {
        h = mix_bits(h, v.lane_key);
        h = mix_bits(h, v.slot);
    }
    for (const PedAgent& p : peds_) {
        h = mix_bits(h, p.lane_key ^ 0xFFull);
        h = mix_bits(h, p.slot);
    }
    return h;
}

}  // namespace apricot
