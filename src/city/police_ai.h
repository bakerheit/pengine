#pragma once

#include <array>
#include <cstdint>

#include <glm/glm.hpp>

// Police decision logic — pure, headless-testable.
//
// This header carries ONLY plain-data helpers: no traffic system, no scene, no
// world state. The drive loop feeds these positions / counts / scalars and
// routes the result into the kinematic and dynamic drive paths, so every
// branchy judgement is a free function you can pin with a table of inputs.
//
// glm + std only, which is what lets it be embedded in TrafficTuning
// (traffic_ai.h includes this) without dragging anything behind it.
//
// PROVENANCE. Lifted from probablecause (PENG-29). The PCG-nnn markers below
// are ITS ticket numbers, not this board's, and they are kept on purpose: the
// comments they head explain why a constant is the value it is, and the ticket
// is the only remaining trace of the session that chose it. See
// src/city/README.md.

namespace apricot {

// PCG-245 — dispatcher-level roadblock tactic knobs. Net-new, so these defaults
// ARE the baseline. Embedded in PoliceTuning (and so in TrafficTuning) for the
// live-tuning pattern. All pure kernels below take this by const-ref.
struct RoadblockTuning {
    int   min_wanted   = 3;      // stage roadblocks only at wanted >= this
    float cooldown_s   = 25.f;   // s between roadblocks (spec: ~20-30 s)
    float retry_s      = 2.f;    // s before retrying after a failed stage

    // Site selection: walk the lane graph forward from the player's directed
    // lane and place the block in this arc-length window ahead.
    float min_ahead_m       = 120.f;
    float max_ahead_m       = 200.f;
    float min_width_m       = 7.f;   // carriageways narrower than this can't be
                                     // blocked meaningfully -> reject the site
    float wide_width_m      = 11.f;  // >= this width: 3 cruisers, else 2
    float junction_setback_m = 14.f; // a preferred site sits this far before a
                                     // real junction (>= 3 approaches)
    float min_player_gap_m  = 100.f; // planar player->site floor — the spawn
                                     // stays out of sight (try_spawn_police's
                                     // ring discipline; sites are also >=
                                     // min_ahead_m of ROAD ahead by design)
    float max_snap_m        = 10.f;  // player farther than this from any
                                     // aligned lane -> off-road, no site

    // Block composition (roadblock_layout).
    float car_angle_deg = 45.f;  // cruiser yaw off the road axis, alternating
    float car_stagger_m = 3.2f;  // alternating fore/aft offset (chevron) so the
                                 // angled OBBs interlock without overlapping
    float officer_back_m = 3.5f; // officers this far downstream of the car line
                                 // (behind the block, away from the approach)

    // Breakthrough / lifecycle.
    float breach_pass_m       = 8.f;   // player this far past the block = through
    float breach_side_slack_m = 6.f;   // lateral tolerance beyond the kerb —
                                       // squeezing past on the shoulder counts
    float stale_dist_m        = 260.f; // player farther than this from the site:
                                       // release the block into normal pursuit
};

// Runtime-tunable knobs for the realism slice. Net-new this ticket, so these
// defaults ARE the baseline (no shipped constexpr to reproduce). Embedded in
// TrafficTuning so a live tuning panel can iterate without a recompile.
struct PoliceTuning {
    // Fraction of civilian AI spawns that come up as a cruising patrol cop
    // (Driver::AI, police livery) blended into normal traffic. 0 = no ambient
    // patrols (pre-realism behaviour: police only ever spawn on a wanted level).
    float patrol_fraction = 0.12f;

    // Witness gate. A cruising patrol converts to a pursuer when the offender is
    // within `witness_range` metres AND inside the cop's forward view cone
    // (dot(forward, to-offender) >= witness_fov_cos) AND the line of sight is
    // unobstructed (tested in production against world collision). 0.30 ~= a 145
    // deg total cone — wide enough that a crime committed beside a stopped
    // cruiser is reliably seen, tight enough that it doesn't "see" behind itself.
    float witness_range   = 45.f;
    float witness_fov_cos = 0.30f;

    // PCG-030 — wanted heat de-escalation (LOS-gated decay + graceful stand-down).
    //
    // Bug 1 (LOS-gated decay): while the offender is inside ANY police unit's view
    // (the SAME range + cone + LOS gate as the witness check above, applied across
    // every cop), the wanted level HOLDS — it does not decay and "last seen"
    // refreshes. Only once the player is out of ALL police LOS does the
    // lose-track grace run; if the player isn't re-sighted within that window,
    // heat then cools at `heat_decay_rate` per second. The window itself is
    // PER LEVEL — kPoliceLevelProfiles[level].escape_s, thirty seconds at one
    // star and eighty-two at five — and is passed to wanted_heat_decay_step by
    // the caller. (heat_decay_rate carries the pre-PCG-030 rate so the cooldown
    // slope itself is unchanged — only the GATE changed from a pure timer to
    // line-of-sight.)
    float heat_decay_rate   = 0.22f;   // heat/s once the lose-track grace elapses

    // Bug 2 (graceful stand-down): when the chase ends a pursuer must not pop out
    // of existence in the player's face. A standing-down cruiser reverts to ambient
    // AI patrol and is despawned by the normal traffic rules — only once it is
    // beyond `stand_down_distance` from the (former) target, i.e. it has visibly
    // peeled off and driven away.
    float stand_down_distance = 90.f;  // m; keep a standing-down cop until beyond this

    // How far an ENGAGED pursuer may fall behind before the streamer is allowed
    // to delete it. This is not a nicety. Ordinary traffic retires at ~320 m,
    // and a cruiser that loses fifteen seconds to a U-turn at city speeds is
    // three hundred metres back — so with no exemption EVERY pursuer was
    // deleted mid-chase and replaced by whichever patrol happened to be near
    // the player. That is why the chase felt like nobody was chasing: there was
    // never one car following you, only a rotating cast of local cars, each
    // deleted the moment it fell behind. Beyond THIS ring the unit genuinely
    // has no chance and a fresh local responder is the better answer.
    float pursuit_retire_m = 650.f;

    // PCG-030 (2nd increment) — pursuit CONTACT range. An ALREADY-ENGAGED unit (an
    // active pursuer, or a deployed cruiser whose officer is on foot) holds the
    // player's heat as long as it stays within this range with clear line of sight
    // — NO forward view cone. An engaged unit already knows exactly where the
    // player is; it must not need him in its windshield to keep heat hot. (The
    // founder's feel-check caught a pursuer REVERSING to ram — facing dead away
    // from the player, so outside the witness cone — letting heat decay to 0
    // point-blank.) Deliberately looser than the tighter witness_range (45 m): a
    // chasing cop a bit further back with a clear sightline is still tracking you.
    float pursuit_contact_range = 70.f;  // m; engaged-unit heat-hold radius

    // PCG-033 — lane-graph pursuit routing. A pursuer plans a lane route toward
    // the target (a lane-route planner, not in this tree yet) and follows it,
    // steering at a look-ahead waypoint on the route so it tracks the road,
    // until it closes inside route_handoff_range — where control hands to the
    // terminal ram/reverse/handbrake cmd (police_terminal_pursuit_cmd) aimed at
    // the target itself. Routing layers ON TOP of that terminal phase; it never
    // replaces it.
    //   route_handoff_range — close-quarters radius where route-follow yields to
    //                         the terminal cmd (the unchanged PCG-011 end-game).
    //   route_lookahead     — how far ahead along the route to place the steer
    //                         point; bigger = smoother/wider, smaller = tighter
    //                         corner tracking.
    //   replan_interval     — seconds between forced route refreshes (throttle so
    //                         replanning is never per-frame).
    //   replan_target_move  — metres the target must move from its position at the
    //                         last plan to force an early replan between ticks (so
    //                         a juking target is tracked without per-frame churn).
    float route_handoff_range = 22.f;
    float route_lookahead     = 12.f;
    float replan_interval     = 0.6f;
    float replan_target_move  = 10.f;

    // An ENGAGED unit does not queue at the lights. Pre-PENG this was not a
    // tuning value because there was no override at all: a pursuer sat through
    // the same red, the same stop-sign dwell and the same yield creep as the
    // delivery van behind it, and the chase ended the moment the player crossed
    // a junction on green. What an override does NOT touch is cross-traffic
    // conflict — a cop is exempt from the SIGNAL, never from the car actually
    // in the box — so the cruiser noses the junction at this speed and still
    // gives way to a body it would hit.
    //   control_override_speed — cap while crossing against a red or a stop.
    //   control_override_slack — extra metres per metre of remaining approach,
    //                            so the run-up is quick and only the line is slow.
    //   (whether a level may ram at all lives in kPoliceLevelProfiles below;
    //    a one-star traffic stop is not a demolition derby.)
    //   ram_range / ram_min_ahead — the window, ahead along the cruiser's own
    //                            lane, in which lining up on the player is worth
    //                            leaving the lane centre for.
    float control_override_speed = 10.0f;
    float control_override_slack = 0.45f;

    // FREE-DRIVE PURSUIT — the GTA move, and the one thing a lane-following
    // agent structurally cannot do: leave the road. Inside `free_chase_range`
    // a dispatched cruiser stops being traffic and becomes a car, steered at
    // the suspect by police_terminal_pursuit_cmd over the real world — across
    // a forecourt, over a kerb, through a car park, straight at you. Release is
    // deliberately much further out than engage so a cruiser does not flicker
    // between the two modes at the boundary.
    //
    // 22 m is not a guess: it is `route_handoff_range` above, the distance the
    // port itself documents for handing route-following to the terminal
    // command. Set wider (34 m was tried) the cruiser spends the extra
    // distance driving AT a moving target, overshoots, and has to turn around
    // in front of the player — which is where the reversing comes from. Close
    // in it is a contact move and the nose barely leaves the target at all:
    // measured, 22 m halves both the time spent reversing and the time spent
    // pointed the wrong way against 34 m.
    float free_chase_range   = 22.0f;
    float free_chase_release = 60.0f;

    // Ambient turn caps are COMFORT limits — 11 m/s straight through a
    // junction, 6 for a left, 3.5 for a U-turn — and they apply from 32 m out.
    // On a grid that is most of a chase, and a cruiser obeying them crawls
    // every corner while the suspect does fifty. A pursuit scales them; the
    // turn curve itself is unchanged, so this buys speed, not a different line.
    float turn_speed_scale = 1.7f;

    float ram_range       = 34.0f;
    float ram_min_ahead   = 5.0f;

    // ON-DEMAND CRUISERS (PENG-45). When the dispatcher finds no resident
    // patrol to convert, one is instantiated on a lane in this annulus around
    // the wanted centre — outside the activation radius so it is never seen
    // appearing, inside the retire radius so the next refresh does not delete
    // it — occluded from the centre, biased toward the suspect's heading.
    // Never keyed on the camera.
    float spawn_min_m = 220.0f;
    float spawn_max_m = 300.0f;
    // THE BEAT OF NOTHING. A crime is followed by `radio_latency_s` of silence
    // before the dispatch radio goes out, then the district's authored
    // response time before any non-witness unit is converted or spawned. A
    // patrol that saw it happen reacts on the frame regardless. Lanes with no
    // authored response (test grids, editor roads) use `default_response_s`.
    // Both at zero disables the hold entirely.
    float radio_latency_s = 1.5f;
    float default_response_s = 12.0f;

    // PCG-245 — dispatcher-level roadblock tactic (see RoadblockTuning above).
    RoadblockTuning roadblock{};
};

// WHAT A WANTED LEVEL MEANS, in one place. Before this table the level was
// read through a handful of literals scattered across the crowd, the lane
// maneuvers and the kernels below — `min(8, level + 2)` here, a three-way
// cadence ladder there, `level <= 2` in a hazard gate — and the only way to
// learn what three stars did differently from two was to grep. Escalation
// content (search timers, spawning, roadblocks, firing at a car) hangs off
// this row, so adding a level's worth of behaviour is a column, not a hunt.
//
// Levels 1–5 are numerically what the literals were, on purpose: the runtime
// suites pin the cadence in exact steps and the unit budget by count, and a
// refactor that moves them is not a refactor. `escape_s` is the one new
// number — seconds out of every officer's line of sight before heat starts
// to cool — and it takes over from the flat 8 s window when search mode
// lands. Row 0 is "no crime": nobody is dispatched and nothing escalates.
struct PoliceLevelProfile {
    int   units;             // dispatcher target: converted patrols + spawns
    float cadence_s;         // one dispatcher conversion per this many seconds
    float escape_s;          // out-of-all-LOS seconds before heat cools
    float detect_range_m;    // a unit with a clear world ray inside this KNOWS
                             // where the suspect is (the wanted centre tracks)
    float cruise_bonus_mps;  // added to the pursuit catch-up speed
    bool  hazard_braking;    // a pursuer still brakes for the player as a hazard
    bool  may_ram;           // deliberate contact allowed
    bool  may_spawn;         // an off-screen cruiser may be instantiated
    bool  fire_at_vehicle;   // officers may fire on a driving suspect (reserved)
};

// detect_range_m is the reference game's per-star detection radius (145 m at
// one star to 850 m at five): the distance at which an officer who can see
// you is considered to know where you are. It is NOT the heat-hold contact
// range (pursuit_contact_range, 70 m) and not the witness cone.
inline constexpr PoliceLevelProfile kPoliceLevelProfiles[6] = {
    // units cadence escape detect cruise  hazard ram    spawn  fire
    {0,      1.4f,    8.0f,   0.0f, 0.0f,  true,  false, false, false}, // 0: clean
    {3,      1.4f,   30.0f, 145.0f, 0.6f,  true,  false, true,  false},
    {4,      0.7f,   37.0f, 230.0f, 1.2f,  true,  true,  true,  false},
    {5,      0.7f,   45.0f, 340.0f, 1.8f,  false, true,  true,  false},
    {6,      0.4f,   56.0f, 510.0f, 2.4f,  false, true,  true,  true },
    {7,      0.4f,   82.0f, 850.0f, 3.0f,  false, true,  true,  true },
};

// Clamps: a level below zero is clean, one above five is five. Every caller
// that used to clamp on its own now gets the same answer from here.
constexpr const PoliceLevelProfile& police_level_profile(int wanted_level) {
    return kPoliceLevelProfiles[wanted_level < 0 ? 0 : wanted_level > 5 ? 5
                                                                        : wanted_level];
}

// The first level at which `may_ram` is set — what `ram_min_wanted` used to
// be, derived from the table instead of duplicated beside it.
constexpr int police_min_ram_level() {
    for (int level = 0; level < 6; ++level)
        if (kPoliceLevelProfiles[level].may_ram) return level;
    return 6;
}

static_assert(police_min_ram_level() == 2, "two stars is the ramming threshold");
static_assert(kPoliceLevelProfiles[0].units == 0, "a clean player is not dispatched on");
static_assert(kPoliceLevelProfiles[1].units <= kPoliceLevelProfiles[2].units &&
              kPoliceLevelProfiles[2].units <= kPoliceLevelProfiles[3].units &&
              kPoliceLevelProfiles[3].units <= kPoliceLevelProfiles[4].units &&
              kPoliceLevelProfiles[4].units <= kPoliceLevelProfiles[5].units,
              "more stars never means fewer units");
static_assert(kPoliceLevelProfiles[1].detect_range_m < kPoliceLevelProfiles[2].detect_range_m &&
              kPoliceLevelProfiles[2].detect_range_m < kPoliceLevelProfiles[3].detect_range_m &&
              kPoliceLevelProfiles[3].detect_range_m < kPoliceLevelProfiles[4].detect_range_m &&
              kPoliceLevelProfiles[4].detect_range_m < kPoliceLevelProfiles[5].detect_range_m,
              "more stars see further");
static_assert(kPoliceLevelProfiles[1].escape_s < kPoliceLevelProfiles[2].escape_s &&
              kPoliceLevelProfiles[2].escape_s < kPoliceLevelProfiles[3].escape_s &&
              kPoliceLevelProfiles[3].escape_s < kPoliceLevelProfiles[4].escape_s &&
              kPoliceLevelProfiles[4].escape_s < kPoliceLevelProfiles[5].escape_s,
              "more stars always takes longer to shake");

// An engaged pursuer's speed at a control it is running (pure). `slack` is the
// remaining metres to the stop line, and the result is the cap that REPLACES
// the ordinary hold — never a stop. Returns +inf when the unit is not engaged,
// which is the identity for the caller's std::min and leaves ordinary traffic
// (and a patrol that has not been dispatched) on exactly the path it had.
float police_control_override_speed(bool engaged, float slack_m,
                                    const PoliceTuning& t);

// Should this pursuer deliberately drive INTO the target (pure)? True inside the
// window where a swerve out of the lane centre is worth planning: engaged, the
// heat is high enough to justify contact, the target is in a car ahead of the
// cruiser along its own lane, and the cruiser is carrying enough speed for the
// contact to mean anything. The geometry check on the actual arc — kerbs,
// medians, other traffic — stays with the caller, which is the only thing that
// can see the road.
bool police_should_ram(bool engaged, bool target_on_foot, int wanted_level,
                       float ahead_m, float range_m, float speed_mps,
                       const PoliceTuning& t);

// PCG-011 witness gate (pure). True when a cruising patrol should convert to a
// pursuer: a crime is active, the offender is in sight range, inside the forward
// view cone, and the line of sight is clear. The LOS raycast needs world
// collision (not headless), so its result is passed in as `los_clear`; the range
// + FOV geometry is pure 2D XZ and unit-tested here. Degenerate inputs (offender
// coincident with the cop, or a zero facing vector) fail closed — no witness.
//
//   cop_pos / cop_fwd  — cop world XZ position and (un-normalised OK) heading.
//   offender_pos       — offender world XZ (the wanted target).
//   crime_active       — the existing crime/wanted signal (wanted level > 0).
//   los_clear          — true when nothing static occludes cop -> offender.
bool police_can_witness(glm::vec2 cop_pos, glm::vec2 cop_fwd,
                        glm::vec2 offender_pos, bool crime_active,
                        bool los_clear, const PoliceTuning& t);

// PCG-030 (2nd increment) — pursuit CONTACT gate (pure). Sibling to
// police_can_witness, for a unit that is ALREADY engaged (an active pursuer, or a
// deployed cruiser whose officer is on foot). Where witnessing decides whether a
// patrol should START a chase — and so demands the offender inside the forward
// view cone — maintaining contact only asks whether an engaged unit STILL has the
// player: range + clear line of sight, NO cone. Facing is irrelevant (a pursuer
// reversing to ram faces away yet is right on the player's bumper); only a genuine
// LOS break (world occlusion — a corner / building) drops contact. Unlike the
// witness gate this does NOT fail closed on a coincident offender: a cruiser
// pressed against the player is the tightest possible contact, not a degenerate
// (there is no cone division here to guard). The LOS raycast needs world collision
// (not headless) so its result is passed in as `los_clear`; the range cull is pure
// 2D XZ and unit-tested here.
//
//   cop_pos       — cop world XZ position.
//   offender_pos  — offender world XZ (the wanted target).
//   los_clear     — true when nothing static occludes cop -> offender.
bool police_maintains_contact(glm::vec2 cop_pos, glm::vec2 offender_pos,
                              bool los_clear, const PoliceTuning& t);

// PCG-011 spawn-at-distance fallback (pure). How many fresh cruisers to route in
// from off-screen this frame, given the pursuit target size and how many units
// are ALREADY engaged (converted ambient patrols + previously-spawned
// pursuers). Converted patrols count as engaged, so an organic responder shrinks
// the fallback rather than stacking a fresh spawn on top of it — the
// "prefer a nearby patrol over a spawn" choice. Never negative; 0 when no crime
// is active. (Production still throttles to one spawn per timer; this is the
// gate + the deficit it draws toward.)
int police_spawn_fallback_count(bool crime_active, int target_units,
                                int engaged_units);

// PCG-011 terminal close-quarters pursuit command (pure). Reproduces today's
// steer-at-target cruiser logic EXACTLY (the tactics the epic's path-planner
// child will later layer on TOP of), extracted from traffic_drive.cpp so it can
// be characterization-tested headless and reused unchanged. Inputs are in the
// cop's own frame toward the target:
//   dist  — planar gap to the target (m).
//   ahead — dot(forward, dir-to-target)  (+1 dead ahead, -1 directly behind).
//   side  — dot(right,   dir-to-target)  (+1 hard right, -1 hard left).
//   speed — the cop's current speed (m/s).
// Mirrors the prior inline branch order 1:1: ram when lined up & close, reverse
// when the target slips behind at low speed, brake-into-turn when behind at
// speed, handbrake to swing the tail around on a hard side angle at speed.
struct PursuitCmd {
    float throttle  = 0.f;   // signed: +1 forward, -1 reverse
    float brake     = 0.f;
    float steer     = 0.f;   // [-1, 1]
    bool  handbrake = false;
};
//
// TURNING AROUND. `forward_blocked` says the caller has looked and there is
// something solid immediately in front of the nose. It exists because a car
// with room turns around FORWARDS — full lock, one arc — and only backs up
// when it is boxed in. The original port reversed unconditionally whenever the
// target was behind, and reversed with the steering pointed AT the target,
// which is exactly backwards: the front wheels still steer in reverse, so the
// nose swings AWAY from where they point. A cruiser that missed a pass would
// therefore back up, fail to come round, keep the target behind it, and go on
// reversing into whatever was behind it for the rest of the chase.
PursuitCmd police_terminal_pursuit_cmd(float dist, float ahead, float side,
                                       float speed, bool forward_blocked = false);

// PCG-033 — pursuit replan trigger (pure). True when a pursuer should re-plan its
// lane route THIS frame: the existing route is invalid (empty / fully consumed),
// OR the cadence has elapsed (since_last_replan >= replan_interval), OR the target
// has moved more than move_thresh from where it was at the last plan. Throttles
// replanning to a cadence + a moved-enough trigger so it never thrashes per-frame,
// while still refreshing promptly when the target jukes. Pinned headless so the
// branch order is stable.
bool police_should_replan(float since_last_replan, float replan_interval,
                          glm::vec2 target_now, glm::vec2 target_at_last_plan,
                          float move_thresh, bool route_invalid);

// PCG-033 — terminal hand-off boundary (pure). True when the pursuer is within
// close-quarters range of the target, so route-following yields to the terminal
// ram/reverse/handbrake (police_terminal_pursuit_cmd) aimed at the target itself.
// The single boundary between the routing layer and the unchanged PCG-011 end-
// game (inclusive at the threshold). Trivial, but named + pinned so the hand-off
// distance is one well-tested decision rather than an inline magic comparison.
bool police_use_terminal(float dist_to_target, float handoff_range);

// Aim route planning ahead of a moving suspect instead of at the point they
// occupied on the last 0.6 s replan. Lead is capped so a fast airborne or
// corrupted target cannot send a cruiser across the city.
glm::vec2 police_pursuit_intercept(glm::vec2 target, glm::vec2 velocity);

// Engaged units need enough headroom to close on a speeder, including on a
// freeway. Normal patrols still use their authored cruise speed and every
// junction/leader safety gate remains in force after this target is chosen.
float police_pursuit_cruise_mps(float road_limit_mps,
                                float target_speed_mps,
                                int wanted_level);

// The cap a pursuer takes a junction movement at (pure). `ambient_cap` is the
// comfort limit the same movement would get from ordinary traffic; a
// disengaged unit is handed it back unchanged, so an ambient patrol corners
// exactly as it always did.
float police_turn_speed_mps(float ambient_cap, bool engaged,
                            const PoliceTuning& t);

// PCG-030 wanted heat de-escalation (pure, Bug 1). Given the current heat, the
// running lose-track timer (seconds the offender has been out of ALL police LOS),
// whether the offender is in any cop's view THIS frame, and the frame dt, returns
// the next (heat, lose_track_timer):
//   in view      -> the level HOLDS; the timer resets to 0 ("last seen" refreshed).
//   out of view  -> the timer accrues; only once it passes `lose_track_window_s`
//                   does heat decay at heat_decay_rate per second. Heat floors
//                   at 0. The window is the caller's: the wanted system passes
//                   the profile's escape_s for the CURRENT level.
// No sim / scene state, so the hold/grace/decay branch is pinned headless by the
// headless. (The 1..5 level bucketing stays with the wanted system.)
struct HeatDecay {
    float heat             = 0.f;
    float lose_track_timer = 0.f;
};
HeatDecay wanted_heat_decay_step(float heat, float lose_track_timer,
                                 bool in_police_view, float dt,
                                 float lose_track_window_s,
                                 const PoliceTuning& t);

// PCG-030 graceful police stand-down (pure, Bug 2). Decides whether a police
// cruiser may despawn THIS frame, given its squared planar distance to the
// (former) chase target and whether the chase is over (wanted cleared):
//   beyond the hard far-despawn ring          -> true  (it's gone regardless).
//   chase over AND beyond stand_down_distance -> true  (peeled off, drove away).
//   otherwise                                 -> false.
// The near-and-chase-over case returns false on purpose: the caller reverts that
// cop to ambient AI patrol so it drives away on its own rather than vanishing in
// the player's view. Distances are squared (caller-supplied) to avoid a sqrt.
bool police_should_despawn_standdown(float dist2_to_target, bool chase_over,
                                     float stand_down_distance,
                                     float far_despawn_distance);

// PCG-148 — police RESPONSE delay gate (pure). Holds the actual police *response*
// (patrol->pursuit conversion + at-distance spawns) for a beat after a FRESH
// 0-star -> wanted escalation, so units engage AFTER the dispatch radio callout
// (PCG-125) rather than on the crime frame. The world then reads in believable
// order: crime -> radio call goes out -> a beat later, units respond.
//
// Only the initial escalation is gated. The gate is fed the TRUE wanted level and
// returns the level to drive the response with (`responding_level`): 0 while the
// hold is active, the true level once it lapses. The HUD stars read the true level
// directly and stay immediate — only the pursuit response is held.
//
// State machine (carried in ResponseGate):
//   idle              (!pending)          -> responding_level == true_level: the
//                                            response passes the true level through
//                                            every frame (bumps while already
//                                            wanted respond immediately).
//   waiting-for-radio (pending, delay<=0) -> a fresh 0-star escalation fired; the
//                                            response is held at 0 until the
//                                            dispatch radio goes out.
//   counting-down     (pending, delay>0)  -> the radio went out; the response stays
//                                            held at 0 until the random delay lapses.
//
// Per-frame transitions (applied in order):
//   escalated_from_zero -> latch: pending=true, delay=0. Clears any stale countdown
//                          left by an escaped prior crime, so the reset is clean and
//                          the new crime's radio arms a fresh delay (AC: latch resets
//                          cleanly, like PCG-125's).
//   dispatch_played & pending & delay<=0 -> start the countdown: delay=armed_delay
//                          (the caller's random 5-10 s, house-style LCG). The radio
//                          going out is what starts the response clock, so the total
//                          is crime + dispatch_delay(PCG-125) + response_delay.
//   pending & delay>0 -> delay -= dt; on lapse pending=false (units go live with the
//                          true level).
//
// A bump while already wanted never fires escalated_from_zero (that edge is 0 -> >=1
// only), so pending stays false and the true level passes straight through — no added
// or reset delay. A bump DURING the initial hold likewise doesn't re-latch or extend:
// the response stays held at 0 (the held level) until the original timer lapses, then
// the true (possibly bumped) level goes live.
struct ResponseGate {
    bool  pending = false;  // response held: a fresh escalation fired, not yet live
    float delay   = 0.f;    // s remaining once the dispatch radio has gone out
};
struct ResponseGateStep {
    ResponseGate gate;        // next state (store back onto the caller's ResponseGate)
    int responding_level = 0; // level to feed set_police_response / _context / spawn
};
ResponseGateStep police_response_gate_step(ResponseGate gate, int true_level,
                                           bool escalated_from_zero,
                                           bool dispatch_played,
                                           float armed_delay, float dt);

// PCG-149 — pure "report pending" latch for the wanted-star HUD blink. This is
// the HUD counterpart to the response gate above: while the report is pending the
// newly-earned stars BLINK (the crime is being called in); once the dispatch radio
// airs they snap solid. Returns the next pending state; the caller stores it back
// and derives the pulse factor from it (the game owns the clock). Kept pure so the
// timing edges pin headless — the blink VISUAL itself is founder feel-checked.
//
// Latch on the FRESH 0-star escalation edge, but ONLY when a dispatch is genuinely
// armed the same frame, so the guaranteed radio event (PCG-125's delay always
// lapses -> plays) will clear it — no stuck-blinking. A bump while already wanted
// never fires escalated_from_zero, so it can't (re)arm the blink. Cleared when the
// dispatch radio plays (stars solid) OR heat returns to 0 (stars clear cleanly; a
// later crime re-arms). Radio/heat-clear win over a same-frame arm (unreachable in
// practice — an arm needs the 0->>=1 edge, which can't coincide with a prior crime's
// radio — but the safe default is "go solid", never stuck blinking).
bool wanted_report_blink_step(bool pending, bool escalated_from_zero,
                              bool dispatch_armed, bool dispatch_played,
                              int wanted_level);

// PCG-030 (Aisha B1) — pure predicate behind Car::is_pursuit_unit(). A cop's
// siren, emissive light-bar and beacon halo (all three gates in Application)
// key off this single flag, so it is the one place "is this cruiser wailing?"
// is decided. A car is an ENGAGED pursuit unit iff it is an active chaser
// (Driver::Police) OR a deployed cop's parked cruiser (Driver::Parked +
// police_unit) — UNLESS it has gracefully STOOD DOWN (standing_down): a
// stood-down cruiser goes dark/quiet immediately and lingers silently until the
// despawn sweep removes it once the player is past stand_down_distance (the
// sweep keys off police_unit, NOT this predicate, so silencing it never keeps it
// alive). Kept pure (plain booleans, no TrafficSystem::Driver enum) so the gate
// is pinned headless without dragging the traffic system in; whoever owns the
// driver enum owns the enum->bool reduction.
inline bool police_is_pursuit_unit(bool driver_is_police,
                                   bool parked_police_unit,
                                   bool standing_down) {
    if (standing_down) return false;
    return driver_is_police || parked_police_unit;
}

// PCG-174 — pure verdict: when the player's car and a police car collide, was
// that the PLAYER ramming the cruiser (a crime), or the cruiser ramming the
// player (pursuit tactics — no heat)? Attribution rule: the player is at fault
// only when they are the DOMINANT mover into the contact — their approach
// speed along the contact normal strictly exceeds the cruiser's. So a pursuer
// PIT-ramming / rear-ending the player attributes to the cop (the player's
// component is away from or below the cop's), while driving into a parked,
// patrolling or crossing cruiser attributes to the player. An exactly-equal
// mutual head-on gives the player the benefit of the doubt (strict >). Kept
// pure (glm only) so the rule pins headless; the collision
// resolver feeds it the pre-impulse contact-point velocities.
//
// Below this total closing speed a contact is resting contact / solver
// vibration, not a ram. 1 m/s is the shipped gate the offence tracker ran
// inline before it called this kernel: a deliberate parking-speed bump into a
// cruiser counts, a car settling against a bumper does not.
constexpr float POLICE_RAM_MIN_CLOSING_MPS = 1.0f;

struct PoliceRamVerdict {
    bool  player_rammed = false;  // attributed to the player -> raise wanted
    float closing_speed = 0.f;    // total approach speed along the normal (m/s)
};

// =============================================================================
// PCG-245 — police roadblocks (pure kernels; production wiring in traffic.cpp /
// traffic_spawn.cpp / traffic_drive.cpp).
// =============================================================================

// A selected roadblock site on the lane graph. `lane` is an opaque lane
// reference into whichever graph chose the site — spelled std::uint32_t so
// this header owes that graph nothing; `station` is the arc-length along it.
// `center` is the
// ROAD-centreline point of the block (lane centre shifted by -mid_offset —
// derived read-only from the authored geometry, never snapped/written:
// PCG-170), `fwd` the unit XZ road tangent there (= the player's travel
// direction through the site), `width` the carriageway width (4*mid_offset).
struct RoadblockSite {
    bool          found   = false;
    std::uint32_t lane    = 0xFFFFFFFFu;   // kInvalidLane
    float         station = 0.f;
    glm::vec3     center{0.f};
    glm::vec2     fwd{0.f};
    float         width   = 0.f;
};

// NOT LIFTED (PENG-29): roadblock_select_site(). Choosing WHERE to put a block
// means walking the lane graph — loaded lanes, junction arity, arc-length
// projection along a directed lane — and that class did not come across.
// RoadblockSite stays because it is the plain-data handoff: whatever picks a
// site fills one of these, and every function below works from it alone.

// Block composition: cruiser + officer poses for a site. 2 cruisers (3 when
// width >= wide_width_m) angled car_angle_deg off the road axis with
// ALTERNATING sign (a chevron), spread evenly across the full carriageway
// width, staggered fore/aft by car_stagger_m so the angled boxes interlock
// without overlapping. Officers (n_cars - 1, max 2) stand DOWNSTREAM of the
// car line — behind the block relative to the player's approach — spread
// across the road. `facing` is a unit XZ heading (the caller owns the yaw
// convention); `car_half_len` is the cruiser's half-length (real model AABB in
// production, so the fan matches the actual body). Degenerate fwd/width
// returns n_cars == 0.
struct RoadblockCarPose {
    glm::vec2 pos{0.f};
    glm::vec2 facing{1.f, 0.f};
};
struct RoadblockLayout {
    int n_cars = 0;
    std::array<RoadblockCarPose, 3> cars{};
    int n_officers = 0;
    std::array<glm::vec2, 2> officers{};
};
RoadblockLayout roadblock_layout(glm::vec2 center, glm::vec2 fwd, float width,
                                 float car_half_len, const RoadblockTuning& t);

// Breakthrough predicate: true when the player has PASSED the block — at least
// breach_pass_m downstream of its centre, within the carriageway plus
// breach_side_slack_m of lateral slack (blowing past on the shoulder counts;
// a parallel street a block over does not — that path goes stale instead).
// The other breach trigger (slamming a block cruiser) is latched by the
// collision resolver, not here.
bool roadblock_breached(glm::vec2 player_xz, glm::vec2 center, glm::vec2 fwd,
                        float width, const RoadblockTuning& t);

// Dispatcher trigger: ticks the cooldown and decides whether to ATTEMPT a
// stage this frame. Gates: no block already active (max 1), wanted >=
// min_wanted, cooldown elapsed, and the minimum 2-cruiser block fits the
// police budget (police_count + 2 <= max_police) — a roadblock thins the
// chase pack (the caller counts block cruisers in police_count), it never
// exceeds POLICE_MAX_CARS. The caller resets the cooldown on a successful
// stage (cooldown_s) or a failed attempt (retry_s).
struct RoadblockDispatchStep {
    bool  try_stage = false;
    float cooldown  = 0.f;   // ticked-down value to store back
};
RoadblockDispatchStep roadblock_dispatch_step(bool active, float cooldown,
                                              int wanted, int police_count,
                                              int max_police, float dt,
                                              const RoadblockTuning& t);

// `n_toward_player` is the unit contact normal pointing from the police car
// toward the player's car; v_* are the bodies' world-space velocities at the
// contact point (m/s), sampled BEFORE the collision impulse is applied.
inline PoliceRamVerdict police_ram_verdict(const glm::vec3& v_player,
                                           const glm::vec3& v_police,
                                           const glm::vec3& n_toward_player) {
    PoliceRamVerdict v;
    // Approach speed each body contributes INTO the other (positive = closing).
    const float player_into = -glm::dot(v_player, n_toward_player);
    const float police_into =  glm::dot(v_police, n_toward_player);
    v.closing_speed = player_into + police_into;
    v.player_rammed = v.closing_speed >= POLICE_RAM_MIN_CLOSING_MPS
                   && player_into > police_into;
    return v;
}

} // namespace apricot
