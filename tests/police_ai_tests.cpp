// Police decision kernels, headless.
//
// The witness and contact gates, the terminal pursuit command, the LOS-gated
// wanted decay, the graceful stand-down, the response-delay gate, the ram
// attribution rule, and the parts of the roadblock tactic that work from a
// chosen site.
//
// NOT HERE: roadblock site SELECTION and the lane-route planner tests. Both
// walk a lane graph, and that class did not come across with PENG-29. The
// site-selection kernel was left behind with them, so this suite covers
// everything that survived and nothing that did not.

#include <algorithm>
#include <cmath>
#include <vector>

#include <glm/glm.hpp>

#include "test_assert.h"

#include "city/police_ai.h"

using namespace apricot;

namespace {

// =============================================================================
// PCG-011 — police realism slice: witness gate, spawn-at-distance fallback, and
// the terminal-pursuit command (characterization net before the epic's deeper
// path-planner child refactors update_police_dynamic).
// =============================================================================

void test_police_witness_gate() {
    PoliceTuning t;                       // defaults: range 45 m, fov_cos 0.30
    const glm::vec2 cop{0.f, 0.f};
    const glm::vec2 fwd{1.f, 0.f};        // facing +x

    // Crime active, dead ahead, in range, clear LOS -> witness.
    REQUIRE(police_can_witness(cop, fwd, {20.f, 0.f}, true, true, t));
    // No active crime -> never a witness, even staring right at it.
    REQUIRE(!police_can_witness(cop, fwd, {20.f, 0.f}, false, true, t));
    // LOS blocked -> no witness.
    REQUIRE(!police_can_witness(cop, fwd, {20.f, 0.f}, true, false, t));
    // Out of range (beyond 45 m) -> no witness.
    REQUIRE(!police_can_witness(cop, fwd, {60.f, 0.f}, true, true, t));
    // Directly behind -> outside the forward view cone.
    REQUIRE(!police_can_witness(cop, fwd, {-20.f, 0.f}, true, true, t));

    // Cone edge at fixed range: fov_cos 0.30 ~= 72.5 deg half-angle, so 60 deg
    // off-axis is inside and 80 deg is outside.
    auto at_angle = [&](float deg) {
        float r = glm::radians(deg);
        return glm::vec2{std::cos(r) * 20.f, std::sin(r) * 20.f};
    };
    REQUIRE(police_can_witness(cop, fwd, at_angle(60.f), true, true, t));
    REQUIRE(!police_can_witness(cop, fwd, at_angle(80.f), true, true, t));

    // Degenerate inputs fail closed: coincident offender, and a zero facing.
    REQUIRE(!police_can_witness(cop, fwd, cop, true, true, t));
    REQUIRE(!police_can_witness(cop, {0.f, 0.f}, {20.f, 0.f}, true, true, t));
}

// PCG-030 (2nd increment) — pursuit CONTACT gate. Sibling to the witness gate but
// cone-LESS: an already-engaged unit holds the player's heat on range + LOS alone,
// regardless of facing. Pins the exact founder feel-check bug (a pursuer reversing
// to ram faces AWAY yet stays in contact), the LOS-break drop, the range cull, and
// the contrast that the WITNESS cone is unchanged for ambient patrols.
void test_police_maintains_contact_gate() {
    PoliceTuning t;                       // pursuit_contact_range 70 m (cone-less)
    const glm::vec2 cop{0.f, 0.f};

    // THE RAM GEOMETRY: an engaged pursuer reversing into the player faces dead
    // away (the player is BEHIND the cop's nose, forward·to-player < 0), point-
    // blank, clear LOS -> contact HOLDS. This is the case that used to let heat
    // decay to 0 mid-ram because the witness cone rejected it.
    REQUIRE(police_maintains_contact(cop, {-3.f, 0.f}, /*los_clear=*/true, t));

    // Point-blank, cop on top of the player -> still holds. No coincident guard:
    // a cruiser pressed to your bumper is the tightest contact, not a degenerate
    // (no cone division to fail closed on, unlike police_can_witness).
    REQUIRE(police_maintains_contact(cop, cop, true, t));

    // Genuine LOS break (rounded a corner / behind a building), still in range ->
    // contact LOST. This is the legit-escape path: it lets the lose-track timer
    // start so heat can finally cool.
    REQUIRE(!police_maintains_contact(cop, {-3.f, 0.f}, /*los_clear=*/false, t));

    // Beyond the contact range (player outran the cop) -> lost, even clear LOS.
    REQUIRE(!police_maintains_contact(cop, {t.pursuit_contact_range + 1.f, 0.f},
                                      true, t));
    // Just inside the contact range, clear LOS -> holds.
    REQUIRE(police_maintains_contact(cop, {t.pursuit_contact_range - 1.f, 0.f},
                                     true, t));
    // Exactly AT the range boundary is in contact (inclusive `<=`).
    REQUIRE(police_maintains_contact(cop, {t.pursuit_contact_range, 0.f}, true, t));

    // The contact range is deliberately looser than the witness range, so the
    // model split actually WIDENS the heat-hold for an engaged unit.
    REQUIRE(t.pursuit_contact_range > t.witness_range);

    // CONTRAST (the witness path is unchanged): the SAME behind-the-cop ram
    // geometry is NOT a witness for an ambient patrol — the forward cone still
    // rejects a crime behind the cop, so conversion-to-pursuer is not regressed.
    const glm::vec2 fwd{1.f, 0.f};        // facing +x; player at -x is behind
    REQUIRE(!police_can_witness(cop, fwd, {-3.f, 0.f}, true, true, t));
}

void test_police_spawn_fallback() {
    // No crime -> never spawn, whatever the counts.
    REQUIRE(police_spawn_fallback_count(false, 5, 0) == 0);
    // Crime, nobody engaged -> draw toward the whole target.
    REQUIRE(police_spawn_fallback_count(true, 3, 0) == 3);
    // Converted patrols count as engaged -> they shrink the fallback.
    REQUIRE(police_spawn_fallback_count(true, 3, 1) == 2);
    REQUIRE(police_spawn_fallback_count(true, 3, 3) == 0);
    // Never negative (e.g. the wanted level just dropped below the engaged count).
    REQUIRE(police_spawn_fallback_count(true, 3, 5) == 0);
}

void test_police_terminal_pursuit_characterization() {
    // Target on top of us -> brake hard, no steer / throttle / handbrake.
    {
        PursuitCmd c = police_terminal_pursuit_cmd(0.f, 1.f, 0.f, 5.f);
        REQUIRE(c.brake == 1.f);
        REQUIRE(c.throttle == 0.f);
        REQUIRE(!c.handbrake);
    }
    // Lined up, mid-range -> full-throttle chase, no brake, no steer.
    {
        PursuitCmd c = police_terminal_pursuit_cmd(30.f, 1.f, 0.f, 10.f);
        REQUIRE(c.throttle == 1.f);
        REQUIRE(c.brake == 0.f);
        REQUIRE(c.steer == 0.f);
    }
    // Side offset -> steer saturates to +/-1 (gain 2.2, clamped).
    {
        REQUIRE(police_terminal_pursuit_cmd(30.f, 0.8f, 1.f, 10.f).steer == 1.f);
        REQUIRE(police_terminal_pursuit_cmd(30.f, 0.8f, -1.f, 10.f).steer == -1.f);
    }
    // Target behind, still fast -> brake into the turn (no throttle).
    {
        PursuitCmd c = police_terminal_pursuit_cmd(30.f, -0.5f, 0.3f, 10.f);
        REQUIRE(c.throttle == 0.f);
        REQUIRE(c.brake == 0.75f);
    }
    // Target behind, nearly stopped -> reverse to recover from the missed pass.
    {
        PursuitCmd c = police_terminal_pursuit_cmd(30.f, -0.5f, 0.3f, 2.f);
        REQUIRE(c.throttle == -0.65f);
        REQUIRE(c.brake == 0.f);
    }
    // Ram window: lined up & close (<8 m) -> full throttle; point-blank (<2.5 m)
    // eases to 0.6 so a pressed cruiser keeps shoving without grinding.
    {
        REQUIRE(police_terminal_pursuit_cmd(5.f, 0.5f, 0.f, 6.f).throttle == 1.0f);
        REQUIRE(police_terminal_pursuit_cmd(2.f, 0.5f, 0.f, 6.f).throttle == 0.6f);
    }
    // Handbrake gate: |side|>0.75 AND ahead>0.1 AND speed>14.
    {
        REQUIRE(police_terminal_pursuit_cmd(30.f, 0.5f, 0.8f, 20.f).handbrake);
        REQUIRE(!police_terminal_pursuit_cmd(30.f, 0.5f, 0.8f, 10.f).handbrake);
    }
}

// =============================================================================
// PCG-030 — wanted heat de-escalation: LOS-gated decay + graceful police
// stand-down. Both decisions are pure helpers (wanted_heat_decay_step,
// police_should_despawn_standdown) so the hold/grace/decay branch and the
// no-in-view-pop despawn gate are pinned headless.
// =============================================================================

void test_wanted_heat_decay_los_gated() {
    PoliceTuning t;   // lose_track_window 8 s, heat_decay_rate 0.22/s

    // In ANY cop's view -> heat HOLDS and the lose-track timer refreshes to 0,
    // however long it had been running (Bug 1: no pure-timer decay while seen).
    {
        HeatDecay d = wanted_heat_decay_step(/*heat=*/6.f, /*timer=*/5.f,
                                             /*in_view=*/true, /*dt=*/0.1f, t);
        REQUIRE(d.heat == 6.f);
        REQUIRE(d.lose_track_timer == 0.f);
    }
    // Out of view but still inside the grace window -> heat holds, timer accrues.
    {
        HeatDecay d = wanted_heat_decay_step(6.f, 1.f, false, 0.5f, t);
        REQUIRE(d.heat == 6.f);
        REQUIRE(std::fabs(d.lose_track_timer - 1.5f) < 1e-5f);
    }
    // Out of view past the grace window -> heat cools at heat_decay_rate.
    {
        HeatDecay d = wanted_heat_decay_step(6.f, 8.5f, false, 1.0f, t);
        REQUIRE(std::fabs(d.heat - (6.f - 0.22f)) < 1e-5f);
        REQUIRE(std::fabs(d.lose_track_timer - 9.5f) < 1e-5f);
    }
    // Re-sight after the cooldown started refreshes the timer and stops the decay.
    {
        HeatDecay d = wanted_heat_decay_step(4.f, 20.f, true, 1.0f, t);
        REQUIRE(d.heat == 4.f);
        REQUIRE(d.lose_track_timer == 0.f);
    }
    // Decay floors at 0 and can't go negative.
    {
        HeatDecay d = wanted_heat_decay_step(0.1f, 100.f, false, 1.0f, t);
        REQUIRE(d.heat == 0.f);
    }
    // No heat -> stays cleared, timer zeroed (idle, so a re-offence starts fresh).
    {
        HeatDecay d = wanted_heat_decay_step(0.f, 5.f, false, 1.0f, t);
        REQUIRE(d.heat == 0.f);
        REQUIRE(d.lose_track_timer == 0.f);
    }
}

void test_police_standdown_despawn_gate() {
    PoliceTuning t;            // stand_down_distance 90 m
    // N1 coupling note: this MUST track POLICE_DESPAWN_DIST in src/game/traffic.cpp
    // (the anon-namespace, single-consumer hard far-ring). It's hardcoded here
    // because this lightweight target deliberately does NOT link traffic.cpp / GL
    // (see CMakeLists "Lightweight non-render traffic tests"); promoting a
    // single-consumer tuning constant into a shared header just to dedupe a test
    // literal would be the wrong coupling. If you change it there, change it here.
    const float far = 280.f;
    auto sq = [](float d) { return d * d; };

    // Bug 2 pin: chase over, cop still near the player -> must NOT despawn (it
    // reverts to patrol / drives off instead of vanishing in view).
    REQUIRE(!police_should_despawn_standdown(sq(30.f), true,
                                             t.stand_down_distance, far));
    // Chase over, cop has driven beyond the stand-down distance -> despawn.
    REQUIRE(police_should_despawn_standdown(sq(120.f), true,
                                            t.stand_down_distance, far));
    // Active chase (not over), cop nearby -> never despawn just for being close.
    REQUIRE(!police_should_despawn_standdown(sq(50.f), false,
                                             t.stand_down_distance, far));
    // Beyond the hard far ring -> despawn regardless of chase state (it's gone).
    REQUIRE(police_should_despawn_standdown(sq(300.f), false,
                                            t.stand_down_distance, far));
    // Exactly AT the stand-down distance is not past it (strict >).
    REQUIRE(!police_should_despawn_standdown(sq(90.f), true,
                                             t.stand_down_distance, far));
}

// PCG-030 (Aisha B1) — the siren/light-bar/beacon gate (Car::is_pursuit_unit)
// must go dark/quiet the moment a cop gracefully stands down. is_pursuit_unit()
// reduces the Driver enum onto police_is_pursuit_unit(driver_is_police,
// parked_police_unit, standing_down); pin that pure predicate so a stood-down
// cruiser is NOT a pursuit unit (no wail at wanted=0) while a real chaser still
// is — both reach paths Aisha flagged (no-lane fallback pursuer AND an on-foot
// officer's parked cruiser) end up as Parked + police_unit + standing_down.
void test_police_standdown_silences_pursuit_unit() {
    // A real active chaser (Driver::Police): wails. (police, not-parked, not-down)
    REQUIRE(police_is_pursuit_unit(/*driver_is_police=*/true,
                                   /*parked_police_unit=*/false,
                                   /*standing_down=*/false));
    // A deployed cop's parked cruiser mid-chase (Parked + police_unit): wails.
    REQUIRE(police_is_pursuit_unit(false, true, false));

    // B1 fix: a stood-down cruiser (Parked + police_unit + standing_down) — both
    // the no-lane fallback pursuer and the on-foot officer's cruiser at wanted=0 —
    // is NOT a pursuit unit, so siren + light-bar + beacon all go dark.
    REQUIRE(!police_is_pursuit_unit(false, true, true));
    // standing_down dominates even if the engine still reads it as the chaser
    // branch for one frame before its driver flips: never wail once stood down.
    REQUIRE(!police_is_pursuit_unit(true, true, true));

    // An ambient patrol (Driver::AI, neither branch) stays dark, as before.
    REQUIRE(!police_is_pursuit_unit(false, false, false));
}

// PCG-148 — police RESPONSE delay gate. The pure state machine holds the response
// (patrol->pursuit conversion + at-distance spawns) for a random beat after a FRESH
// 0-star escalation, so units engage AFTER the dispatch radio (PCG-125), not on the
// crime frame. Only the initial escalation is gated; a bump while already wanted
// responds immediately. The gate returns responding_level (0 while held, the true
// level once it lapses) — the level Application feeds set_police_response/_context.
void test_police_response_gate() {
    constexpr float kArmed = 7.0f;   // stand-in for the caller's random 5-10 s

    // Idle: no escalation, no hold -> the true level passes straight through. This
    // is also the "already wanted, bump responds immediately" path.
    {
        ResponseGateStep s = police_response_gate_step(ResponseGate{}, /*true=*/3,
                                                       /*escalated=*/false,
                                                       /*dispatch=*/false,
                                                       kArmed, 0.1f);
        REQUIRE(!s.gate.pending);
        REQUIRE(s.responding_level == 3);
    }

    // Full happy path: escalate -> wait for radio -> radio -> count down -> go live.
    ResponseGate g;
    // 1) The crime frame: the 0 -> 1 escalation latches the hold. Response is held
    //    at 0 the SAME frame (units must not engage on the crime frame).
    {
        ResponseGateStep s = police_response_gate_step(g, 1, /*escalated=*/true,
                                                       /*dispatch=*/false, kArmed, 0.1f);
        g = s.gate;
        REQUIRE(g.pending);
        REQUIRE(g.delay == 0.f);
        REQUIRE(s.responding_level == 0);   // held, not 1
    }
    // 2) Waiting for the radio (PCG-125 dispatch delay still running): stays held at
    //    0, and the countdown is NOT armed until the radio actually plays.
    {
        ResponseGateStep s = police_response_gate_step(g, 1, false, false, kArmed, 0.5f);
        g = s.gate;
        REQUIRE(g.pending);
        REQUIRE(g.delay == 0.f);
        REQUIRE(s.responding_level == 0);
    }
    // 3) The dispatch radio goes out -> the response countdown arms to kArmed and
    //    ticks by dt this same frame. Still held.
    {
        ResponseGateStep s = police_response_gate_step(g, 1, false, /*dispatch=*/true,
                                                       kArmed, 0.5f);
        g = s.gate;
        REQUIRE(g.pending);
        REQUIRE(std::fabs(g.delay - (kArmed - 0.5f)) < 1e-5f);
        REQUIRE(s.responding_level == 0);
    }
    // 4) A SECOND dispatch mid-countdown must NOT re-arm or extend the delay (the
    //    arm guard is pending && delay <= 0). The level can climb via bumps
    //    meanwhile; the bumped level is still held at 0 during the grace.
    {
        ResponseGateStep s = police_response_gate_step(g, 2, false, /*dispatch=*/true,
                                                       /*armed=*/99.f, 0.5f);
        g = s.gate;
        REQUIRE(g.pending);
        REQUIRE(std::fabs(g.delay - (kArmed - 1.0f)) < 1e-5f);  // not re-armed to 99
        REQUIRE(s.responding_level == 0);   // bumped level 2 still held at 0
    }
    // 5) Burn the rest of the countdown; on the lapse frame units go live with the
    //    (now bumped) true level and the latch clears.
    {
        ResponseGateStep s = police_response_gate_step(g, 2, false, false, kArmed, 10.f);
        g = s.gate;
        REQUIRE(!g.pending);
        REQUIRE(g.delay == 0.f);
        REQUIRE(s.responding_level == 2);   // live: the true (bumped) level
    }
    // 6) Once live, a further bump while already wanted responds immediately — no
    //    new hold (the escalation edge is 0 -> >=1 only, so it never re-fires here).
    {
        ResponseGateStep s = police_response_gate_step(g, 4, /*escalated=*/false,
                                                       false, kArmed, 0.1f);
        g = s.gate;
        REQUIRE(!g.pending);
        REQUIRE(s.responding_level == 4);
    }

    // Clean reset: heat decayed to 0 (idle), then a NEW 0-star crime arms a fresh
    // hold even if a stale countdown lingered from an escaped prior crime —
    // escalation clears delay to 0 so the new crime's radio re-arms.
    {
        ResponseGate stale;
        stale.pending = false;
        stale.delay   = 3.0f;   // leftover countdown from a prior crime
        ResponseGateStep s = police_response_gate_step(stale, 1, /*escalated=*/true,
                                                       false, kArmed, 0.1f);
        REQUIRE(s.gate.pending);
        REQUIRE(s.gate.delay == 0.f);        // stale countdown cleared
        REQUIRE(s.responding_level == 0);    // freshly held
    }
}

// -----------------------------------------------------------------------------
// PCG-149 — wanted-star HUD blink latch. Pure "report pending" state machine that
// drives the star pulse: blink from the crime until the dispatch radio airs, then
// snap solid. The blink VISUAL is founder feel-checked; these pin the timing edges.
void test_wanted_report_blink_latch() {
    // Idle: no escalation, nothing armed -> stays clear (solid stars).
    REQUIRE(!wanted_report_blink_step(/*pending=*/false, /*escalated=*/false,
                                      /*dispatch_armed=*/false,
                                      /*dispatch_played=*/false, /*wanted=*/0));

    // Fresh 0-star crime WITH a dispatch armed the same frame -> latch: blink.
    bool pending = wanted_report_blink_step(false, /*escalated=*/true,
                                            /*dispatch_armed=*/true,
                                            /*dispatch_played=*/false, /*wanted=*/2);
    REQUIRE(pending);

    // Waiting for the radio (dispatch delay still running): stays blinking.
    pending = wanted_report_blink_step(pending, false, false, false, 2);
    REQUIRE(pending);

    // The dispatch radio airs -> snap solid (clear the latch).
    pending = wanted_report_blink_step(pending, false, false,
                                       /*dispatch_played=*/true, 2);
    REQUIRE(!pending);

    // No stuck-blinking: a fresh crime whose dispatch is NOT armed (e.g. blocked by
    // the callout cooldown) does NOT latch -> stars just go solid, nothing to clear.
    REQUIRE(!wanted_report_blink_step(false, /*escalated=*/true,
                                      /*dispatch_armed=*/false, false, 1));

    // Heat decays to 0 before the radio -> the blink clears cleanly.
    pending = wanted_report_blink_step(false, true, true, false, 3);  // arm
    REQUIRE(pending);
    pending = wanted_report_blink_step(pending, false, false, false, /*wanted=*/0);
    REQUIRE(!pending);

    // A bump while ALREADY wanted (no 0->>=1 edge) never re-arms the blink.
    REQUIRE(!wanted_report_blink_step(false, /*escalated=*/false,
                                      /*dispatch_armed=*/true, false, 3));
    // ...and it doesn't disturb an in-flight blink either (stays pending).
    REQUIRE(wanted_report_blink_step(/*pending=*/true, false, true, false, 3));

    // A later crime after a clean clear re-arms the blink (re-usable latch).
    REQUIRE(wanted_report_blink_step(false, true, true, false, 1));
}

// PCG-174 — police_ram_verdict: attribution of a (player car, police car)
// impact. The player earns heat only when they're the DOMINANT mover into the
// contact; a pursuer ramming the player never attributes. The convention: the
// normal points from the police car TOWARD the player's car.
void test_police_ram_verdict() {
    const glm::vec3 n{0.f, 0.f, 1.f};   // police at -Z, player at +Z

    // Player drives into a stationary (parked / kinematic patrol) cruiser at
    // 8 m/s -> rammed, closing speed = 8.
    {
        auto v = police_ram_verdict({0.f, 0.f, -8.f}, {0.f, 0.f, 0.f}, n);
        REQUIRE(v.player_rammed);
        REQUIRE(std::fabs(v.closing_speed - 8.f) < 1e-4f);
    }
    // Pursuing cruiser rear-ends the fleeing player (cop faster, same
    // direction): closing, but the COP is the mover -> no heat.
    {
        auto v = police_ram_verdict({0.f, 0.f, 10.f}, {0.f, 0.f, 14.f}, n);
        REQUIRE(!v.player_rammed);
        REQUIRE(std::fabs(v.closing_speed - 4.f) < 1e-4f);
    }
    // Cruiser rams a STATIONARY player (PIT while boxed in) -> cop's fault.
    {
        auto v = police_ram_verdict({0.f, 0.f, 0.f}, {0.f, 0.f, 9.f}, n);
        REQUIRE(!v.player_rammed);
    }
    // Love-tap below the 3 m/s ram gate (parking-lot nudge / low-speed grind)
    // never latches, even though the player is the mover.
    {
        auto v = police_ram_verdict({0.f, 0.f, -2.f}, {0.f, 0.f, 0.f}, n);
        REQUIRE(!v.player_rammed);
    }
    // Exactly-equal mutual head-on: benefit of the doubt (strict >) -> no heat.
    {
        auto v = police_ram_verdict({0.f, 0.f, -6.f}, {0.f, 0.f, 6.f}, n);
        REQUIRE(!v.player_rammed);
        REQUIRE(std::fabs(v.closing_speed - 12.f) < 1e-4f);
    }
    // Mutual head-on where the player clearly dominates -> the player's crime.
    {
        auto v = police_ram_verdict({0.f, 0.f, -10.f}, {0.f, 0.f, 3.f}, n);
        REQUIRE(v.player_rammed);
    }
    // T-bone: player drives into the side of a crossing cruiser — the cop's
    // velocity is perpendicular to the contact normal (contributes ~nothing
    // into the hit), the player's is straight in -> the player's crime.
    {
        auto v = police_ram_verdict({0.f, 0.f, -9.f}, {5.f, 0.f, 0.f}, n);
        REQUIRE(v.player_rammed);
        REQUIRE(std::fabs(v.closing_speed - 9.f) < 1e-4f);
    }
    // Separating contact (player pulling away) is not a ram.
    {
        auto v = police_ram_verdict({0.f, 0.f, 5.f}, {0.f, 0.f, 0.f}, n);
        REQUIRE(!v.player_rammed);
    }
}

// Replan trigger (pure): invalid route always replans; otherwise replan only on
// the cadence OR a target move past the threshold — small jitter does NOT thrash.
void test_police_should_replan() {
    const glm::vec2 t0{0.f, 0.f};
    // Fresh route, timer not elapsed, target steady -> hold the route.
    REQUIRE(!police_should_replan(0.2f, 0.6f, t0, t0, 10.f, /*invalid=*/false));
    // Invalid route -> always replan, regardless of timer / move.
    REQUIRE(police_should_replan(0.f, 0.6f, t0, t0, 10.f, /*invalid=*/true));
    // Cadence elapsed -> replan (inclusive at the interval).
    REQUIRE(police_should_replan(0.6f, 0.6f, t0, t0, 10.f, false));
    REQUIRE(police_should_replan(1.0f, 0.6f, t0, t0, 10.f, false));
    // Target moved past the threshold -> early replan.
    REQUIRE(police_should_replan(0.1f, 0.6f, {15.f, 0.f}, t0, 10.f, false));
    // Sub-threshold jitter -> no thrash.
    REQUIRE(!police_should_replan(0.1f, 0.6f, {3.f, 0.f}, t0, 10.f, false));
    REQUIRE(!police_should_replan(0.1f, 0.6f, {0.f, 9.9f}, t0, 10.f, false));
}

// Terminal hand-off boundary (pure): at/under the close-range threshold the route
// is bypassed for the terminal cmd; beyond it, route-following stays in control.
void test_police_terminal_handoff_boundary() {
    const float range = 22.f;
    REQUIRE(police_use_terminal(0.f, range));
    REQUIRE(police_use_terminal(range - 1.f, range));
    REQUIRE(police_use_terminal(range, range));        // inclusive boundary
    REQUIRE(!police_use_terminal(range + 1.f, range)); // outside -> route-follow
    REQUIRE(!police_use_terminal(100.f, range));
}

// XZ OBB overlap via SAT (both boxes' axes; sufficient for a separating-axis
// proof between two rectangles).
bool obb_overlap(glm::vec2 ca, glm::vec2 fa, glm::vec2 cb, glm::vec2 fb,
                 float half_len, float half_wid) {
    const glm::vec2 axes[4] = {fa, {fa.y, -fa.x}, fb, {fb.y, -fb.x}};
    const glm::vec2 d = cb - ca;
    for (const glm::vec2& axis : axes) {
        const glm::vec2 ax = glm::normalize(axis);
        const float ra = half_len * std::abs(glm::dot(fa, ax)) +
                         half_wid * std::abs(glm::dot(glm::vec2{fa.y, -fa.x}, ax));
        const float rb = half_len * std::abs(glm::dot(fb, ax)) +
                         half_wid * std::abs(glm::dot(glm::vec2{fb.y, -fb.x}, ax));
        if (std::abs(glm::dot(d, ax)) > ra + rb) return false;
    }
    return true;
}

// Block composition: N cars for the width class, spanning the full
// carriageway with no OBB overlap and no drivable lateral gap; officers
// strictly downstream of every car (behind the block).
void test_roadblock_layout_spans_width_no_overlap() {
    const RoadblockTuning t{};
    const float half_len = 2.25f, half_wid = 0.95f;   // ~Car5 body
    // Rotated frame too — the layout must hold off-axis, not just along +x.
    const glm::vec2 fwds[2] = {{1.f, 0.f}, {0.6f, 0.8f}};
    for (const glm::vec2& fwd : fwds) {
        const glm::vec2 right{fwd.y, -fwd.x};
        for (float width : {7.f, 8.f, 10.99f, 11.f, 12.f, 16.f}) {
            const glm::vec2 center{30.f, -12.f};
            RoadblockLayout lay =
                roadblock_layout(center, fwd, width, half_len, t);
            const int expect = width >= t.wide_width_m ? 3 : 2;
            REQUIRE(lay.n_cars == expect);
            REQUIRE(lay.n_officers == expect - 1);

            const std::size_t n_cars = static_cast<std::size_t>(lay.n_cars);
            const std::size_t n_off = static_cast<std::size_t>(lay.n_officers);

            // No pair of cruiser OBBs overlaps.
            for (std::size_t i = 0; i < n_cars; ++i)
                for (std::size_t j = i + 1; j < n_cars; ++j)
                    REQUIRE(!obb_overlap(lay.cars[i].pos, lay.cars[i].facing,
                                         lay.cars[j].pos, lay.cars[j].facing,
                                         half_len, half_wid));

            // Lateral coverage: project every car's OBB onto the right axis;
            // the union must reach both kerbs (within 0.8 m) and leave no
            // gap a car could drive through (> 1.0 m) between neighbours.
            std::vector<std::pair<float, float>> spans;
            for (std::size_t i = 0; i < n_cars; ++i) {
                const glm::vec2 f = lay.cars[i].facing;
                const float mid = glm::dot(lay.cars[i].pos - center, right);
                const float ext =
                    half_len * std::abs(glm::dot(f, right)) +
                    half_wid * std::abs(glm::dot(glm::vec2{f.y, -f.x}, right));
                spans.push_back({mid - ext, mid + ext});
            }
            std::sort(spans.begin(), spans.end());
            REQUIRE(spans.front().first <= -width * 0.5f + 0.8f);
            REQUIRE(spans.back().second >= width * 0.5f - 0.8f);
            for (std::size_t i = 0; i + 1 < spans.size(); ++i)
                REQUIRE(spans[i + 1].first - spans[i].second <= 1.0f);

            // Officers behind the block: strictly downstream of every car.
            float car_max_lon = -1e9f;
            for (std::size_t i = 0; i < n_cars; ++i)
                car_max_lon = std::max(
                    car_max_lon, glm::dot(lay.cars[i].pos - center, fwd));
            for (std::size_t k = 0; k < n_off; ++k) {
                const float lon = glm::dot(lay.officers[k] - center, fwd);
                REQUIRE(lon > car_max_lon);
                // ...and still over the road, not in a building.
                REQUIRE(std::abs(glm::dot(lay.officers[k] - center, right))
                        <= width * 0.5f);
            }
        }
    }
    // Degenerate inputs fail closed.
    REQUIRE(roadblock_layout({0.f, 0.f}, {0.f, 0.f}, 8.f, half_len, t)
                .n_cars == 0);
    REQUIRE(roadblock_layout({0.f, 0.f}, {1.f, 0.f}, 0.f, half_len, t)
                .n_cars == 0);
}

// Breakthrough predicate: past the block within the road corridor = breached;
// short of it, behind it, or on a parallel street = not.
void test_roadblock_breach_predicate() {
    const RoadblockTuning t{};   // pass 8 m, side slack 6 m
    const glm::vec2 c{0.f, 0.f}, fwd{1.f, 0.f};
    const float w = 8.f;
    REQUIRE(!roadblock_breached({-30.f, 0.f}, c, fwd, w, t));  // approaching
    REQUIRE(!roadblock_breached({4.f, 0.f}, c, fwd, w, t));    // at the block
    REQUIRE(roadblock_breached({8.f, 0.f}, c, fwd, w, t));     // inclusive edge
    REQUIRE(roadblock_breached({10.f, 0.f}, c, fwd, w, t));    // through it
    REQUIRE(roadblock_breached({10.f, 9.9f}, c, fwd, w, t));   // shoulder pass
    REQUIRE(!roadblock_breached({10.f, 10.1f}, c, fwd, w, t)); // parallel street
    REQUIRE(!roadblock_breached({-10.f, 0.f}, c, fwd, w, t));  // still behind
    REQUIRE(!roadblock_breached({10.f, 0.f}, c, {0.f, 0.f}, w, t)); // degenerate
}

// Dispatcher trigger: wanted floor, cooldown tick, single-block rule, and the
// POLICE_MAX_CARS budget (a block THINS the pack, never exceeds it).
void test_roadblock_dispatch_gate() {
    const RoadblockTuning t{};   // min_wanted 3
    // Below the wanted floor: never stages, cooldown still ticks.
    RoadblockDispatchStep s =
        roadblock_dispatch_step(false, 1.0f, 2, 0, 8, 0.25f, t);
    REQUIRE(!s.try_stage);
    REQUIRE(std::abs(s.cooldown - 0.75f) < 1e-4f);
    // Cooldown not elapsed: no stage.
    REQUIRE(!roadblock_dispatch_step(false, 5.f, 3, 0, 8, 0.1f, t).try_stage);
    // Wanted >= 3, cooldown lapsed, budget free: stage.
    REQUIRE(roadblock_dispatch_step(false, 0.05f, 3, 3, 8, 0.1f, t).try_stage);
    REQUIRE(roadblock_dispatch_step(false, 0.f, 5, 0, 8, 0.1f, t).try_stage);
    // Budget: the minimum 2-car block must fit under max_police.
    REQUIRE(!roadblock_dispatch_step(false, 0.f, 3, 7, 8, 0.1f, t).try_stage);
    REQUIRE(roadblock_dispatch_step(false, 0.f, 3, 6, 8, 0.1f, t).try_stage);
    // One block at a time: active suppresses staging (cooldown still ticks).
    RoadblockDispatchStep a =
        roadblock_dispatch_step(true, 0.5f, 5, 0, 8, 0.2f, t);
    REQUIRE(!a.try_stage);
    REQUIRE(std::abs(a.cooldown - 0.3f) < 1e-4f);
    // Cooldown floors at zero.
    REQUIRE(roadblock_dispatch_step(true, 0.1f, 5, 0, 8, 1.f, t).cooldown == 0.f);
}

}  // namespace

int main() {
    // Witnessing a crime, and holding contact once engaged.
    test_police_witness_gate();
    test_police_maintains_contact_gate();
    test_police_spawn_fallback();
    test_police_terminal_pursuit_characterization();

    // LOS-gated wanted decay and the graceful stand-down.
    test_wanted_heat_decay_los_gated();
    test_police_standdown_despawn_gate();
    test_police_standdown_silences_pursuit_unit();

    // Holding the response until the dispatch radio has gone out.
    test_police_response_gate();
    test_wanted_report_blink_latch();

    // Who rammed whom.
    test_police_ram_verdict();

    // Route-following hand-off (the pure halves of it).
    test_police_should_replan();
    test_police_terminal_handoff_boundary();

    // Roadblocks, downstream of a chosen site.
    test_roadblock_layout_spans_width_no_overlap();
    test_roadblock_breach_predicate();
    test_roadblock_dispatch_gate();

    return apricot_test::done("police_ai_tests");
}
