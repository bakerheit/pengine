#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <limits>
#include <vector>

#include <glm/glm.hpp>

#include "city/city_rng.h"    // keyed entropy: no stream ever reaches this file
#include "city/police_ai.h"   // PCG-011: PoliceTuning (embedded in TrafficTuning)

// Traffic AI — the decision kernels an AI car's drive loop asks before it moves.
//
// Everything here is a free function over a plain-data view struct: the drive
// loop scans the world, fills a view, and this decides. That is what makes the
// branchy parts testable with a table of inputs instead of a running city, and
// it is why this file could be lifted at all.
//
// PROVENANCE. Lifted from probablecause (PENG-29). PCG-nnn markers are ITS
// ticket numbers, kept because the comments they head explain why a constant is
// the value it is. See src/city/README.md for what was left behind.

namespace apricot {

// The four axis directions of the procedural road lattice. probablecause got
// this from its road graph header; that header is not in this tree and traffic
// needed exactly four enumerators out of it, so it lives here with its users
// (traffic_dir_info, LaneId, turn_kind). The VALUES are load-bearing: turn_kind
// classifies a turn by the difference of two of these modulo 4, so East=0,
// North=1, West=2, South=3 is a contract, not an ordering preference.
enum class GridDir : int { East = 0, North = 1, West = 2, South = 3 };

struct TrafficDirInfo {
    int       di = 1;
    int       dj = 0;
    glm::vec3 unit{1.f, 0.f, 0.f};
    glm::vec3 right{0.f, 0.f, -1.f};
    float     yaw_deg = 270.f;
};

TrafficDirInfo traffic_dir_info(GridDir dir);

enum class LaneChangeIntent : std::uint8_t {
    None,
    AroundBlocker,
    ReturnToLane,
};

enum class TrafficAgentState : std::uint8_t {
    Cruise,
    FollowLeader,
    ApproachSignal,
    Queued,
    YieldIntersection,
    TraverseIntersection,
    LaneChange,
    AvoidObstacle,
    BlockedRecovery,
    Panic,
    PhysicsFallback,
    YieldEmergency,   // PCG-244: pulled over / edging right for a responder
};

enum class DriverProfileKind : std::uint8_t {
    Cautious,
    Normal,
    Impatient,
    AggressiveLite,
};

struct DriverProfile {
    DriverProfileKind kind = DriverProfileKind::Normal;
    float speed_mul        = 1.f;
    float headway          = 1.35f;
    float min_gap          = 2.8f;
    float accel            = 4.5f;
    float brake            = 8.0f;
    float patience_seconds = 5.0f;
    float safe_lane_gap    = 28.f;
    float honk_after       = 3.0f;
    float yellow_bias      = 0.5f; // 0 = stop early, 1 = likely to continue.
};

DriverProfile make_driver_profile(DriverProfileKind kind);

// WHO IS DRIVING THIS CAR — and the one signature this lift deliberately
// changed.
//
// probablecause spelled it `random_driver_profile(std::mt19937&)`: the spawner
// held a stream and pulled a driver off it. The mix is unchanged (18% Cautious,
// 60% Normal, 16% Impatient, 6% AggressiveLite) but the ENTROPY IS NOT, because
// a stream answers differently depending on how many cars were spawned before
// this one — and in a streamed city that count is a function of which way the
// player drove in. Two players arriving at the same junction from opposite
// directions would meet different drivers, and a replay would meet a third set.
//
// So the roll is keyed to the car's spawn identity instead. `cell_x`/`cell_z`
// are the spawn cell and `slot` distinguishes several cars spawned into the
// same cell (a lane ordinal, an index in the spawn batch — anything the caller
// can reproduce). Same car, same driver, forever, regardless of approach order.
//
// driver_profile_from_roll() is the bucketing on its own, taking a [0,1) roll,
// so the mix can be pinned without going through a hash — and so nobody is
// tempted to inline the thresholds at a call site.
DriverProfile driver_profile_from_roll(float roll);
DriverProfile driver_profile_for(uint64_t seed, int32_t cell_x, int32_t cell_z,
                                 uint32_t slot);

float traffic_follow_speed_for_gap(float gap, const DriverProfile& profile);
bool traffic_should_stop_for_yellow(float distance_to_stop, float speed,
                                    const DriverProfile& profile);
bool traffic_profile_may_pass_jam(const DriverProfile& profile,
                                  float blocked_seconds);

// PCG-009 / PCG-165: the deep-jam despawn gate. The PCG-009 BASE conditions
// are unchanged and always required: the car is (a) `blocked` with no forward
// progress, (b) past the `stuck_despawn_seconds` ceiling, AND (c) wedged
// behind a NON-AI permanent obstacle (`leader_is_dynamic` — a parked wreck /
// the player / police), not another AI car. The non-AI-blocker gate is the
// load-bearing condition: in a signal queue the blocker is ALWAYS a stopped
// AI leader, so a follower legitimately queued at a red — whose `blocked`
// timer can ratchet across red phases — can never be despawned out of the
// queue, in EITHER mode below.
//
// PCG-165 reengineers the despawn into a LAST RESORT on top of that base:
//   legacy_instant == true  — the shipped PCG-009 behaviour: base conditions
//     alone flag the car (the F1 A/B toggle,
//     RecoveryTuning::legacy_instant_despawn — kept so the founder can
//     compare old/new in one session).
//   legacy_instant == false (the new default) — the base must ALSO hold:
//     * maneuvers_exhausted — the recovery ladder ran dry (recovery_plan
//       returned GiveUp: every eligible Nudge/Reverse/3PT attempt has been
//       made within its bounded budget); a car that can still TRY something
//       drives out instead of despawning; and
//     * far from the player — dist_from_player >= min_player_dist, with a
//       NEGATIVE dist (no player reference, the PCG-157 sentinel) counting
//       as far. A car never blinks out in front of the founder; it waits for
//       the player to leave, exactly like the kernel's GiveUp gating.
// The despawn sweep + spawner top-up are unchanged; only the arming gets
// stricter. Pure + headless-testable.
bool traffic_should_despawn_jam(bool blocked, float blocked_seconds,
                                float stuck_despawn_seconds,
                                bool leader_is_dynamic,
                                bool legacy_instant,
                                bool maneuvers_exhausted,
                                float dist_from_player,
                                float min_player_dist);

// PCG-157: recovery/despawn telemetry — read-only instrumentation for the two
// PCG-009 despawn paths, so the maneuver-recovery work (#232) can quantify how
// often cars vanish and HOW CLOSE to the player they do it, before any
// behaviour changes. Never read by the simulation. Counted once per car at the
// moment the flag is armed:
//   jam_*     — the deep-jam gate (traffic_should_despawn_jam) fired in
//               ai_update_speed: wedged behind a non-AI blocker past the
//               stuck_despawn_seconds ceiling.
//   give_up_* — the recovery ceiling fired in try_ai_recover: a collision-
//               demoted Parked car never re-attached within recovery_give_up.
// The histograms bucket the car's planar distance from the player when the
// flag was armed: BUCKETS buckets of BUCKET_M metres, the LAST bucket also
// absorbing everything beyond the range and samples with NO player reference
// (dist < 0 — headless / menu). 8 x 25 m spans the 200 m AI despawn radius.
// Displayed + reset by the F1 Dev-Tweaks "Traffic AI" panel.
struct RecoveryTelemetry {
    static constexpr int   BUCKETS  = 8;
    static constexpr float BUCKET_M = 25.f;

    std::uint32_t jam_count     = 0;
    std::uint32_t give_up_count = 0;
    std::uint32_t jam_hist[BUCKETS]     = {};
    std::uint32_t give_up_hist[BUCKETS] = {};

    void record_jam(float dist_from_player);
    void record_give_up(float dist_from_player);
    void reset() { *this = RecoveryTelemetry{}; }
};

// Histogram bucket for a distance-from-player sample: floor(dist / bucket_m),
// clamped to [0, buckets-1]. A negative dist (no player reference) files under
// the last bucket alongside beyond-range samples. Pure + headless-testable.
int telemetry_dist_bucket(float dist, float bucket_m, int buckets);

// =============================================================================
// PCG-158: maneuver recovery planning (epic #232 — "drive out of the problem,
// don't despawn it"). The pure escalation kernel for a deeply-blocked car:
// given a snapshot of its predicament (RecoveryView, filled by the drive loop
// from CarGrid scans) and where it is in the recovery sequence, pick the next
// action. Decisions here, execution in the drive loop — same split as
// overtake_gap_acceptable / turner_should_yield. Unwired by this ticket;
// PCG-159 wires Nudge, PCG-161/162/163 the dynamic actions, PCG-164 Reroute,
// PCG-165 the GiveUp -> despawn gate.
// =============================================================================

// What is actually wedging the car. Classified by the drive loop's leader scan
// (leader_is_dynamic / player pointer) + the junction-deadlock work.
enum class RecoveryBlocker : std::uint8_t {
    None,               // not deep-blocked: no recovery applies
    StaticWreck,        // a parked / collision-dead vehicle (non-AI, non-player)
    Player,             // the player's (stopped) car
    AiJam,              // a stopped AI leader in a deep jam (never despawn-gated)
    OpposingTraverser,  // mid-box mutual standoff (two committed crossers wedged
                        //   nose-to-nose — the junction-deadlock cluster's
                        //   residual case; resolvable by Reverse, never Nudge)
};

// The escalation ladder, cheapest first. None = keep waiting (normal driving /
// no viable action near the player); GiveUp = the last resort that feeds the
// (PCG-165, reengineered) despawn path.
enum class RecoveryAction : std::uint8_t {
    None,
    Nudge,            // kinematic in-lane / shoulder edge-around (PCG-159)
    Reverse,          // dynamic reverse-to-clearance (PCG-161/162/163)
    ThreePointTurn,   // dynamic turn-around onto the opposing lane (PCG-163)
    Reroute,          // pick a different next lane and drive off (PCG-164)
    GiveUp,           // exhausted, far from the player -> clean despawn (PCG-165)
    // PCG-224: NOT a jam-ladder action (kRecoveryLadder/recovery_plan never
    // return it) — it is only ever an ai_maneuver_kind. A car KNOCKED off its
    // path by a ram enters the dynamic maneuver mode with this kind and steers
    // itself back onto its lane instead of teleport-snapping (the old "reset").
    DriveBack,
};

// One car's predicament, in plain scalars so the kernel stays pure. The drive
// loop fills it from the leader scan + CarGrid clearance probes.
struct RecoveryView {
    RecoveryBlocker blocker = RecoveryBlocker::None;
    float front_gap        = std::numeric_limits<float>::infinity(); // m to the blocker
    float rear_gap         = std::numeric_limits<float>::infinity(); // m clear behind
    // Widest free lateral corridor beside the blocker (m) that stays on OUR
    // half of the road + shoulder — the Nudge space. 0 == no shoulder at all.
    float shoulder_clear   = 0.f;
    // The full oncoming-lane overtake is available (PCG-008 gap acceptance
    // passed). When true the normal go-around handles it — recovery actions
    // are for when this is false.
    bool  oncoming_clear   = false;
    bool  road_bidirectional = true;   // a 3PT needs an opposing lane to land on
    bool  reroute_available  = false;  // an alternative next lane exists
    // Planar distance to the player (m); negative == no player reference
    // (same sentinel as the PCG-157 telemetry). Gates GiveUp: despawning is a
    // far-from-player-only last resort.
    float dist_from_player = -1.f;
};

// Net-new knobs (defaults ARE the baseline): per-action eligibility clearances
// and the BOUNDED per-action time budgets — the §3.5 guardrail that recovery
// can never become a car crawling/wiggling forever: every action ends, the
// ladder is finite, and its far-from-player terminal is GiveUp. Embedded in
// TrafficTuning for the F1 Dev-Tweaks path once wired.
struct RecoveryTuning {
    // Eligibility clearances.
    float nudge_min_shoulder = 2.3f;  // m of free corridor beside the blocker:
                                      //   car width (2 m) + the nudge pass
                                      //   margin (0.3 m) — matches
                                      //   nudge_pick_target's fit test exactly
    float reverse_min_rear   = 7.f;   // m clear behind to back into
    float turn_min_rear      = 5.f;   // m clear behind for a 3PT's reverse arc
    // Bounded budgets (s) — an action past its budget escalates.
    float nudge_budget_s     = 6.f;
    float reverse_budget_s   = 8.f;
    float turn_budget_s      = 14.f;
    float reroute_budget_s   = 6.f;
    // PCG-224 knock-off drive-back. driveback_budget_s bounds the whole
    // return-to-path maneuver (compose-3PT + home leg) — past it the car
    // exit-or-gives-up (clean despawn), the §3.5 never-crawl-forever guardrail.
    // driveback_max_offset is how far off its lane a settled car may be and
    // still be worth steering back; flung farther, it waits for the
    // recovery_give_up settle ceiling to despawn it.
    float driveback_budget_s  = 12.f;  // s the drive-back may run, then give up
    float driveback_max_offset = 18.f; // m off-lane still eligible to drive back
    // GiveUp is allowed only at/beyond this player distance (a car must never
    // blink out in front of the founder); a NEGATIVE dist (no player) counts
    // as far. Nearer than this, an exhausted car RETRIES the ladder instead —
    // conditions change as traffic moves — or holds if nothing is eligible.
    float giveup_min_player_dist = 60.f;
    // PCG-165 A/B toggle (F1): true == the shipped PCG-009 instant despawn
    // (timer + non-AI blocker only); false (the branch default) == the
    // last-resort gate — despawn additionally requires the recovery ladder
    // exhausted AND the car far from the player. Founder-directed, so old and
    // new can be compared in one session.
    bool legacy_instant_despawn = false;
};

// The escalation decision. `current` is the action the car is presently
// executing (None if just entering recovery) and `elapsed_in_action` how long
// it has run. Semantics, pinned by the PCG-158 decision-table tests:
//   - blocker == None -> None (not in recovery).
//   - A current action still ELIGIBLE and within BUDGET is kept (no thrash).
//   - Otherwise escalate: the first eligible action STRICTLY AFTER `current`
//     on the ladder (monotone within a cycle -> guaranteed to terminate).
//   - Ladder exhausted: GiveUp if far from the player (or no player);
//     otherwise retry from the top (first eligible overall) or None if
//     nothing is eligible (boxed in: hold — today's behaviour).
// Eligibility:
//   Nudge  — blocker is a STATIC obstacle (StaticWreck / Player) with
//            shoulder_clear >= nudge_min_shoulder. Never for AiJam (that is a
//            legitimate queue — jam-passing already handles impatient passes)
//            and never for OpposingTraverser (mid-box: no shoulder exists).
//   Reverse— rear_gap >= reverse_min_rear (the OpposingTraverser resolution).
//   3PT    — road_bidirectional && rear_gap >= turn_min_rear.
//   Reroute— reroute_available.
// Pure + headless-testable.
RecoveryAction recovery_plan(const RecoveryView& v, RecoveryAction current,
                             float elapsed_in_action, const RecoveryTuning& t);

// PCG-159: the Nudge side-pick — where to edge past a static blocker while
// staying on OUR half of the road. All laterals are in the car's lane frame
// (LaneGraph::project_onto sign convention: + toward the kerb, − toward the
// road centreline). The usable band for the car's CENTRE is
//   [−mid_offset, mid_offset − my_half]
// — kerb side bounded so the body stays on the pavement (the kerb line sits
// mid_offset outboard of the lane centre: road half-width 2·mid off the road
// centreline), centreline side bounded AT the road centreline (never into the
// oncoming corridor; the oncoming lane centre is another 2·mid_offset away,
// so a centreline pass keeps > a car width to oncoming traffic). A side is
// viable when the free corridor between the blocker's edge and that bound
// fits car width + margin; the returned target hugs the blocker at the
// margin (minimal deviation) and the returned corridor feeds
// RecoveryView::shoulder_clear (nudge_min_shoulder is calibrated to the same
// fit test). Picks the viable side with the smaller |target|. Pure +
// headless-testable; the drive loop feeds the blocker's projected lateral
// and verifies the corridor with the ai_overtake_lane_clear band scan before
// committing.
struct NudgePlan {
    bool  viable   = false;
    float target   = 0.f;   // lateral offset to pass at (this lane's frame)
    float corridor = 0.f;   // free corridor width on the chosen side (m)
};
NudgePlan nudge_pick_target(float blocker_lat, float blocker_half,
                            float my_half, float margin, float mid_offset);

// =============================================================================
// PCG-161: the precision low-speed maneuver controller (epic #232). Pure input
// kernels — maneuver geometry in, throttle/brake/steer out — for the two
// DYNAMIC recovery actions (Reverse, ThreePointTurn). Shaped like
// police_terminal_pursuit_cmd, but where the police cmd is coarse bang-bang
// (constant -0.65 reverse, no stop condition), these are precision
// controllers: speed is GOVERNED to a crawl (raw reverse authority is
// ~8.7 m/s^2 on the sedan — PCG-160 spike), and every leg has a stop target.
// The drive loop fills the views from the real body state and the broadphase
// scans, and executes by handing the commands to the vehicle.
//
// STEERING SIGN CONVENTION, AND ITS STATUS IN THIS TREE. steer > 0 turns the
// nose RIGHT moving forward, and therefore swings the nose LEFT while
// reversing — yaw rate has the sign of (-v_signed * steer), with yaw positive
// counterclockwise seen from above. heading_err_deg below is
// normalize_deg(target_yaw - current_yaw): positive err means the target
// heading is counterclockwise of us.
//
// In probablecause that convention was VERIFIED, by a closed-loop suite that
// drove a real vehicle substep against these kernels on flat ground. That suite
// did NOT come across with PENG-29, because apricot's step_vehicle() does not
// steer or accelerate yet — throttle, brake and handbrake are read and do
// nothing. So here the convention is a CONTRACT the vehicle must satisfy, not a
// measured fact, and the kernels below are pinned only on their own arithmetic.
// The closed loop goes back in the moment there is a car that drives.
// =============================================================================

// One frame of maneuver inputs for Vehicle::set_inputs. Same shape as
// PursuitCmd (police_ai.h); duplicated deliberately so the pure traffic
// kernel does not include the police header.
struct ManeuverCmd {
    float throttle  = 0.f;   // signed: +1 forward, -1 reverse
    float brake     = 0.f;
    float steer     = 0.f;   // [-1, 1]
    bool  handbrake = false; // never used by the maneuvers; kept for shape
};

// Net-new knobs (defaults ARE the baseline), embedded in TrafficTuning for
// the F1 Dev-Tweaks path once PCG-163 wires the panel.
struct ManeuverTuning {
    // Longitudinal governor.
    float crawl_speed     = 3.0f;   // m/s cap either direction (PCG-160 value)
    float approach_gain   = 0.8f;   // 1/s: |v_target| = min(crawl, gain * dist)
    float stop_tol        = 0.30f;  // m: within this of the stop target == there
    float stop_speed      = 0.15f;  // m/s: below this == stopped
    float throttle_per_ms = 0.5f;   // throttle per m/s of speed shortfall
    float brake_per_ms    = 0.35f;  // brake per m/s of overshoot
    // Three-point turn geometry.
    float turn_switch_deg  = 100.f; // reverse arc ends once |err| swings under
    float done_heading_deg = 20.f;  // forward arc ends aligned within this
    float leg_travel_max   = 6.0f;  // m: cap on either arc leg's travel
    float leg_stop_margin  = 0.8f;  // m: brake the leg when clearance runs out
    // Clearance margins (maneuver_path_clear). Pedestrians get a much larger
    // margin than cars: a person behind the car hard-blocks the maneuver.
    float car_clear_margin = 0.5f;  // m beyond the needed path length
    float ped_clear_margin = 2.0f;  // m — peds are NOT in the CarGrid; the
                                    //   drive loop MUST fill ped_gap from its
                                    //   people_/on-foot-player list (PCG-134/143)
    // Exit / re-attach acceptance (maneuver_try_exit; promoted from constexpr
    // in PCG-163 so the founder can tune the re-attach feel live). A maneuver
    // ends by attaching to a loaded lane the car is ON and FACING; these gate
    // how much residual yaw snap and lateral offset the attach may absorb —
    // tighter reads cleaner but aborts more maneuvers to the crash-recovery
    // flow, looser exits more but can visibly snap.
    float exit_heading_tol_deg = 25.f;  // max yaw snap at re-attach
    float exit_max_lat         = 3.5f;  // m off the landing lane's centreline
    // PCG-224 drive-back (knock-off recovery). The return-to-path home leg
    // aims at a look-ahead point ON the assigned lane and steers toward it at
    // the governed crawl; when the car is knocked facing away (the aim would
    // be behind it) the drive loop composes a ThreePointTurn first.
    float driveback_lookahead     = 6.0f;  // m ahead on the lane for the aim point
    float driveback_turn_cone_deg = 90.f;  // |bearing-to-aim| beyond this -> 3PT
    float driveback_steer_full_deg = 35.f; // bearing err at which home steer saturates
};

// Longitudinal governor: drive the SIGNED speed (dot of velocity on forward,
// + = forward) toward v_target. Same-direction shortfall -> proportional
// signed throttle; overshoot / wrong direction -> proportional brake (the
// Vehicle brake opposes current motion, so it is direction-agnostic);
// v_target ~ 0 -> stand on the brake. Shared by both maneuvers.
ManeuverCmd maneuver_speed_cmd(float v_signed, float v_target,
                               const ManeuverTuning& t);

// Precision reverse-to-clearance: back up `dist_to_go` more metres (straight
// at steer = 0, or arcing at a caller-chosen steer), decelerating on approach
// and stopping inside stop_tol. `done` == arrived AND stopped.
struct ReverseStep {
    ManeuverCmd cmd;
    bool        done = false;
};
ReverseStep maneuver_reverse_cmd(float dist_to_go, float v_signed, float steer,
                                 const ManeuverTuning& t);

// Three-point turn: full-lock reverse arc -> brake -> opposite-lock forward
// arc -> brake -> done. The kernel owns phase progression; the drive loop
// owns the geometry it is fed (target heading = the opposing lane direction,
// leg travel accumulated from the body pose, live clearance along the current
// travel direction with cars AND pedestrians already min'd in).
enum class TurnPhase : std::uint8_t {
    ReverseArc,
    BrakeAfterReverse,
    ForwardArc,
    BrakeAfterForward,
    Done,
};
struct ThreePointView {
    TurnPhase phase = TurnPhase::ReverseArc;
    float heading_err_deg = 0.f; // normalize_deg(target - current); + == CCW
    float leg_travel = 0.f;      // m travelled in the CURRENT leg
    float leg_clear  = std::numeric_limits<float>::infinity(); // m of free path
                                 //   along the current travel direction
    float v_signed   = 0.f;      // m/s, + = forward
};
struct ThreePointStep {
    ManeuverCmd cmd;
    TurnPhase   next = TurnPhase::ReverseArc;
};
ThreePointStep maneuver_three_point_cmd(const ThreePointView& v,
                                        const ManeuverTuning& t);

// PCG-224: the return-to-path home leg for a knocked-off car. Given the
// bearing to a look-ahead point ON the assigned lane, steer the nose onto it
// at the governed crawl (the longitudinal governor is maneuver_speed_cmd, so
// the drive-back never lunges). Forward-ONLY: the drive loop composes a
// ThreePointTurn first when the car is knocked facing away (|bearing| past
// driveback_turn_cone_deg), so this kernel only ever sees a reachable aim.
// The crawl target eases down as the nose swings off the aim so the car curves
// onto the lane instead of overshooting it.
//
// Steering sign — the same CONTRACT as the 3PT block above, and unverified here
// for the same reason: moving forward, steer > 0 turns the nose RIGHT (CW), so
// an aim CCW of us (bearing_err_deg > 0) needs steer < 0, hence
// steer = -bearing_err / full.
struct DriveBackView {
    float bearing_err_deg = 0.f;  // normalize_deg(aim_bearing - yaw); + == aim is CCW
    float v_signed        = 0.f;  // m/s, + = forward
};
ManeuverCmd drive_back_cmd(const DriveBackView& v, const ManeuverTuning& t);

// Ped-aware path clearance for a maneuver needing `need_m` metres of travel.
// Cars come from the CarGrid scan; PEDESTRIANS ARE NOT IN THE CARGRID — the
// drive loop must fill ped_gap from its per-frame people_/on-foot-player
// hazard list, or a reversing car can back over a person the broadphase
// simply cannot see (PR #235 review catch). Pedestrians block at a larger
// margin than cars on purpose.
struct ManeuverClearance {
    float car_gap = std::numeric_limits<float>::infinity(); // m to first car
    float ped_gap = std::numeric_limits<float>::infinity(); // m to first ped
};
bool maneuver_path_clear(const ManeuverClearance& c, float need_m,
                         const ManeuverTuning& t);

// Distance along a straight probe corridor (start, unit dir, half_width wide,
// max_len long — all planar XZ) at which a disc obstacle (p, radius) first
// intrudes, or +inf if it never does. Conservative rectangle-vs-disc: used by
// the drive loop to fill BOTH ManeuverClearance gaps (cars use their
// half-diagonal as the radius, pedestrians their body radius). Pure geometry.
float maneuver_corridor_gap(glm::vec2 start, glm::vec2 dir, float half_width,
                            float max_len, glm::vec2 p, float radius);

// PCG-009: the effective IDM follow-gap floor. A driver's personality `min_gap`
// is the bumper gap it WANTS to keep at a standstill, but the four profiles
// settle below a car length (1.8-3.8 m vs. the 4 m collision length), so a
// follower closing on a dynamic / parked leader overlaps its OBB and the SAT
// resolver demotes it to PhysicsFallback. This raises the profile's min_gap to
// at least `floor` (TrafficTuning::min_follow_gap, which is >= the car length)
// so steady-state following can never settle the OBBs into an overlap, while a
// driver who personally wants MORE than the floor keeps its larger gap. Pure +
// headless-testable; pins the min_gap >= car_length invariant.
float effective_min_gap(const DriverProfile& profile, float floor);

// PCG-167: permissive-left yield — the decision kernel. Nothing in the shipped
// negotiation modelled "a turner yields to oncoming": at signal junctions
// ai_yield_to_cross_traffic bailed early (lights phase-separate CROSS traffic,
// but a street's two OPPOSITE approaches are deliberately the same phase group
// — the approach_group_a axis fold — so opposing left-turners get green
// together); at stop/uncontrolled junctions clause (a) skips antiparallel
// headings and clause (b) only conflicts across groups. Result: two opposing
// left-turners both commit and wedge nose-to-nose mid-box — and being
// AI-blocked-by-AI they never despawn (the PCG-155 research §0 nuance).
//
// This kernel judges ONE opposing candidate for ME — a pre-commit turner whose
// planned link crosses the oncoming side (the producer, ai_turner_must_yield,
// establishes that + fills the view). I hold at the line if ANY candidate says
// yield:
//   (ii)  o committed (past its stop line / mid-turn on an opposite approach):
//         ALWAYS yield — never drive into a car already in the box. My own gate
//         disengages once I pass MY line (the caller runs it pre-commit only),
//         so committed-vs-waiting pairs resolve one way and can't mutually hold.
//   (iii) o is ALSO a pre-commit crossing turner (the opposing-lefts case):
//         exactly one proceeds — the one closer to the box; a near-tie (0.5 m,
//         same epsilon as clause (b)) falls to `tie_winner`, which the caller
//         derives from a strict antisymmetric order (lower LaneRef wins), so
//         for any pair exactly one yields: deadlock-free by construction.
//   (i)   o is oncoming straight-through (or a non-crossing right-turner):
//         it has right of way while it is actually ROLLING toward the box
//         (speed > stuck_speed) within the acceptance window. An essentially
//         stopped oncoming car is NOT entering the box and never holds — that
//         is also the anti-starvation half: a stationary oncoming queue only
//         gates via its front car as it moves off. `impatient` (the caller
//         feeds traffic_profile_may_pass_jam over the accumulated yield wait,
//         mirroring the jam-pass escalation) shrinks the window from the full
//         lookahead to `hold_zone` (~ the stop-back + conflict radius), so a
//         long-waiting Normal/Impatient/AggressiveLite turner accepts tighter —
//         but still real — gaps; Cautious never escalates and waits for a full
//         gap, exactly like it never passes a jam.
// Candidates beyond `lookahead` of the junction are out of negotiation range.
// Pure + headless-testable (cf. traffic_should_despawn_jam).
struct TurnYieldCandidate {
    bool  opposing   = false;  // on the OPPOSITE approach of my junction
    bool  crossing   = false;  // its own planned link also crosses oncoming
    bool  committed  = false;  // already past its stop line / mid-turn
    bool  tie_winner = false;  // stable near-tie pick; must be antisymmetric
                               //   between any two cars (caller: lower LaneRef)
    float to_junction = std::numeric_limits<float>::infinity(); // m short of the box
    float speed       = 0.f;   // m/s along its approach
};

bool turner_should_yield(const TurnYieldCandidate& o, float my_to_jn,
                         float lookahead, float hold_zone, float stuck_speed,
                         bool impatient);

// Player-reactivity knobs (PCG-006). How an AI car treats the human-driven car
// as a PRIORITISED off-corridor hazard (over and above the in-lane leader scan):
// the proximity / lateral gate for "the player is in my path", and the
// predicted-collision window for "the player is closing on a collision course".
// Runtime-tunable so the F1 Dev-Tweaks panel can iterate on how twitchy / chill
// traffic is around the player without a recompile. Embedded in TrafficTuning.
struct PlayerHazardTuning {
    float range       = 18.f;   // m: ignore the player entirely beyond this distance
    float half_width  = 2.6f;   // m: lateral gate for "player ahead in my path"
                                //    (just past the 2 m lane corridor — wide enough
                                //    to catch a player cutting in, tight enough to
                                //    ignore normal oncoming traffic ~4 m abreast)
    float ttc_horizon = 2.0f;   // s: predicted-collision look-ahead for a crosser
    float collision_r = 2.6f;   // m: predicted closest-approach under this == a hit
    float honk_gap    = 6.0f;   // m: honk AT the player when the hazard gap drops here
};

// PCG-143: sharp emergency braking for a PERSON (pedestrian or on-foot player)
// in the car's path. The default hazard response is the smooth IDM leader term,
// which eases the car down from far out — correct for cars, but for a person
// stepping into the road the founder wants a QUICK, urgent stop (with a visual
// recoil). These shape that: brake late and hard instead of gliding.
struct PersonBrakeTuning {
    float standoff     = 2.0f;   // m: aim to stop the nose this far short
    float max_decel    = 10.0f;  // m/s^2: hardest emergency stop. Deliberately NOT
                                 //   enough to always stop from the short reaction
                                 //   window below — a person who steps in front of a
                                 //   moving car can be too late, and the car keeps
                                 //   rolling -> the knockdown/impact path fires
    // Reaction window: the car holds speed until the person is THIS close, then
    // slams the brakes. Short and speed-scaled so it's a last-moment reaction (the
    // founder wants the danger of walking out in front), not an anticipatory yield:
    //   react = react_base + react_per_mps * v
    //   ~2 m stopped, ~5 m at the 12 m/s cruise, more above. At cruise the stopping
    //   distance (~7 m) exceeds this, so a moving car genuinely can't always stop.
    float react_base    = 2.0f;  // m: reaction distance at a standstill (the minimum)
    float react_per_mps = 0.25f; // m of reaction distance per m/s of car speed
};

// Sharp emergency-brake deceleration (<= 0) for a person `gap` m ahead at speed
// `v`. Cruises (returns 0) until the person enters the short, speed-scaled
// reaction window, then commands the decel to stop `standoff` short, capped at
// `max_decel`. Because the window is intentionally shorter than the stopping
// distance at speed, a moving car often CAN'T stop in time (the founder wants the
// risk of stepping out). Folds into the IDM brake via std::min. Headless-tested.
inline float person_brake_accel(float v, float gap, const PersonBrakeTuning& t) {
    const float react = t.react_base + t.react_per_mps * v;
    if (gap > react) return 0.f;                    // person not close enough yet: cruise
    const float g = std::max(gap - t.standoff, 0.05f);
    return -std::min((v * v) / (2.f * g), t.max_decel);
}

// PCG-143: visual nose-dive ("recoil bounce") for a hard-braking KINEMATIC AI
// car. The physics-driven player car already pitches via its real suspension;
// AI cars are pose-stamped and bypass it, so this fakes the weight transfer.
struct RecoilTuning {
    float dive_threshold = 2.5f;   // m/s^2: decel below this produces no visible dive
    float dive_per_decel = 0.010f; // rad of nose-dive per m/s^2 of decel over threshold
    float max_dive       = 0.10f;  // rad (~5.7 deg): cap so it stays "a little" dip
    float stiffness      = 140.f;  // spring k (1/s^2) — higher returns to level faster
    float damping        = 12.f;   // spring c (1/s); < 2*sqrt(k) (~23.7) == underdamped
                                   //   so the return overshoots level: the BOUNCE
};

// Advance the recoil pitch spring one step. `pitch` (rad, + == nose down) and
// `pitch_vel` are the car's mutated state; `decel` is this frame's deceleration
// (m/s^2, >= 0). The spring chases a dive proportional to decel above the
// threshold; when the car stops (decel -> 0) the underdamped return overshoots
// past level — the bounce. Pure — headless-tested.
inline void recoil_step(float& pitch, float& pitch_vel, float decel,
                        float dt, const RecoilTuning& t) {
    const float target = std::min(t.max_dive,
        std::max(0.f, (decel - t.dive_threshold) * t.dive_per_decel));
    const float acc = t.stiffness * (target - pitch) - t.damping * pitch_vel;
    pitch_vel += acc * dt;
    pitch     += pitch_vel * dt;
}

// Go-around / overtaking knobs (PCG-008). A blocked, impatient car routes around
// the obstacle by pulling into the only "adjacent lane" a one-lane-per-direction
// road offers — the ONCOMING lane — so a go-around is a real-world two-lane
// overtake (pull out, pass, tuck back). These gate WHEN it triggers and WHETHER
// the target lane is clear enough. Runtime-tunable (embedded in TrafficTuning →
// F1 Dev-Tweaks) so the founder can dial impatience / caution without a recompile.
struct OvertakeTuning {
    // Hard clearances required in the target lane before pulling out.
    float min_front_gap = 16.f;   // m: clear distance required ahead in the target lane
    float min_rear_gap  = 8.f;    // m: clear distance required behind (don't cut anyone off)
    // Time-to-collision floor (MOBIL safety criterion): a car CLOSING on the gap —
    // a head-on oncoming car ahead, or a faster car behind already in the target
    // lane — must be at least this many seconds away. The dominant safety term for
    // an oncoming overtake, where the front gap closes at the SUM of both speeds.
    float safe_ttc      = 4.0f;   // s
    // Scales the driver's per-profile patience before a go-around triggers
    // (>1 = more patient / fewer go-arounds, <1 = quicker to pull out). The
    // per-AI impatience variation still comes from DriverProfile::patience_seconds;
    // this is the single global dial over all of them.
    float impatience_scale = 1.f;

    // PCG-038: steered-arc EXECUTION of the go-around (the decision + gap
    // acceptance above are unchanged). The chosen lateral move is coupled to
    // forward travel and bounded by the steering lock, so the car ARCS rather
    // than slides and faces its true direction of travel. See the go_around_*
    // helpers. Appended (OvertakeTuning is only ever default-constructed, so no
    // positional aggregate-init breakage).
    float max_steer_deg   = 38.f; // steering lock; caps lateral rate at speed*tan(this)
    float shift_seconds   = 1.6f; // nominal time to sweep a full lane shift, once rolling
    float min_shift_speed = 1.5f; // m/s; a car must be rolling this fast before it pulls out
};

// PCG-038: go-around steered-arc kinematics. PCG-008 executed the chosen lateral
// move by translating the body sideways (ai_lateral_offset ramped on a wall-clock
// timer, independent of forward speed) and facing it down-lane with a cosmetic
// lean clamped to 30 deg — so a near-stopped car slid sideways and crabbed. These
// pure helpers make the lateral move OBEY car motion: coupled to forward travel
// (no forward roll -> no sideways displacement; a stopped car cannot shift),
// bounded by the steering lock (so the heading deviation never exceeds it — no
// artificial clamp), eased over the shift on an S-curve, and the body facing its
// real direction of travel. Pure + headless-testable (cf. overtake_gap_acceptable).

// Max lateral (crabbing) rate a car can achieve rolling forward at `speed` with
// the wheels at the steering lock: speed * tan(lock). Zero at zero speed — the
// no-slide invariant — and grows with speed. `speed` is clamped non-negative.
float go_around_max_lateral_rate(float speed, float max_steer_rad);

// Ease-in/ease-out S-curve (smoothstep) position of a lateral shift as a function
// of forward progress `phase`: 0 -> `from`, 1 -> `target`, monotone between with
// zero slope at both ends (reads as two smooth steering inputs). Clamps `phase`.
float go_around_shift_offset(float from, float target, float phase);

// Advance a lateral shift one frame, coupled to forward travel and bounded by the
// steering lock. `offset` is the live lateral offset, `phase` the live forward
// progress [0,1]; `from`/`target` are the shift endpoints. Returns the new offset
// and advances `phase` by reference. Invariants: speed < min_shift_speed (incl.
// zero) -> NO change (the car must accelerate first; a stopped car cannot slide);
// otherwise `phase` advances dt/shift_seconds and the per-frame lateral step is
// capped at go_around_max_lateral_rate(speed, lock)*dt, so the offset arcs out
// within the steering lock (at cruise the cap rarely binds and the offset tracks
// the S-curve; at low speed it throttles the arc so the car can't out-steer).
float go_around_advance_offset(float offset, float& phase, float from, float target,
                               float speed, float dt, float max_steer_rad,
                               float shift_seconds, float min_shift_speed);

// World-XZ direction of ACTUAL travel: forward (lane tangent) * speed + right
// (lane normal) * lateral_rate, normalised. With lateral_rate 0 this is exactly
// the (normalised) forward tangent — the heading reduces to plain lane-following
// (the no-regression case). A non-zero lateral_rate rotates it toward the motion
// by atan(lateral_rate/speed), which stays within the steering lock when the rate
// is bounded — the body faces its arc with no crab and no clamp. Falls back to
// the forward tangent if the combined vector is degenerate.
glm::vec2 go_around_motion_dir(glm::vec2 fwd, glm::vec2 right, float speed,
                               float lateral_rate);

// PCG-008: the go-around gap-acceptance view + decision. Pure 1D kinematics in the
// target (oncoming) lane's frame: the nearest car ahead and the nearest car behind
// in that lane, each with the rate its bumper gap to us is closing. Production
// (traffic_drive.cpp) fills this in with a CarGrid band scan (reusing the existing
// broadphase); this function only judges accept/reject, so it is headless-testable
// like assess_player_hazard / traffic_should_despawn_jam.
struct OvertakeLaneView {
    float front_gap     = std::numeric_limits<float>::infinity(); // m bumper gap ahead
    float front_closing = 0.f;  // m/s the front gap shrinks (>0 == closing; oncoming = sum of speeds)
    float rear_gap      = std::numeric_limits<float>::infinity(); // m bumper gap behind
    float rear_closing  = 0.f;  // m/s a car behind closes on us (>0 == closing)
};

// True only when BOTH ends of the target lane leave a safe margin: enough hard
// clearance (the front clearance must also cover the whole pass, `pass_length`)
// AND, for any car closing on the gap, a time-to-collision above safe_ttc. An
// empty target lane (+inf gaps) always accepts — that's the single-close-blocker
// case the PCG-004 finding flagged as previously unreachable. A close head-on
// oncoming car (tiny TTC) or a faster car about to be cut off behind always
// rejects: the move never forces a vehicle (us or them) into an unsafe
// deceleration. Pure + headless-testable. `pass_length` is how far we must run in
// the target lane to clear the blocker and tuck back.
bool overtake_gap_acceptable(const OvertakeLaneView& view, float pass_length,
                             const OvertakeTuning& t);

// Startle-&-flee knobs (PCG-007). When the player drives recklessly AT an AI car
// — bearing down fast within a short gap (a near-miss / clip) — the car panics:
// a brief STARTLE (hard brake / jolt) escalating to a FLEE (speed up to escape +
// a bounded curb-side pull-aside) for `duration`, then a deterministic decay back
// to normal. These gate the trigger and shape the reaction. Runtime-tunable
// (embedded in TrafficTuning → F1 Dev-Tweaks) so the founder can dial how twitchy
// / dramatic the panic is without a recompile. Net-new this ticket, so these
// defaults ARE the baseline (no shipped constexpr to reproduce).
struct PanicTuning {
    // Trigger gate. A panic fires only when the player is BOTH already close
    // (binding hazard bumper gap at/under trigger_gap) AND closing fast
    // (mutual approach speed at/over trigger_closing). The high closing floor is
    // what keeps a normal, anticipated approach to a stopped player — which
    // PCG-006 already brakes for smoothly — from tripping a panic: by the time
    // the gap is short the AI has bled its speed, so closing is well under this.
    float trigger_gap     = 7.0f;   // m: hazard bumper gap under which a panic is possible
    float trigger_closing = 9.0f;   // m/s: mutual closing speed over which it's "reckless"

    // Lifecycle. Once tripped the panic lasts `duration`; the first
    // `startle_frac` of it is the startle (brake/jolt), the remainder the flee.
    // The timer always decays (see panic_tick) so a car can never get stuck.
    float duration        = 2.5f;   // s: total panic length
    float startle_frac    = 0.35f;  // first this fraction is startle, rest is flee

    // Flee shaping (kinematic lane-follower: speed + bounded lateral, never a
    // free-steer). flee_speed_mul scales the free-flow target UP so the car bolts
    // away; flee_offset is a bounded curb-side (away-from-oncoming) pull-aside —
    // kept WELL under a full lane (2 m) so the dart stays in-lane and never feeds
    // the lateral-offset→low-speed→blocked cascade PCG-006/009 warned about.
    float flee_speed_mul  = 1.6f;   // x: free-flow speed multiplier while fleeing
    float flee_offset     = 1.2f;   // m: curb-side lateral pull-aside while fleeing
};

// =============================================================================
// PCG-244: yield to emergency vehicles. The pure decision kernel for the most
// iconic open-world traffic behaviour — civilian cars parting for a
// lights-and-sirens responder (a Driver::Police pursuer or the tasked
// Driver::Ambulance). Same split as assess_player_hazard / turner_should_yield:
// TrafficSystem snapshots the few active responders once per frame (their REAL
// dynamic-body position/velocity — kinematic AI cars would feed zero velocity,
// the PCG-132 trap; responders are dynamic so theirs is real), the drive loop
// feeds each civilian's pose in, and this classifies. Pure + headless-testable.
// =============================================================================

// What a civilian should do about one responder this frame.
enum class EmergencyYield : std::uint8_t {
    None,            // no responder constrains this car
    PullOverRight,   // same direction: pull to the kerb and brake to a stop
    SlowEdgeRight,   // oncoming: edge toward the kerb and slow to a crawl
    HoldQueued,      // already queued at a light/leader: just stay put
};

// One active responder, snapshotted by TrafficSystem::update() each frame.
// vel MUST be the responder's real planar body velocity (Vehicle::body()
// .linear_vel) — the heading/speed classification below runs on it, and the
// min-speed gate deliberately drops a stopped responder (an ambulance dwelling
// on scene doesn't freeze the street; normal avoidance handles a parked truck).
struct EmergencyResponderView {
    glm::vec2 pos{0.f};   // world XZ
    glm::vec2 vel{0.f};   // planar velocity, m/s
};

// Net-new knobs (defaults ARE the baseline), embedded in TrafficTuning for
// the F1 Dev-Tweaks path.
struct EmergencyYieldTuning {
    float detect_range        = 42.f;  // m: responders beyond this are ignored
    float lateral_gate        = 10.f;  // m off the ego heading line: a responder
                                       //   on a parallel street never triggers
    float min_responder_speed = 3.0f;  // m/s: below this the responder is
                                       //   parked/dwelling, not approaching
    float parallel_dot        = 0.5f;  // heading dot over this == same
                                       //   direction; under -this == oncoming;
                                       //   between == crossing (junction
                                       //   negotiation owns that: no yield)
    float resume_behind_m     = 15.f;  // m the responder must be PAST the ego
                                       //   before the yield may release
    float resume_stagger_min_s = 1.0f; // s: per-car resume delay band, so the
    float resume_stagger_max_s = 2.0f; //   street un-freezes progressively
    float pull_over_offset    = 1.5f;  // m kerb-side lateral bias (same dir);
                                       //   the drive loop clamps to the lane band
    float edge_offset         = 1.0f;  // m kerb-side bias for the oncoming side
    float edge_slow_speed     = 3.0f;  // m/s crawl cap while edging (oncoming)
};

// Classify ONE responder against the ego car. `ego_fwd` is the ego's planar
// heading (un-normalised is fine); `ego_in_junction` suppresses a NEW yield so
// a car mid-traversal finishes crossing the box first (the caller feeds
// ai_in_turn); `ego_queued` maps a would-be yield to HoldQueued — a car boxed
// in at a light stays put instead of darting kerb-side out of a queue.
// Geometry, in the ego frame (along = responder's signed distance ahead):
//   same direction  — yields while the responder is behind us or within
//                     resume_behind_m past (it closes from behind, we pull
//                     over ahead of it); a responder already well ahead of us
//                     (we are BEHIND it) never yields.
//   oncoming        — yields while the responder is still ahead of us (closing)
//                     or within resume_behind_m past.
//   crossing        — never (|heading dot| under parallel_dot).
// Pure + headless-testable (cf. assess_player_hazard).
EmergencyYield emergency_yield_classify(glm::vec2 ego_pos, glm::vec2 ego_fwd,
                                        bool ego_in_junction, bool ego_queued,
                                        const EmergencyResponderView& r,
                                        const EmergencyYieldTuning& t);

// Advance one car's yield latch a frame and return the EFFECTIVE yield. `now`
// is this frame's binding classification over all responders (None when none
// yields). A live classification latches and re-arms the resume timer to
// `stagger_s`; once the classification drops (responder past + resume_behind_m
// — the classify-side hysteresis) the latch HOLDS for the remaining stagger,
// then releases. Deterministic; mutates only the two threaded-in fields.
EmergencyYield emergency_yield_tick(EmergencyYield& latched, float& resume_s,
                                    EmergencyYield now, float stagger_s,
                                    float dt);

// Deterministic per-car resume stagger in [min_s, max_s), hashed off the car's
// planar position when the yield releases — neighbouring cars land on
// different delays, so the street un-freezes progressively, not all at once.
float emergency_yield_stagger(glm::vec2 pos, const EmergencyYieldTuning& t);

// Global, runtime-tunable traffic-AI knobs (PCG-005). Promoted from the
// compile-time constexprs in traffic_drive.cpp so Phases B-D — and the F1
// Dev-Tweaks panel — can iterate on AI behaviour without a recompile. One
// instance is held by TrafficSystem and shared by every AI car (no per-car
// overrides). The defaults below REPRODUCE the shipped constexpr values
// exactly: changing a default here is a deliberate behaviour change, guarded by
// the PCG-004 characterization tests, not a refactor. Structural constants
// (lane half-width, car length, turn-arc geometry) stay compile-time.
struct TrafficTuning {
    // Blocked-car avoidance: a car behind a dynamic blocker grows a blocked
    // timer, then (per its driver profile) honks / cautiously shifts toward the
    // opposing lane.
    float stuck_speed = 1.5f;   // m/s; below this a car counts as "not moving"
    float stuck_gap   = 8.0f;   // m; leader gap under which it counts as blocked
    // (PCG-038 removed swerve_rate — the wall-clock lateral ramp it drove was the
    // sideways slide the founder flagged. The go-around now arcs via the steering-
    // lock / shift-time / min-shift-speed knobs in OvertakeTuning.)

    // Follow-gap floor (PCG-009). The bumper gap a follower settles into under
    // the IDM must be at least a car length — otherwise its OBB overlaps the
    // (dynamic / parked) leader's, the SAT resolver fires, and the AI car is
    // demoted to PhysicsFallback dead scenery. Floors every driver's personality
    // `min_gap` (see effective_min_gap), so steady-state following can never
    // manufacture an overlap. Must be >= CAR_LENGTH (the gap math's 4 m nominal
    // car length); default carries a small margin over it. Live-tunable so the
    // founder can trade following-distance realism vs. road density.
    float min_follow_gap = 4.5f;   // m; >= CAR_LENGTH so OBBs never settle overlapping

    // Jam recovery / despawn (PCG-009). One shared recover-or-despawn policy for
    // genuinely-stuck cars (reused by PCG-007 panic / PCG-010 junction stalls):
    // a car that can't get moving again within bounded time is cleanly despawned
    // instead of being left as a permanently-parked wreck at the roadside.
    //   recovery_give_up   — seconds a collision-demoted Parked car tries to
    //                        re-attach to its lane before we give up and despawn it.
    //   stuck_despawn_seconds — seconds an AI car may sit in deep BlockedRecovery
    //                        wedged behind a NON-AI obstacle (a parked wreck /
    //                        the player) before it's despawned. Gated on a non-AI
    //                        blocker (see traffic_should_despawn_jam) so a car
    //                        merely queued at a red is never despawned.
    float recovery_give_up      = 8.0f;    // s; Parked->AI recovery window, then despawn
    float stuck_despawn_seconds = 30.0f;   // s; AI deep-jam ceiling, then despawn

    // Broadphase query radii for the per-car CarGrid scans. Each is a
    // conservative superset of what the precise test needs, so the spatial cull
    // never drops a car that could actually constrain this one.
    float leader_query_r = 45.f;   // leader detection (ai_update_speed)
    float yield_query_r  = 26.f;   // cross traffic, around the junction
    float shift_margin_r = 12.f;   // added to clear_dist for a lane-shift

    // Intersection gap-acceptance at uncontrolled / stop-controlled junctions.
    float junction_lookahead  = 20.f; // m; how far back negotiation begins
    float junction_conflict_r = 5.f;  // m; crossing-zone radius (inside STOP_BACK)
    float cross_parallel_dot  = 0.7f; // |dot| of headings below which paths cross

    // Cornering comfort: the max lateral acceleration used to cap speed through
    // a bend (a kinematic AI car has no tyre model to limit it otherwise).
    float corner_lat_accel = 3.5f;    // m/s^2

    // Player reactivity (PCG-006). See PlayerHazardTuning above. New in this
    // ticket, so there is no "shipped constexpr" to reproduce — these defaults
    // ARE the baseline. (The F1 Dev-Tweaks panel exposes player_hazard.*.)
    PlayerHazardTuning player_hazard{};

    // On-foot person reactivity (PCG-134; cone NARROWED in the PCG-143 line).
    // The SAME geometry assessor as PCG-006, tuned for a PERSON. The lateral gate
    // (half_width / collision_r) is the TRIGGER CONE — how far off the car's
    // driving line a person must be for the car to react. The founder wants it
    // tight and inward ("[car]>", not a wide "[car]))"): the car should only
    // react to someone in its ACTUAL path, so the player can run alongside / cut
    // in front and the car doesn't magically yield from off to the side.
    //   half_width / collision_r 1.5 m == the car's collision half-width (chassis
    //     1.0 m + player radius ~0.4 m). So the car reacts to exactly the people
    //     it would hit, no wider. A person at the road centre (2 m off the lane
    //     line, > 1.5 m) is now IGNORED — you can stand just off the driving line
    //     and traffic flows past. (This intentionally reverts the 2.5 m widening:
    //     "yield to anyone in the road" became "react only to your direct path".)
    //   Combined with the short ~2-5 m forward reaction window (PersonBrakeTuning)
    //     the live trigger is a small box right in front of the bumper — the tight
    //     forward wedge the founder sketched.
    //   range / ttc_horizon unchanged.
    // Note: the PCG-007 panic/flee escalation is gated OFF for the on-foot case
    // in ai_update_speed — cars slow & stop for a person, they don't bolt.
    PlayerHazardTuning player_hazard_on_foot{
        18.f,   // range       (m) — detection extent (braking is gated far shorter)
        1.5f,   // half_width  (m) — trigger cone == car+player collision half-width
        2.0f,   // ttc_horizon (s) — crossing look-ahead
        1.5f,   // collision_r (m) — match half_width: only a crosser into the path
        6.0f,   // honk_gap    (m) — honk at a jaywalker right in front
    };

    // PCG-143: quick, urgent braking for a person in the path + the visual
    // recoil dive it produces. Net-new, so these defaults ARE the baseline.
    PersonBrakeTuning person_brake{};
    RecoilTuning      recoil{};

    // Go-around / overtaking (PCG-008). See OvertakeTuning above. Also net-new,
    // so these defaults ARE the baseline. (F1 Dev-Tweaks exposes overtake.*.)
    OvertakeTuning overtake{};

    // Startle & flee (PCG-007). See PanicTuning above. Net-new, so these
    // defaults ARE the baseline. (F1 Dev-Tweaks exposes panic.*.)
    PanicTuning panic{};

    // Police realism (PCG-011). See PoliceTuning in police_ai.h. Net-new, so
    // these defaults ARE the baseline: ambient patrol fraction + the witness
    // range / view-cone gate for converting a patrol to a pursuer.
    PoliceTuning police{};

    // Yield to emergency vehicles (PCG-244). See EmergencyYieldTuning above.
    // Net-new, so these defaults ARE the baseline.
    EmergencyYieldTuning emergency{};

    // Maneuver recovery (PCG-158+). See RecoveryTuning above. Net-new, so
    // these defaults ARE the baseline. (Consumed as the actions wire up:
    // Nudge in PCG-159, the dynamic ladder in PCG-161/163.)
    RecoveryTuning recovery{};

    // Dynamic maneuver controller (PCG-161). See ManeuverTuning above.
    // Net-new; defaults ARE the baseline.
    ManeuverTuning maneuver{};
};

// Player-hazard assessment (PCG-006). Decides whether the human-driven car is an
// imminent hazard to an AI car and, if so, how hard to react. Pure 2D kinematics
// in the world XZ plane — NO sim / scene / TrafficSystem state — so it is
// unit-testable headless (the production wiring in traffic_drive.cpp just feeds
// it positions/velocities and routes the result into the existing IDM brake).
//
// Two complementary detectors, both off the in-lane leader corridor:
//   (A) the player is ahead within a widened path corridor (a static or moving
//       leader the lane scan's narrow 2 m corridor would miss); and
//   (B) the player is on a closing collision course (a crosser / head-on car)
//       whose predicted closest approach breaches `collision_r` within
//       `ttc_horizon` — caught BEFORE the OBB-SAT impulse, not after.
// The binding (closest) of the two is returned.
struct PlayerHazard {
    bool  active       = false;                                    // react to the player at all
    float gap          = std::numeric_limits<float>::infinity();   // bumper gap for the IDM
                                                                  //   virtual leader (+inf == none)
    float leader_speed = 0.f;   // player speed component along the AI heading (m/s);
                                //   the IDM closing term dv = ai_speed - leader_speed
    bool  honk         = false; // honk AT the player (close cut-off / blocking)
};

// `ai_fwd` is the AI car's (un-normalised is fine) planar heading; `*_pos` /
// `player_vel` are world XZ. `car_length` is the bumper offset subtracted from
// the centre-to-centre distance (matches the follow-gap math's CAR_LENGTH).
//
// PCG-156: `consider_path_leader` gates ONLY detector (A) — the swept-heading
// "leader ahead in my corridor" term. During a turn the physical heading sweeps
// through the corner arc, so (A)'s wide (range x half_width) cone rakes across a
// stopped player who is NOT on the lane the car is turning into — a false brake
// that freezes the car mid-turn (PCG-006 interaction). The caller passes `false`
// when the car is turning AND the player doesn't project onto the destination
// lane, suppressing (A) so the car completes its turn around an off-path player.
// Detector (B) (the closing collision course) is UNAFFECTED — a genuine
// crossing / head-on / drive-into-a-stopped-car is still braked for. Default
// `true` == the shipped behaviour (straight-line following / oncoming unchanged).
PlayerHazard assess_player_hazard(glm::vec2 ai_pos, glm::vec2 ai_fwd, float ai_speed,
                                  glm::vec2 player_pos, glm::vec2 player_vel,
                                  const PlayerHazardTuning& t, float car_length = 4.0f,
                                  bool consider_path_leader = true);

// Honk debounce (PCG-022). `Car::ai_honking` is recomputed every AI tick and
// stays true for as long as the jam / closing-player condition holds, so driving
// a horn one-shot straight off it would machine-gun. honk_should_fire() turns
// that LEVEL signal into a one-shot EVENT: it fires on the RISING edge
// (false->true) and only if at least `min_interval` seconds have elapsed since
// the LAST honk. So a sustained block honks exactly once, and a flag flickering
// across a threshold can't retrigger within the interval. Pure + headless-
// testable; the per-car state lives on the Car and is threaded back in by ref.
struct HonkDebounceState {
    bool   was_honking    = false;
    double last_honk_time = -1.0e9;   // far in the past -> first honk always allowed
};
bool honk_should_fire(bool honking_now, double now_seconds,
                      double min_interval, HonkDebounceState& state);

// PCG-007: the panic reaction phase for a single frame. None = normal driving;
// Startle = the brief brake/jolt that opens a panic; Flee = the speed-up-and-
// dart-aside escape that follows. Drives both the speed/offset shaping in
// ai_update_speed and the TrafficAgentState::Panic overlay label.
enum class PanicPhase : std::uint8_t { None, Startle, Flee };

// PCG-007: the reckless-player panic TRIGGER. Given the binding player hazard
// (assess_player_hazard — PCG-006's always-on signal) and the mutual closing
// speed between the two cars (m/s, >0 == the gap is shrinking), decide whether
// THIS encounter is reckless enough to startle/flee — i.e. the player is bearing
// down fast (closing >= trigger_closing) and already close (hazard gap <=
// trigger_gap). A merely-active hazard (a stopped player ahead the AI is calmly
// braking for) does NOT trip a panic; only an abrupt near-miss does. Pure +
// headless-testable, like assess_player_hazard / honk_should_fire.
bool panic_should_trigger(const PlayerHazard& hazard, float closing_speed,
                          const PanicTuning& t);

// PCG-007: advance one car's panic timer a frame and report the phase. The timer
// is REMAINING panic time (seconds); it ALWAYS decays by dt and clamps at 0, so
// a car can never get stuck in Panic — absent a fresh trigger it returns to None
// within `duration`, deterministically. A trigger from idle arms it to the full
// duration (so the panic opens in Startle); a trigger DURING an ongoing panic
// refreshes it but only up into the flee band (a sustained menace keeps the car
// fleeing rather than re-jolting into startle every frame). Phase is derived from
// how much of the duration has elapsed: the first `startle_frac` is Startle, the
// rest Flee. Pure (mutates only the passed-in timer) + headless-testable.
PanicPhase panic_tick(float& timer, bool triggered, float dt,
                      const PanicTuning& t);

struct LaneId {
    int     i = 0;
    int     j = 0;
    GridDir dir = GridDir::East;
};

bool operator==(const LaneId& a, const LaneId& b);
bool operator!=(const LaneId& a, const LaneId& b);

enum class TrafficTurnKind : std::uint8_t {
    Left,
    Straight,
    Right,
    UTurn,
};

// Turn classification + branch weights. Promoted from the (removed) grid-only
// TrafficLaneGraph to free functions; the unified LaneGraph's grid producer
// uses them so grid turn behaviour stays identical.
TrafficTurnKind turn_kind(GridDir from, GridDir to);
float           turn_weight(TrafficTurnKind kind);

} // namespace apricot
