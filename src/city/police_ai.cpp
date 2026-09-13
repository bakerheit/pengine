#include "city/police_ai.h"

#include <algorithm>
#include <cmath>
#include <limits>


namespace apricot {

bool police_can_witness(glm::vec2 cop_pos, glm::vec2 cop_fwd,
                        glm::vec2 offender_pos, bool crime_active,
                        bool los_clear, const PoliceTuning& t) {
    if (!crime_active || !los_clear) return false;

    glm::vec2 to = offender_pos - cop_pos;
    float d2 = glm::dot(to, to);
    if (d2 < 1e-6f) return false;                       // coincident -> fail closed
    if (d2 > t.witness_range * t.witness_range) return false;

    float fwd2 = glm::dot(cop_fwd, cop_fwd);
    if (fwd2 < 1e-12f) return false;                    // no facing -> fail closed

    // cos of the angle between the cop's heading and the line to the offender.
    float cos_ang = glm::dot(cop_fwd, to) / std::sqrt(fwd2 * d2);
    return cos_ang >= t.witness_fov_cos;
}

bool police_maintains_contact(glm::vec2 cop_pos, glm::vec2 offender_pos,
                              bool los_clear, const PoliceTuning& t) {
    if (!los_clear) return false;                       // occluded -> contact lost

    glm::vec2 to = offender_pos - cop_pos;
    float d2 = glm::dot(to, to);
    // NOTE: no coincident guard. An engaged unit on top of the player (the ram
    // case) is the strongest contact there is; there is no cone division to fail
    // closed on, so d2 == 0 is in-contact.
    return d2 <= t.pursuit_contact_range * t.pursuit_contact_range;
}

float police_control_override_speed(bool engaged, float slack_m,
                                    const PoliceTuning& t) {
    // Not engaged -> no override. Infinity is the identity for the caller's
    // std::min, so an ambient patrol keeps the civilian path byte for byte.
    if (!engaged) return std::numeric_limits<float>::infinity();
    const float cap = std::max(0.0f, t.control_override_speed);
    if (!std::isfinite(slack_m) || slack_m <= 0.0f) return cap;
    // Quick on the run-up, slow at the line. A flat cap would have a pursuer
    // crawl the last hundred metres of a green approach it never had to slow
    // for, which reads as a cop losing interest.
    return cap + slack_m * std::max(0.0f, t.control_override_slack);
}

bool police_should_ram(bool engaged, bool target_on_foot, int wanted_level,
                       float ahead_m, float range_m, float speed_mps,
                       const PoliceTuning& t) {
    if (!engaged || target_on_foot) return false;
    if (!police_level_profile(wanted_level).may_ram) return false;
    if (!std::isfinite(ahead_m) || !std::isfinite(range_m) ||
        !std::isfinite(speed_mps)) return false;
    // Behind the cruiser, or too far to reach before the road turns: the
    // ordinary route-follow closes that gap first.
    if (ahead_m < t.ram_min_ahead || range_m > t.ram_range) return false;
    // A stationary cruiser nudging a car is not a ram, it is a parking scrape.
    return speed_mps >= 6.0f;
}

float police_contact_speed_mps(float suspect_speed_along_mps, float gap_m,
                               const PoliceTuning& t) {
    const float closing = std::max(0.0f, t.contact_closing_mps);
    const float suspect = std::isfinite(suspect_speed_along_mps)
        ? std::max(0.0f, suspect_speed_along_mps) : 0.0f;
    const float gap = std::isfinite(gap_m) ? std::max(0.0f, gap_m) : 0.0f;
    return suspect + std::sqrt(closing * closing +
        2.0f * std::max(0.0f, t.contact_brake_mps2) * gap);
}

PursuitCmd police_limit_contact_closing(PursuitCmd cmd, float ahead,
                                        float forward_speed_mps, float limit_mps) {
    // Behind the car is a turn-around, not a ram; leave it to the command.
    if (!(ahead > 0.0f) || !std::isfinite(forward_speed_mps) ||
        !std::isfinite(limit_mps)) return cmd;
    const float surplus = forward_speed_mps - limit_mps;
    if (surplus <= 0.0f) return cmd;
    cmd.throttle = 0.0f;
    cmd.brake = std::max(cmd.brake, std::clamp(surplus, 0.2f, 1.0f));
    return cmd;
}

int police_spawn_fallback_count(bool crime_active, int target_units,
                                int engaged_units) {
    if (!crime_active) return 0;
    int deficit = target_units - engaged_units;
    return deficit > 0 ? deficit : 0;
}

PursuitCmd police_terminal_pursuit_cmd(float dist, float ahead, float side,
                                       float speed, bool forward_blocked) {
    PursuitCmd cmd;

    // Target is on top of us: stand on the brake (avoid steer/throttle jitter at
    // a zero-length direction vector).
    if (dist < 1e-3f) {
        cmd.brake = 1.f;
        return cmd;
    }

    cmd.steer    = std::clamp(side * 2.2f, -1.f, 1.f);
    cmd.throttle = 1.f;
    cmd.brake    = 0.f;

    // If the target is behind us, brake into the turn first; once nearly
    // stopped, TURN AROUND.
    if (ahead < -0.25f) {
        if (speed > 5.f) {
            cmd.throttle = 0.f;
            cmd.brake    = 0.75f;
        } else if (!forward_blocked) {
            // Room to swing it round: a car turns around forwards, at full
            // lock, toward the side the target is on. One arc, nose leading.
            cmd.throttle = 1.f;
            cmd.brake    = 0.f;
            cmd.steer    = side >= 0.f ? 1.f : -1.f;
        } else {
            // Boxed in — back up. THE STEER INVERTS HERE, and that is the
            // whole point: the front wheels still steer in reverse, so the
            // nose goes opposite them. Steering toward the target while
            // reversing swings the nose further away and the car simply never
            // comes round.
            cmd.throttle = -0.65f;
            cmd.brake    = 0.f;
            cmd.steer    = side >= 0.f ? -1.f : 1.f;
        }
    }

    // GTA-style ram: when we're lined up on the player, drive INTO them at full
    // throttle rather than easing off. Only back the throttle down a touch at
    // point-blank range so a cruiser already pressed against the player keeps
    // shoving without grinding its engine.
    if (dist < 8.f && ahead > 0.3f) {
        cmd.throttle = (dist < 2.5f) ? 0.6f : 1.0f;
        cmd.brake    = 0.f;
    }

    cmd.handbrake = std::abs(side) > 0.75f && ahead > 0.1f && speed > 14.f;
    return cmd;
}

bool police_should_replan(float since_last_replan, float replan_interval,
                          glm::vec2 target_now, glm::vec2 target_at_last_plan,
                          float move_thresh, bool route_invalid) {
    if (route_invalid) return true;                       // no usable route -> plan
    if (since_last_replan >= replan_interval) return true;  // cadence elapsed
    glm::vec2 d = target_now - target_at_last_plan;
    return glm::dot(d, d) > move_thresh * move_thresh;    // target moved enough
}

bool police_use_terminal(float dist_to_target, float handoff_range) {
    return dist_to_target <= handoff_range;
}

glm::vec2 police_pursuit_intercept(glm::vec2 target, glm::vec2 velocity) {
    if (!std::isfinite(target.x) || !std::isfinite(target.y) ||
        !std::isfinite(velocity.x) || !std::isfinite(velocity.y)) return target;
    const float speed = glm::length(velocity);
    if (speed < 0.15f) return target;
    const float lead_seconds = std::clamp(0.75f + speed * 0.04f, 0.75f, 2.2f);
    const glm::vec2 lead = velocity * lead_seconds;
    const float distance = glm::length(lead);
    return target + (distance > 45.0f ? lead * (45.0f / distance) : lead);
}

float police_pursuit_cruise_mps(float road_limit_mps,
                                float target_speed_mps,
                                int wanted_level) {
    if (!std::isfinite(road_limit_mps) || road_limit_mps < 0.0f)
        road_limit_mps = 0.0f;
    if (!std::isfinite(target_speed_mps) || target_speed_mps < 0.0f)
        target_speed_mps = 0.0f;
    // The margin has to survive the junctions the cruiser still has to cross.
    // Measured on the authored city at a 40 mph flee, the nearest pursuer runs
    // free for under half the chase and spends the rest crossing, cornering or
    // held — so a flat "+3 m/s over the suspect" is a NET LOSS of about four
    // metres a second, and the cop drifts backwards out of the chase however
    // well it drives. The margin below is what makes holding station possible;
    // the caps everywhere else are what stop it being a straight-line win.
    const float catchup = target_speed_mps * 1.30f + 6.0f +
        police_level_profile(wanted_level).cruise_bonus_mps;
    return std::clamp(std::max(road_limit_mps * 1.25f, catchup),
                      0.0f, 38.0f);
}

float police_turn_speed_mps(float ambient_cap, bool engaged,
                            const PoliceTuning& t) {
    if (!engaged || !std::isfinite(ambient_cap) || ambient_cap <= 0.0f)
        return ambient_cap;
    return ambient_cap * std::max(1.0f, t.turn_speed_scale);
}

HeatDecay wanted_heat_decay_step(float heat, float lose_track_timer,
                                 bool in_police_view, float dt,
                                 float lose_track_window_s,
                                 const PoliceTuning& t) {
    HeatDecay r;
    // No heat -> stay cleared, timer idle (so a re-offence starts its grace fresh).
    if (heat <= 0.f) {
        r.heat = 0.f;
        r.lose_track_timer = 0.f;
        return r;
    }
    // Seen by ANY cop -> the level holds; refresh "last seen".
    if (in_police_view) {
        r.heat = heat;
        r.lose_track_timer = 0.f;
        return r;
    }
    // Out of all police LOS: run the lose-track grace, then cool down.
    r.heat = heat;
    r.lose_track_timer = lose_track_timer + dt;
    if (r.lose_track_timer >= lose_track_window_s)
        r.heat = std::max(0.f, heat - dt * t.heat_decay_rate);
    return r;
}

bool police_should_despawn_standdown(float dist2_to_target, bool chase_over,
                                     float stand_down_distance,
                                     float far_despawn_distance) {
    if (dist2_to_target > far_despawn_distance * far_despawn_distance)
        return true;                                  // past the hard ring: gone
    if (chase_over &&
        dist2_to_target > stand_down_distance * stand_down_distance)
        return true;                                  // stood down & peeled off
    return false;                                     // near the player: keep it
}

ResponseGateStep police_response_gate_step(ResponseGate g, int true_level,
                                           bool escalated_from_zero,
                                           bool dispatch_played,
                                           float armed_delay, float dt) {
    // A fresh 0-star escalation latches the hold and clears any stale countdown
    // (from a prior crime the player escaped before units engaged), so the reset
    // is clean and the new crime's radio arms a fresh delay.
    if (escalated_from_zero) {
        g.pending = true;
        g.delay   = 0.f;
    }
    // The dispatch radio going out starts the response clock — once, while waiting.
    // (delay <= 0 with pending set is the "waiting-for-radio" state.)
    if (dispatch_played && g.pending && g.delay <= 0.f)
        g.delay = armed_delay;
    // Count the held response down; when it lapses, units go live.
    if (g.pending && g.delay > 0.f) {
        g.delay -= dt;
        if (g.delay <= 0.f) {
            g.delay   = 0.f;
            g.pending = false;
        }
    }
    ResponseGateStep out;
    out.gate             = g;
    out.responding_level = g.pending ? 0 : true_level;
    return out;
}

// =============================================================================
// Police roadblocks.
//
// NOT LIFTED: roadblock_select_site(). It walks a LaneGraph — loaded lanes,
// junction arity, arc-length projection — and that class is not in this tree.
// It is the one piece of the roadblock tactic that needs the road network
// rather than plain geometry, so it lands with the lane graph or not at all.
// Everything downstream of a chosen site is here and is pure.
// =============================================================================

RoadblockLayout roadblock_layout(glm::vec2 center, glm::vec2 fwd, float width,
                                 float car_half_len,
                                 const RoadblockTuning& t) {
    RoadblockLayout out;
    float fl = glm::length(fwd);
    if (fl < 1e-4f || width <= 0.f) return out;
    fwd /= fl;
    const glm::vec2 right{-fwd.y, fwd.x};   // XZ right-of-travel

    const int n = width >= t.wide_width_m ? 3 : 2;
    out.n_cars = n;
    const float ang = glm::radians(t.car_angle_deg);
    for (int k = 0; k < n; ++k) {
        // Even lateral spread across the FULL carriageway; alternating +/-45
        // deg yaw and fore/aft stagger interlock the angled boxes (chevron)
        // without overlap — pinned by the layout unit test.
        const float lat = -width * 0.5f +
                          width * (static_cast<float>(k) + 0.5f) /
                              static_cast<float>(n);
        const float sgn = (k % 2 == 0) ? 1.f : -1.f;
        const float c = std::cos(sgn * ang), s = std::sin(sgn * ang);
        auto& car  = out.cars[static_cast<std::size_t>(k)];
        car.pos    = center + right * lat + fwd * (sgn * 0.5f * t.car_stagger_m);
        car.facing = {fwd.x * c - fwd.y * s, fwd.x * s + fwd.y * c};
    }

    // Officers stand DOWNSTREAM of the car line (cover behind the block, away
    // from the player's approach): one for a 2-car block, two for a 3-car.
    const int n_off = std::min(n - 1, 2);
    out.n_officers = n_off;
    const float back = 0.5f * t.car_stagger_m + car_half_len + t.officer_back_m;
    for (int k = 0; k < n_off; ++k) {
        const float lat = (n_off == 1) ? 0.f
                                       : (k == 0 ? -width * 0.25f
                                                 :  width * 0.25f);
        out.officers[static_cast<std::size_t>(k)] =
            center + fwd * back + right * lat;
    }
    return out;
}

bool roadblock_breached(glm::vec2 player_xz, glm::vec2 center, glm::vec2 fwd,
                        float width, const RoadblockTuning& t) {
    float fl = glm::length(fwd);
    if (fl < 1e-4f) return false;
    fwd /= fl;
    const glm::vec2 right{-fwd.y, fwd.x};
    const glm::vec2 d = player_xz - center;
    if (glm::dot(d, fwd) < t.breach_pass_m) return false;   // not yet past
    return std::abs(glm::dot(d, right)) <=
           width * 0.5f + t.breach_side_slack_m;            // on/near the road
}

RoadblockDispatchStep roadblock_dispatch_step(bool active, float cooldown,
                                              int wanted, int police_count,
                                              int max_police, float dt,
                                              const RoadblockTuning& t) {
    RoadblockDispatchStep out;
    out.cooldown = std::max(0.f, cooldown - dt);
    if (active) return out;                        // max 1 roadblock at a time
    if (wanted < t.min_wanted) return out;
    if (out.cooldown > 0.f) return out;
    if (police_count + 2 > max_police) return out; // min block busts the budget
    out.try_stage = true;
    return out;
}

bool wanted_report_blink_step(bool pending, bool escalated_from_zero,
                              bool dispatch_armed, bool dispatch_played,
                              int wanted_level) {
    // Arm on the fresh 0-star escalation ONLY when a dispatch is genuinely queued
    // this frame, so the guaranteed radio event clears it later (no stuck-blinking).
    if (escalated_from_zero && dispatch_armed)
        pending = true;
    // The radio airing snaps the stars solid; heat clearing clears the blink too.
    // These win over a same-frame arm — the safe default is never-stuck.
    if (dispatch_played || wanted_level <= 0)
        pending = false;
    return pending;
}

} // namespace apricot
