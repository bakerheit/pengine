#include "city/traffic_ai.h"

#include <algorithm>
#include <cmath>
#include <limits>


namespace apricot {

TrafficDirInfo traffic_dir_info(GridDir dir) {
    switch (dir) {
        case GridDir::East:
            return {+1,  0, {+1.f, 0.f,  0.f}, { 0.f, 0.f, -1.f}, 270.f};
        case GridDir::North:
            return { 0, +1, { 0.f, 0.f, +1.f}, {+1.f, 0.f,  0.f}, 180.f};
        case GridDir::West:
            return {-1,  0, {-1.f, 0.f,  0.f}, { 0.f, 0.f, +1.f},  90.f};
        case GridDir::South:
            return { 0, -1, { 0.f, 0.f, -1.f}, {-1.f, 0.f,  0.f},   0.f};
    }
    return {+1, 0, {+1.f, 0.f, 0.f}, {0.f, 0.f, -1.f}, 270.f};
}

DriverProfile make_driver_profile(DriverProfileKind kind) {
    DriverProfile p;
    p.kind = kind;
    switch (kind) {
        case DriverProfileKind::Cautious:
            p.speed_mul = 0.82f;
            p.headway = 1.75f;
            p.min_gap = 3.8f;
            p.accel = 3.2f;
            p.brake = 7.0f;
            p.patience_seconds = 7.0f;
            p.safe_lane_gap = 34.f;
            p.honk_after = 5.5f;
            p.yellow_bias = 0.15f;
            break;
        case DriverProfileKind::Normal:
            p.speed_mul = 1.0f;
            p.headway = 1.35f;
            p.min_gap = 2.8f;
            p.accel = 4.5f;
            p.brake = 8.0f;
            p.patience_seconds = 5.0f;
            p.safe_lane_gap = 28.f;
            p.honk_after = 3.0f;
            p.yellow_bias = 0.5f;
            break;
        case DriverProfileKind::Impatient:
            p.speed_mul = 1.12f;
            p.headway = 1.05f;
            p.min_gap = 2.2f;
            p.accel = 5.6f;
            p.brake = 9.5f;
            p.patience_seconds = 3.0f;
            p.safe_lane_gap = 24.f;
            p.honk_after = 1.8f;
            p.yellow_bias = 0.72f;
            break;
        case DriverProfileKind::AggressiveLite:
            p.speed_mul = 1.22f;
            p.headway = 0.9f;
            p.min_gap = 1.8f;
            p.accel = 6.4f;
            p.brake = 10.5f;
            p.patience_seconds = 2.0f;
            p.safe_lane_gap = 20.f;
            p.honk_after = 1.0f;
            p.yellow_bias = 0.85f;
            break;
    }
    return p;
}

// The mix, and only the mix. Cut points are the probablecause percentages
// (18 / 78 / 94 out of 100) expressed against a [0,1) roll, so the population
// of drivers on the street is unchanged by the re-key — only where the number
// comes from changed. See the header for why that mattered.
DriverProfile driver_profile_from_roll(float roll) {
    if (roll < 0.18f) return make_driver_profile(DriverProfileKind::Cautious);
    if (roll < 0.78f) return make_driver_profile(DriverProfileKind::Normal);
    if (roll < 0.94f) return make_driver_profile(DriverProfileKind::Impatient);
    return make_driver_profile(DriverProfileKind::AggressiveLite);
}

DriverProfile driver_profile_for(uint64_t seed, int32_t cell_x, int32_t cell_z,
                                 uint32_t slot) {
    return driver_profile_from_roll(
        city_unit_roll(seed, cell_x, cell_z, slot, kChannelDriverProfile));
}

float traffic_follow_speed_for_gap(float gap, const DriverProfile& profile) {
    return std::max(0.f, (gap - profile.min_gap) / profile.headway);
}

bool traffic_should_stop_for_yellow(float distance_to_stop, float speed,
                                    const DriverProfile& profile) {
    float stopping_dist = (speed * speed)
        / (2.f * std::max(profile.brake, 0.1f));
    return distance_to_stop > stopping_dist && profile.yellow_bias < 0.7f;
}

float effective_min_gap(const DriverProfile& profile, float floor) {
    return std::max(profile.min_gap, floor);
}

bool traffic_profile_may_pass_jam(const DriverProfile& profile,
                                  float blocked_seconds) {
    switch (profile.kind) {
        case DriverProfileKind::Cautious:
            return false;
        case DriverProfileKind::Normal:
            return blocked_seconds >= profile.patience_seconds + 7.f;
        case DriverProfileKind::Impatient:
        case DriverProfileKind::AggressiveLite:
            return blocked_seconds >= profile.patience_seconds;
    }
    return false;
}

bool turner_should_yield(const TurnYieldCandidate& o, float my_to_jn,
                         float lookahead, float hold_zone, float stuck_speed,
                         bool impatient) {
    // Not on the opposite approach / beyond negotiation range: no constraint.
    if (!o.opposing) return false;
    if (o.to_junction > lookahead) return false;

    // (ii) Committed — past its line / mid-turn. Never drive into it, moving or
    // not (a wedged committed car holds me at the line, which beats joining the
    // wedge). One-way by construction: MY gate only runs pre-commit, so once I
    // commit the roles flip and the other car holds instead.
    if (o.committed) return true;

    // (iii) Two pre-commit crossing turners (the opposing-lefts deadlock):
    // closer-to-box proceeds; a near-tie falls to the stable antisymmetric
    // pick. Same 0.5 m epsilon and structure as clause (b)'s cross-group rule,
    // so for any pair exactly one yields — deadlock-free by construction.
    if (o.crossing) {
        if (o.to_junction < my_to_jn - 0.5f) return true;   // they're closer: hold
        if (o.to_junction > my_to_jn + 0.5f) return false;  // I'm closer: proceed
        return o.tie_winner;
    }

    // (i) Oncoming straight-through (or a non-crossing right-turner): right of
    // way while actually ROLLING toward the box. A stopped oncoming car is not
    // entering the box and never holds — the anti-starvation half: a stationary
    // queue only gates via its front car as it moves off. A long-waiting
    // impatient turner shrinks its acceptance window to the hold zone and takes
    // tighter (but still real) gaps, mirroring the jam-pass escalation.
    if (o.speed <= stuck_speed) return false;
    return o.to_junction <= (impatient ? hold_zone : lookahead);
}

bool traffic_should_despawn_jam(bool blocked, float blocked_seconds,
                                float stuck_despawn_seconds,
                                bool leader_is_dynamic,
                                bool legacy_instant,
                                bool maneuvers_exhausted,
                                float dist_from_player,
                                float min_player_dist) {
    // PCG-009 base — all three must hold in EITHER mode: actively blocked,
    // past the ceiling, and the immediate blocker is a non-AI obstacle. The
    // leader_is_dynamic gate is what makes this structurally impossible inside
    // a signal queue (the blocker there is an AI car).
    const bool base = blocked
                   && leader_is_dynamic
                   && blocked_seconds > stuck_despawn_seconds;
    if (!base) return false;
    if (legacy_instant) return true;   // the shipped PCG-009 behaviour (A/B)

    // PCG-165 last resort: only after the recovery ladder is exhausted, and
    // only far from the player (negative == no player reference == far).
    return maneuvers_exhausted
        && (dist_from_player < 0.f || dist_from_player >= min_player_dist);
}

namespace {

// PCG-158: the recovery ladder, escalation order. Indexable so the kernel can
// scan "strictly after current" without a hand-rolled transition table.
constexpr RecoveryAction kRecoveryLadder[] = {
    RecoveryAction::Nudge,
    RecoveryAction::Reverse,
    RecoveryAction::ThreePointTurn,
    RecoveryAction::Reroute,
};
constexpr int kLadderLen =
    static_cast<int>(sizeof(kRecoveryLadder) / sizeof(kRecoveryLadder[0]));

bool recovery_eligible(RecoveryAction a, const RecoveryView& v,
                       const RecoveryTuning& t) {
    switch (a) {
        case RecoveryAction::Nudge:
            // Only a STATIC obstacle can be edged around: an AiJam is a
            // legitimate queue (jam-passing owns impatient passes) and a
            // mid-box standoff has no shoulder to edge into.
            return (v.blocker == RecoveryBlocker::StaticWreck
                    || v.blocker == RecoveryBlocker::Player)
                && v.shoulder_clear >= t.nudge_min_shoulder;
        case RecoveryAction::Reverse:
            return v.rear_gap >= t.reverse_min_rear;
        case RecoveryAction::ThreePointTurn:
            return v.road_bidirectional && v.rear_gap >= t.turn_min_rear;
        case RecoveryAction::Reroute:
            return v.reroute_available;
        default:
            return false;
    }
}

float recovery_budget(RecoveryAction a, const RecoveryTuning& t) {
    switch (a) {
        case RecoveryAction::Nudge:          return t.nudge_budget_s;
        case RecoveryAction::Reverse:        return t.reverse_budget_s;
        case RecoveryAction::ThreePointTurn: return t.turn_budget_s;
        case RecoveryAction::Reroute:        return t.reroute_budget_s;
        default:                             return 0.f;
    }
}

int ladder_index(RecoveryAction a) {
    for (int i = 0; i < kLadderLen; ++i)
        if (kRecoveryLadder[i] == a) return i;
    return -1;   // None / GiveUp: not on the ladder
}

} // anonymous namespace

RecoveryAction recovery_plan(const RecoveryView& v, RecoveryAction current,
                             float elapsed_in_action, const RecoveryTuning& t) {
    // Not deep-blocked: no recovery. (The caller only invokes this from the
    // BlockedRecovery entry, but the guard keeps the kernel total.)
    if (v.blocker == RecoveryBlocker::None) return RecoveryAction::None;

    // Keep a running action while it is still eligible and within its bounded
    // budget — no per-frame thrash between actions.
    const int cur = ladder_index(current);
    if (cur >= 0
        && recovery_eligible(current, v, t)
        && elapsed_in_action < recovery_budget(current, t))
        return current;

    // Escalate: the first eligible action STRICTLY AFTER `current` on the
    // ladder. Monotone within a cycle, so a cycle always terminates.
    for (int i = cur + 1; i < kLadderLen; ++i)
        if (recovery_eligible(kRecoveryLadder[i], v, t))
            return kRecoveryLadder[i];

    // Ladder exhausted. Far from the player (or no player reference — the
    // PCG-157 negative sentinel) the car may give up: the clean, unseen
    // despawn last resort (the PCG-165 gate consumes this). NEAR the player a
    // car must never blink out — retry the ladder from the top (traffic moves;
    // conditions change), or hold (None) if nothing is eligible at all.
    if (v.dist_from_player < 0.f
        || v.dist_from_player >= t.giveup_min_player_dist)
        return RecoveryAction::GiveUp;
    for (int i = 0; i < kLadderLen; ++i)
        if (recovery_eligible(kRecoveryLadder[i], v, t))
            return kRecoveryLadder[i];
    return RecoveryAction::None;
}

NudgePlan nudge_pick_target(float blocker_lat, float blocker_half,
                            float my_half, float margin, float mid_offset) {
    NudgePlan out;
    const float need = 2.f * my_half + margin;   // corridor a pass must fit

    // Kerb side (+): corridor from the blocker's outboard edge to the kerb
    // line, centre bounded so the body stays on the pavement.
    const float curb_corridor = mid_offset - (blocker_lat + blocker_half);
    const bool  curb_ok       = curb_corridor >= need;
    const float curb_target   = blocker_lat + blocker_half + my_half + margin;

    // Centreline side (−): corridor from the blocker's inboard edge to the
    // road centreline, centre bounded AT the centreline.
    const float ctr_corridor = (blocker_lat - blocker_half) - (-mid_offset);
    const bool  ctr_ok       = ctr_corridor >= need;
    const float ctr_target   = blocker_lat - blocker_half - my_half - margin;

    if (!curb_ok && !ctr_ok) return out;         // no side fits: not viable
    const bool use_curb =
        curb_ok && (!ctr_ok || std::abs(curb_target) <= std::abs(ctr_target));
    out.viable   = true;
    out.target   = use_curb ? curb_target : ctr_target;
    out.corridor = use_curb ? curb_corridor : ctr_corridor;
    return out;
}

// ---- PCG-161: precision low-speed maneuver controller ----------------------

ManeuverCmd maneuver_speed_cmd(float v_signed, float v_target,
                               const ManeuverTuning& t) {
    ManeuverCmd cmd;
    // Target ~ zero: stand on the brake (the Vehicle brake opposes current
    // motion and its impulse cap prevents sign-flip jitter at v -> 0).
    if (std::abs(v_target) <= t.stop_speed * 0.5f) {
        cmd.brake = 1.f;
        return cmd;
    }
    const float err = v_target - v_signed;
    const bool same_dir_shortfall =
        (v_target > 0.f && err > 0.f) || (v_target < 0.f && err < 0.f);
    if (same_dir_shortfall) {
        // Under the target in the target's direction: proportional signed
        // throttle. (Also covers rolling the WRONG way: reverse thrust while
        // still creeping forward acts as a brake in the Vehicle model.)
        cmd.throttle = std::clamp(err * t.throttle_per_ms, -1.f, 1.f);
    } else {
        // Past the target (governor): proportional brake.
        cmd.brake = std::clamp(std::abs(err) * t.brake_per_ms, 0.f, 1.f);
    }
    return cmd;
}

ReverseStep maneuver_reverse_cmd(float dist_to_go, float v_signed, float steer,
                                 const ManeuverTuning& t) {
    ReverseStep r;
    if (dist_to_go <= t.stop_tol) {
        r.cmd.brake = 1.f;
        r.done = std::abs(v_signed) <= t.stop_speed;
        return r;
    }
    // Approach profile: crawl on the open stretch, P-decelerate toward the
    // stop target so the brake-to-stop lands inside stop_tol.
    const float v_target = -std::min(t.crawl_speed,
                                     t.approach_gain * dist_to_go);
    r.cmd = maneuver_speed_cmd(v_signed, v_target, t);
    r.cmd.steer = std::clamp(steer, -1.f, 1.f);
    return r;
}

ThreePointStep maneuver_three_point_cmd(const ThreePointView& v,
                                        const ManeuverTuning& t) {
    ThreePointStep r;
    r.next = v.phase;
    switch (v.phase) {
        case TurnPhase::ReverseArc: {
            // Leg ends when the nose has swung far enough that the forward
            // arc can finish (|err| under turn_switch_deg), or the leg's
            // travel/clearance allowance is USED UP — dist_left ≤ stop_tol,
            // not travel ≥ max: the approach profile brakes the car to a stop
            // stop_tol short of the cap, so comparing against the raw cap
            // would park the car mid-leg and never advance the phase (caught
            // by the closed-loop Vehicle test).
            const float dist_left =
                std::min(t.leg_travel_max - v.leg_travel,
                         v.leg_clear - t.leg_stop_margin);
            if (std::abs(v.heading_err_deg) <= t.turn_switch_deg
                || dist_left <= t.stop_tol) {
                r.next = TurnPhase::BrakeAfterReverse;
                r.cmd.brake = 1.f;
                return r;
            }
            ReverseStep rev =
                maneuver_reverse_cmd(dist_left, v.v_signed, 0.f, t);
            r.cmd = rev.cmd;
            // Reversing: yaw_rate ~ (-v * steer) with v < 0, so steer takes
            // the SIGN OF the error to swing the nose toward the target.
            r.cmd.steer = (v.heading_err_deg > 0.f) ? 1.f : -1.f;
            return r;
        }
        case TurnPhase::BrakeAfterReverse:
            r.cmd.brake = 1.f;
            if (std::abs(v.v_signed) <= t.stop_speed)
                r.next = TurnPhase::ForwardArc;
            return r;
        case TurnPhase::ForwardArc: {
            // Same dist_left-based leg end as the reverse arc (see above).
            const float dist_left =
                std::min(t.leg_travel_max - v.leg_travel,
                         v.leg_clear - t.leg_stop_margin);
            if (std::abs(v.heading_err_deg) <= t.done_heading_deg
                || dist_left <= t.stop_tol) {
                r.next = TurnPhase::BrakeAfterForward;
                r.cmd.brake = 1.f;
                return r;
            }
            const float v_target = std::min(t.crawl_speed,
                                            t.approach_gain * dist_left);
            r.cmd = maneuver_speed_cmd(v.v_signed, v_target, t);
            // Forward: yaw_rate ~ (-v * steer) with v > 0, so steer takes the
            // OPPOSITE sign of the error.
            r.cmd.steer = (v.heading_err_deg > 0.f) ? -1.f : 1.f;
            return r;
        }
        case TurnPhase::BrakeAfterForward:
            r.cmd.brake = 1.f;
            if (std::abs(v.v_signed) <= t.stop_speed) {
                // At full lock the default sedan swings ~60 deg per 6 m leg
                // (radius ~5.6 m), so a 180 needs more than one cycle: if the
                // heading is not yet inside the done tolerance, loop back for
                // another reverse/forward pair — a 5-point (N-point) turn.
                // Termination is owned by the caller's action BUDGET
                // (RecoveryTuning::turn_budget_s), the §3.5 guardrail.
                r.next = (std::abs(v.heading_err_deg) <= t.done_heading_deg)
                       ? TurnPhase::Done
                       : TurnPhase::ReverseArc;
            }
            return r;
        case TurnPhase::Done:
            r.cmd.brake = 1.f;
            return r;
    }
    return r;
}

ManeuverCmd drive_back_cmd(const DriveBackView& v, const ManeuverTuning& t) {
    ManeuverCmd cmd;
    // Ease the crawl target down as the nose swings off the aim (up to the
    // turn cone, where the drive loop would have handed off to a 3PT) so the
    // car curves onto the lane rather than overshooting it — but never below a
    // floor, or a car pointing well off-aim would brake to a stop and never
    // steer around (the Vehicle brake kills the very motion the steer needs).
    const float cone = std::max(1.f, t.driveback_turn_cone_deg);
    const float align = std::clamp(1.f - std::abs(v.bearing_err_deg) / cone,
                                   0.f, 1.f);
    const float v_target = t.crawl_speed * (0.4f + 0.6f * align);
    cmd = maneuver_speed_cmd(v.v_signed, v_target, t);
    // Proportional steer toward the aim; full lock by driveback_steer_full_deg.
    const float full = std::max(1.f, t.driveback_steer_full_deg);
    cmd.steer = std::clamp(-v.bearing_err_deg / full, -1.f, 1.f);
    return cmd;
}

bool maneuver_path_clear(const ManeuverClearance& c, float need_m,
                         const ManeuverTuning& t) {
    return c.car_gap >= need_m + t.car_clear_margin
        && c.ped_gap >= need_m + t.ped_clear_margin;
}

float maneuver_corridor_gap(glm::vec2 start, glm::vec2 dir, float half_width,
                            float max_len, glm::vec2 p, float radius) {
    const glm::vec2 rel = p - start;
    const float along = rel.x * dir.x + rel.y * dir.y;          // dot
    const float lateral = std::abs(rel.x * dir.y - rel.y * dir.x); // |cross|
    // Conservative rectangle-vs-disc: outside the widened band, or entirely
    // behind the start / beyond the probe -> never intrudes.
    if (lateral > half_width + radius) {
        return std::numeric_limits<float>::infinity();
    }
    if (along + radius <= 0.f || along - radius >= max_len) {
        return std::numeric_limits<float>::infinity();
    }
    return std::max(0.f, along - radius);
}

int telemetry_dist_bucket(float dist, float bucket_m, int buckets) {
    // Negative == no player reference for this sample; file it with the far /
    // beyond-range samples in the last bucket rather than faking a nearby one.
    if (dist < 0.f) return buckets - 1;
    const int b = static_cast<int>(dist / std::max(bucket_m, 1e-3f));
    return std::min(b, buckets - 1);
}

void RecoveryTelemetry::record_jam(float dist_from_player) {
    ++jam_count;
    ++jam_hist[telemetry_dist_bucket(dist_from_player, BUCKET_M, BUCKETS)];
}

void RecoveryTelemetry::record_give_up(float dist_from_player) {
    ++give_up_count;
    ++give_up_hist[telemetry_dist_bucket(dist_from_player, BUCKET_M, BUCKETS)];
}

bool overtake_gap_acceptable(const OvertakeLaneView& v, float pass_length,
                             const OvertakeTuning& t) {
    // Front: need hard clearance for the whole pass, and — if a car is closing
    // head-on into the gap — a comfortable time before it arrives (TTC > safe_ttc,
    // i.e. front_gap >= safe_ttc * closing). An empty lane has front_gap == +inf
    // and front_closing == 0, so both checks pass: the single-close-blocker case.
    const float need_front = std::max(t.min_front_gap, pass_length);
    if (v.front_gap < need_front) return false;
    if (v.front_closing > 0.f && v.front_gap < t.safe_ttc * v.front_closing)
        return false;
    // Rear: don't pull in front of a car already in the target lane that would
    // then have to brake hard for us (the MOBIL safety criterion on the new
    // follower). Same hard-distance + TTC pair, mirrored.
    if (v.rear_gap < t.min_rear_gap) return false;
    if (v.rear_closing > 0.f && v.rear_gap < t.safe_ttc * v.rear_closing)
        return false;
    return true;
}

float go_around_max_lateral_rate(float speed, float max_steer_rad) {
    return std::max(speed, 0.f) * std::tan(max_steer_rad);
}

float go_around_shift_offset(float from, float target, float phase) {
    phase = std::clamp(phase, 0.f, 1.f);
    const float s = phase * phase * (3.f - 2.f * phase);   // smoothstep ease in/out
    return from + (target - from) * s;
}

float go_around_advance_offset(float offset, float& phase, float from, float target,
                               float speed, float dt, float max_steer_rad,
                               float shift_seconds, float min_shift_speed) {
    // Nothing to do: a zero-magnitude shift is already complete.
    if (std::abs(target - from) < 1e-4f) {
        phase = 1.f;
        return target;
    }
    // No-slide gate: a (near-)stopped car can't displace sideways — it has to
    // accelerate first. Until it's rolling at min_shift_speed the shift is frozen
    // (phase unchanged) and the body stays put.
    if (speed < min_shift_speed) return offset;
    // Progress the shift over forward travel: phase advances dt/shift_seconds while
    // rolling (so a full shift sweeps in ~shift_seconds; a faster car covers more
    // ground per unit phase, tracing a longer, gentler arc).
    const float st = std::max(shift_seconds, 1e-3f);
    phase = std::min(1.f, phase + dt / st);
    const float desired = go_around_shift_offset(from, target, phase);
    // Steering-lock cap: the body can crab no faster than speed*tan(lock), so the
    // per-frame lateral step — and thus the heading deviation — stays within the
    // lock without any artificial clamp.
    const float max_step = go_around_max_lateral_rate(speed, max_steer_rad) * dt;
    const float step = std::clamp(desired - offset, -max_step, max_step);
    return offset + step;
}

glm::vec2 go_around_motion_dir(glm::vec2 fwd, glm::vec2 right, float speed,
                               float lateral_rate) {
    const float fl = glm::length(fwd);
    const glm::vec2 f = (fl > 1e-5f) ? fwd / fl : glm::vec2{0.f, 0.f};
    const glm::vec2 motion = f * std::max(speed, 0.f) + right * lateral_rate;
    const float ml = glm::length(motion);
    if (ml < 1e-4f) return f;   // degenerate (stopped, no lateral) -> face forward
    return motion / ml;
}

PlayerHazard assess_player_hazard(glm::vec2 ai_pos, glm::vec2 ai_fwd, float ai_speed,
                                  glm::vec2 player_pos, glm::vec2 player_vel,
                                  const PlayerHazardTuning& t, float car_length,
                                  bool consider_path_leader) {
    PlayerHazard h;                              // inactive, gap == +inf
    constexpr float kInf = std::numeric_limits<float>::infinity();

    const float fl = glm::length(ai_fwd);
    if (fl < 1e-4f) return h;                    // degenerate heading
    const glm::vec2 fwd = ai_fwd / fl;
    ai_speed = std::max(ai_speed, 0.f);

    const glm::vec2 rel  = player_pos - ai_pos;  // AI -> player
    if (glm::dot(rel, rel) > t.range * t.range) return h;   // proximity gate

    const float fwd_dist = glm::dot(fwd, rel);                    // along heading
    const float lat      = std::abs(fwd.x * rel.y - fwd.y * rel.x); // perpendicular

    float best          = kInf;
    float best_leader_v = 0.f;

    // (A) Player ahead, inside the widened path corridor: a static or moving
    //     leader. Allow a little behind the bumper (-car_length/2) so a player
    //     pressed up alongside-front still registers. PCG-156: the caller
    //     suppresses this term (consider_path_leader == false) while TURNING and
    //     the player is off the destination lane, so the swept heading can't rake
    //     this cone across an off-path player and false-brake mid-turn. Detector
    //     (B) below is intentionally NOT gated — a real collision course still
    //     brakes.
    if (consider_path_leader
        && fwd_dist > -car_length * 0.5f && fwd_dist < t.range && lat < t.half_width) {
        const float gap = std::max(0.f, fwd_dist - car_length);
        if (gap < best) {
            best          = gap;
            best_leader_v = glm::dot(player_vel, fwd);   // +ve == player pulling away
        }
    }

    // (B) Closing collision course: predict the closest approach of the two
    //     points and, if it breaches the collision radius within the horizon,
    //     brake for the predicted conflict as a stationary virtual leader at the
    //     distance the AI travels before it (ai_speed * ttc). This catches a
    //     crosser / head-on player OFF the corridor that (A) misses.
    const glm::vec2 rel_vel = player_vel - fwd * ai_speed;        // player rel. AI
    const float     rv2     = glm::dot(rel_vel, rel_vel);
    if (rv2 > 1e-4f) {
        const float ttc = -glm::dot(rel, rel_vel) / rv2;         // closest-approach time
        if (ttc > 0.f && ttc < t.ttc_horizon) {
            const glm::vec2 closest = rel + rel_vel * ttc;
            if (glm::dot(closest, closest) < t.collision_r * t.collision_r) {
                const float gap = std::max(0.f, ai_speed * ttc - car_length);
                if (gap < best) {
                    best          = gap;
                    best_leader_v = 0.f;                          // brake toward the point
                }
            }
        }
    }

    if (std::isfinite(best)) {
        h.active       = true;
        h.gap          = best;
        h.leader_speed = best_leader_v;
        h.honk         = best < t.honk_gap;
    }
    return h;
}

bool honk_should_fire(bool honking_now, double now_seconds,
                      double min_interval, HonkDebounceState& state) {
    const bool rising = honking_now && !state.was_honking;
    state.was_honking = honking_now;
    if (!rising) return false;                              // not a fresh honk
    if (now_seconds - state.last_honk_time < min_interval)  // too soon since last
        return false;
    state.last_honk_time = now_seconds;
    return true;
}

bool panic_should_trigger(const PlayerHazard& hazard, float closing_speed,
                          const PanicTuning& t) {
    // BOTH gates: the player must already be close (binding hazard gap) AND be
    // bearing down fast (mutual closing). The high closing floor is the
    // selectivity term — a calm approach to a stopped player has bled its speed
    // by the time the gap is short, so closing falls under the floor and only a
    // genuinely abrupt near-miss / clip trips the panic.
    return hazard.active
        && hazard.gap <= t.trigger_gap
        && closing_speed >= t.trigger_closing;
}

PanicPhase panic_tick(float& timer, bool triggered, float dt,
                      const PanicTuning& t) {
    const float dur = std::max(t.duration, 1e-3f);
    if (triggered) {
        if (timer <= 0.f)
            timer = dur;                                  // fresh panic: opens in startle
        else
            timer = std::max(timer, dur * (1.f - t.startle_frac));
            // ^ sustained menace: top the timer back up into the FLEE band only,
            //   so the car keeps fleeing instead of re-jolting into startle.
    }
    // Always decay, always clamp at 0 — the "never stuck" guarantee.
    timer = std::max(0.f, timer - std::max(dt, 0.f));
    if (timer <= 0.f) {
        timer = 0.f;
        return PanicPhase::None;
    }
    const float elapsed = dur - timer;                    // time since the panic started
    return (elapsed < t.startle_frac * dur) ? PanicPhase::Startle
                                            : PanicPhase::Flee;
}

EmergencyYield emergency_yield_classify(glm::vec2 ego_pos, glm::vec2 ego_fwd,
                                        bool ego_in_junction, bool ego_queued,
                                        const EmergencyResponderView& r,
                                        const EmergencyYieldTuning& t) {
    const glm::vec2 rel = r.pos - ego_pos;
    if (glm::dot(rel, rel) > t.detect_range * t.detect_range)
        return EmergencyYield::None;

    // A responder that isn't actually rolling isn't approaching anyone — a
    // dwelling on-scene ambulance / a wedged pursuer doesn't freeze the street
    // (normal leader/avoidance behaviour handles a stopped vehicle).
    const float rspeed = glm::length(r.vel);
    if (rspeed < t.min_responder_speed) return EmergencyYield::None;

    const float flen = glm::length(ego_fwd);
    if (flen < 1e-5f) return EmergencyYield::None;
    const glm::vec2 fwd  = ego_fwd / flen;
    const glm::vec2 rdir = r.vel / rspeed;

    // Ego frame: signed distance ahead + lateral off the heading line. The
    // lateral gate keeps a responder on a PARALLEL street from parting traffic
    // a block over.
    const float along = glm::dot(rel, fwd);
    const float lat   = std::abs(rel.x * fwd.y - rel.y * fwd.x);
    if (lat > t.lateral_gate) return EmergencyYield::None;

    const float dir_dot = glm::dot(fwd, rdir);
    EmergencyYield out = EmergencyYield::None;
    if (dir_dot >= t.parallel_dot) {
        // Same direction: it closes from behind and we clear the road ahead of
        // it. Once it is resume_behind_m PAST us (ahead of us) the encounter is
        // over — which also covers the ego BEHIND the responder from the start
        // (chasing lights that are driving away is not a yield).
        if (along <= t.resume_behind_m) out = EmergencyYield::PullOverRight;
    } else if (dir_dot <= -t.parallel_dot) {
        // Oncoming: matters while it is still ahead of us (closing head-on),
        // plus the same past-us hysteresis margin.
        if (along >= -t.resume_behind_m) out = EmergencyYield::SlowEdgeRight;
    }
    // else: crossing traffic — junction negotiation owns that; no yield.

    if (out == EmergencyYield::None) return EmergencyYield::None;
    if (ego_in_junction) return EmergencyYield::None;  // finish the box first
    if (ego_queued)      return EmergencyYield::HoldQueued;
    return out;
}

EmergencyYield emergency_yield_tick(EmergencyYield& latched, float& resume_s,
                                    EmergencyYield now, float stagger_s,
                                    float dt) {
    if (now != EmergencyYield::None) {
        latched  = now;
        resume_s = stagger_s;   // re-armed every live frame; counts down after
        return now;
    }
    if (latched == EmergencyYield::None) return EmergencyYield::None;
    resume_s -= std::max(dt, 0.f);
    if (resume_s <= 0.f) {
        latched  = EmergencyYield::None;
        resume_s = 0.f;
    }
    return latched;
}

float emergency_yield_stagger(glm::vec2 pos, const EmergencyYieldTuning& t) {
    // Shader-style position hash -> [0,1). Deterministic for a given spot, and
    // neighbouring cars (metres apart) land on well-spread values.
    const float s = std::sin(pos.x * 12.9898f + pos.y * 78.233f) * 43758.5453f;
    const float u = s - std::floor(s);
    return t.resume_stagger_min_s
         + u * std::max(0.f, t.resume_stagger_max_s - t.resume_stagger_min_s);
}

bool operator==(const LaneId& a, const LaneId& b) {
    return a.i == b.i && a.j == b.j && a.dir == b.dir;
}

bool operator!=(const LaneId& a, const LaneId& b) {
    return !(a == b);
}

// Turn classification + branch weights. Promoted from the (removed) grid-only
// TrafficLaneGraph to free functions; LaneGraph's grid producer is the consumer.
TrafficTurnKind turn_kind(GridDir from, GridDir to) {
    const int f = static_cast<int>(from);
    const int t = static_cast<int>(to);
    const int delta = (t - f + 4) & 3;
    if (delta == 0) return TrafficTurnKind::Straight;
    if (delta == 1) return TrafficTurnKind::Left;
    if (delta == 3) return TrafficTurnKind::Right;
    return TrafficTurnKind::UTurn;
}

float turn_weight(TrafficTurnKind kind) {
    switch (kind) {
        case TrafficTurnKind::Straight: return 56.f;
        case TrafficTurnKind::Right:    return 25.f;
        case TrafficTurnKind::Left:     return 18.f;
        case TrafficTurnKind::UTurn:    return 1.f;
    }
    return 1.f;
}

} // namespace apricot
