// Traffic AI decision kernels, headless.
//
// Every function under test takes a plain-data view and returns a decision, so
// this suite is a decision table: fill a RecoveryView / TurnYieldCandidate /
// PlayerHazardView the way the drive loop would, and assert what comes back.
// That shape is the whole reason these kernels could be lifted out of
// probablecause at all (PENG-29).
//
// WHAT IS NOT HERE, and it matters. probablecause pinned the overtake lateral
// target and the nudge side-pick against lane geometry built by the REAL lane
// graph producer, and it drove the maneuver controllers closed-loop against a
// real vehicle substep. Neither the lane graph nor a car that steers is in this
// tree yet, so those tests did not come across and the ones below stand in
// where they can. Each place that is weaker than its original says so.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

#include "test_assert.h"

#include "city/city_rng.h"
#include "city/road_types.h"   // canonical carriageway widths
#include "city/traffic_ai.h"

using namespace apricot;

namespace {

void test_driver_math() {
    DriverProfile cautious = make_driver_profile(DriverProfileKind::Cautious);
    DriverProfile aggressive =
        make_driver_profile(DriverProfileKind::AggressiveLite);
    REQUIRE(cautious.headway > aggressive.headway);
    REQUIRE(traffic_follow_speed_for_gap(2.f, cautious) == 0.f);
    REQUIRE(traffic_follow_speed_for_gap(20.f, aggressive)
           > traffic_follow_speed_for_gap(20.f, cautious));
    REQUIRE(traffic_should_stop_for_yellow(30.f, 8.f, cautious));
    REQUIRE(!traffic_should_stop_for_yellow(30.f, 8.f, aggressive));
    REQUIRE(!traffic_profile_may_pass_jam(cautious, 60.f));
    REQUIRE(!traffic_profile_may_pass_jam(
        make_driver_profile(DriverProfileKind::Impatient), 1.f));
    REQUIRE(traffic_profile_may_pass_jam(
        make_driver_profile(DriverProfileKind::Impatient), 4.f));
    REQUIRE(traffic_profile_may_pass_jam(aggressive, 3.f));
}

// =============================================================================
// PCG-004: characterization of the driver-model helpers the Vehicle-NPC-AI epic
// (PCG-001) builds on. These pin TODAY's behavior of the pure functions in
// traffic_ai.{h,cpp} — turn classification, per-direction geometry, the IDM
// follow-speed curve, the yellow-light gate, the jam-pass thresholds, and the
// driver "personality" ordering. Phases B–D (player reactivity, overtaking,
// jam handling, pursuit) all lean on these; if any later change shifts a
// magnitude here it should be a deliberate edit to this file, not a silent
// regression. (No production code is touched — these only read public helpers.)
// =============================================================================

// turn_kind() is a pure delta classifier over GridDir (East=0,N=1,W=2,S=3):
// same heading = Straight, +1 (CCW) = Left, +3 = Right, +2 = U-turn. Pin the
// full 4x4 matrix so the routing/turn-signal logic that PCG-008 reshapes has a
// fixed reference. turn_weight() must rank Straight > Right > Left > U-turn and
// stay strictly positive (the branch sampler relies on that ordering).
void test_turn_classification_matrix() {
    const std::array<GridDir, 4> dirs{GridDir::East, GridDir::North,
                                      GridDir::West, GridDir::South};
    for (int f = 0; f < 4; ++f) {
        for (int t = 0; t < 4; ++t) {
            const int delta = (t - f + 4) & 3;
            TrafficTurnKind k = turn_kind(dirs[static_cast<std::size_t>(f)],
                                          dirs[static_cast<std::size_t>(t)]);
            TrafficTurnKind want = (delta == 0) ? TrafficTurnKind::Straight
                                 : (delta == 1) ? TrafficTurnKind::Left
                                 : (delta == 3) ? TrafficTurnKind::Right
                                                : TrafficTurnKind::UTurn;
            REQUIRE(k == want);
        }
    }
    const float ws = turn_weight(TrafficTurnKind::Straight);
    const float wr = turn_weight(TrafficTurnKind::Right);
    const float wl = turn_weight(TrafficTurnKind::Left);
    const float wu = turn_weight(TrafficTurnKind::UTurn);
    REQUIRE(ws > wr);
    REQUIRE(wr > wl);
    REQUIRE(wl > wu);
    REQUIRE(wu > 0.f);
}

// traffic_dir_info() is the single source of truth for grid lane geometry. Pin
// the invariants the lane builder + AI heading code depend on: the integer step
// (di,dj) matches the unit XZ vector, and `right` is the unit vector rotated
// -90 deg about +Y, i.e. right == (unit.z, 0, -unit.x). These relationships are
// what keep lane offsets and yaw consistent; PCG-008's lane model must preserve
// them.
void test_dir_info_consistency() {
    const std::array<GridDir, 4> dirs{GridDir::East, GridDir::North,
                                      GridDir::West, GridDir::South};
    for (GridDir d : dirs) {
        TrafficDirInfo info = traffic_dir_info(d);
        // Unit vector is planar and matches the integer step.
        REQUIRE(std::abs(info.unit.y) < 1e-6f);
        REQUIRE(std::abs(static_cast<float>(info.di) - info.unit.x) < 1e-6f);
        REQUIRE(std::abs(static_cast<float>(info.dj) - info.unit.z) < 1e-6f);
        REQUIRE(std::abs(glm::length(info.unit) - 1.f) < 1e-6f);
        // right == rotate(unit, -90 deg about +Y).
        REQUIRE(std::abs(info.right.x - info.unit.z) < 1e-6f);
        REQUIRE(std::abs(info.right.z + info.unit.x) < 1e-6f);
        // unit and right are orthogonal.
        REQUIRE(std::abs(glm::dot(info.unit, info.right)) < 1e-6f);
    }
}

// traffic_follow_speed_for_gap(): the simple gap->speed curve is (gap - min_gap)
// / headway, floored at 0. Pin: it is 0 at/under the minimum gap, strictly
// monotonically increasing once clear of it, and a shorter-headway (more
// aggressive) profile always wants more speed for the same gap. PCG-008/009
// adjust following/overtaking; this curve is the baseline.
void test_follow_speed_curve() {
    DriverProfile normal = make_driver_profile(DriverProfileKind::Normal);
    REQUIRE(traffic_follow_speed_for_gap(0.f, normal) == 0.f);
    REQUIRE(traffic_follow_speed_for_gap(normal.min_gap, normal) == 0.f);
    REQUIRE(traffic_follow_speed_for_gap(normal.min_gap - 1.f, normal) == 0.f);
    // Strictly increasing past the min gap.
    float prev = -1.f;
    for (float gap = normal.min_gap; gap <= 60.f; gap += 5.f) {
        float v = traffic_follow_speed_for_gap(gap, normal);
        REQUIRE(v >= 0.f);
        REQUIRE(v >= prev);          // non-decreasing across the whole sweep
        if (gap > normal.min_gap) REQUIRE(v > 0.f);
        prev = v;
    }
    // Shorter headway -> more desired speed at the same (large) gap.
    DriverProfile aggro = make_driver_profile(DriverProfileKind::AggressiveLite);
    REQUIRE(aggro.headway < normal.headway);
    REQUIRE(traffic_follow_speed_for_gap(40.f, aggro)
            > traffic_follow_speed_for_gap(40.f, normal));
}

// traffic_should_stop_for_yellow(): a driver stops for a yellow only if it could
// comfortably stop in time (distance_to_stop > kinematic stopping distance) AND
// it isn't a yellow-runner (yellow_bias < 0.7). Pin both gates: an aggressive
// runner (bias .85) never stops; a cautious driver stops when there's room but
// commits (runs it) when it's too close/fast to stop safely.
void test_yellow_light_gate() {
    DriverProfile cautious = make_driver_profile(DriverProfileKind::Cautious);
    DriverProfile aggro    = make_driver_profile(DriverProfileKind::AggressiveLite);
    REQUIRE(aggro.yellow_bias >= 0.7f);
    REQUIRE(cautious.yellow_bias < 0.7f);

    // Plenty of room, moderate speed: cautious stops, aggressive runs it.
    REQUIRE(traffic_should_stop_for_yellow(30.f, 8.f, cautious));
    REQUIRE(!traffic_should_stop_for_yellow(30.f, 8.f, aggro));

    // Too close / too fast to pull up in time: even the cautious driver commits
    // (distance_to_stop <= stopping distance => don't slam the brakes).
    REQUIRE(!traffic_should_stop_for_yellow(2.f, 25.f, cautious));

    // The yellow-runner never stops regardless of how much room there is.
    REQUIRE(!traffic_should_stop_for_yellow(200.f, 1.f, aggro));
}

// traffic_profile_may_pass_jam(): who is willing to creep around a stuck leader,
// and after how long. Pin the per-personality contract PCG-008/009 depend on:
//   - Cautious: never.
//   - Normal:   only after patience + 7 s.
//   - Impatient / AggressiveLite: after their (shorter) patience.
void test_jam_pass_thresholds() {
    DriverProfile cautious  = make_driver_profile(DriverProfileKind::Cautious);
    DriverProfile normal    = make_driver_profile(DriverProfileKind::Normal);
    DriverProfile impatient = make_driver_profile(DriverProfileKind::Impatient);
    DriverProfile aggro     = make_driver_profile(DriverProfileKind::AggressiveLite);

    // Cautious never passes a jam, no matter how long it waits.
    REQUIRE(!traffic_profile_may_pass_jam(cautious, 0.f));
    REQUIRE(!traffic_profile_may_pass_jam(cautious, 1000.f));

    // Normal: gated at patience + 7 s.
    REQUIRE(!traffic_profile_may_pass_jam(normal, normal.patience_seconds + 6.9f));
    REQUIRE(traffic_profile_may_pass_jam(normal, normal.patience_seconds + 7.0f));

    // Impatient / aggressive: gated at their own patience.
    REQUIRE(!traffic_profile_may_pass_jam(impatient,
                                          impatient.patience_seconds - 0.1f));
    REQUIRE(traffic_profile_may_pass_jam(impatient, impatient.patience_seconds));
    REQUIRE(traffic_profile_may_pass_jam(aggro, aggro.patience_seconds));
}

// make_driver_profile(): the four "personalities" are the knobs the whole epic
// (and the PCG-005 tuning panel) tunes. Pin their monotonic ordering from
// Cautious -> Normal -> Impatient -> AggressiveLite so a future profile re-tune
// is a deliberate, visible change rather than an accidental scramble.
void test_driver_profile_ordering() {
    DriverProfile c = make_driver_profile(DriverProfileKind::Cautious);
    DriverProfile n = make_driver_profile(DriverProfileKind::Normal);
    DriverProfile i = make_driver_profile(DriverProfileKind::Impatient);
    DriverProfile a = make_driver_profile(DriverProfileKind::AggressiveLite);

    // The kind round-trips into the profile.
    REQUIRE(c.kind == DriverProfileKind::Cautious);
    REQUIRE(a.kind == DriverProfileKind::AggressiveLite);

    // Desired speed climbs with aggression; following distance + patience shrink.
    REQUIRE(c.speed_mul < n.speed_mul);
    REQUIRE(n.speed_mul < i.speed_mul);
    REQUIRE(i.speed_mul < a.speed_mul);

    REQUIRE(c.headway > n.headway);
    REQUIRE(n.headway > i.headway);
    REQUIRE(i.headway > a.headway);

    REQUIRE(c.min_gap > n.min_gap);
    REQUIRE(n.min_gap > i.min_gap);
    REQUIRE(i.min_gap > a.min_gap);

    REQUIRE(c.patience_seconds > n.patience_seconds);
    REQUIRE(n.patience_seconds > i.patience_seconds);
    REQUIRE(i.patience_seconds > a.patience_seconds);

    // Honk sooner, run yellows more, brake/accel harder as aggression rises.
    REQUIRE(c.honk_after > n.honk_after);
    REQUIRE(i.honk_after > a.honk_after);
    REQUIRE(c.yellow_bias < n.yellow_bias);
    REQUIRE(n.yellow_bias < i.yellow_bias);
    REQUIRE(i.yellow_bias < a.yellow_bias);
    REQUIRE(a.accel > c.accel);
    REQUIRE(a.brake > c.brake);
}

// =============================================================================
// PCG-009: effective_min_gap — the follow-gap-vs-car-length reconciliation. The
// IDM floors a follower's bumper gap at its driver profile's min_gap, but all
// four profiles sit BELOW the 4 m collision car length (1.8-3.8 m), so a
// follower closing on a dynamic / parked leader overlaps its OBB and the SAT
// resolver demotes it to PhysicsFallback dead scenery. effective_min_gap raises
// the profile gap to at least the tuning floor, which must itself be >= the car
// length. Pin that invariant here (headless), since the empirical "fewer PHYS
// cars" drop needs a running sim a clean worktree can't cook.
// =============================================================================

// The shipped default floor is at least a car length, and floored against it
// every personality's standstill gap clears the OBB; a driver who personally
// wants MORE than the floor keeps its larger gap (the floor only raises, never
// lowers). CAR_LENGTH (4 m) is the gap-math nominal in traffic_drive.cpp; mirror
// it here as the invariant target (assess_player_hazard's default is the same 4 m).
void test_effective_min_gap_at_least_car_length() {
    constexpr float kCarLength = 4.0f;   // == traffic_drive.cpp CAR_LENGTH

    // The shipped tuning default is principled: never below a car length.
    TrafficTuning t;
    REQUIRE(t.min_follow_gap >= kCarLength);

    // Every personality, floored against the shipped default, settles at >= a
    // car length — so steady-state following can't manufacture an OBB overlap.
    const std::array<DriverProfileKind, 4> kinds{
        DriverProfileKind::Cautious, DriverProfileKind::Normal,
        DriverProfileKind::Impatient, DriverProfileKind::AggressiveLite};
    for (DriverProfileKind k : kinds) {
        DriverProfile p = make_driver_profile(k);
        float g = effective_min_gap(p, t.min_follow_gap);
        REQUIRE(g >= kCarLength);
        REQUIRE(g >= t.min_follow_gap);   // the floor always wins for these profiles
    }

    // The floor only raises: a driver who already wants a bigger gap keeps it.
    DriverProfile roomy = make_driver_profile(DriverProfileKind::Cautious);
    roomy.min_gap = 7.0f;
    REQUIRE(effective_min_gap(roomy, t.min_follow_gap) == 7.0f);

    // ...and a floor below a profile's own min_gap leaves that profile untouched.
    DriverProfile normal = make_driver_profile(DriverProfileKind::Normal);
    REQUIRE(effective_min_gap(normal, 0.f) == normal.min_gap);
}

// PCG-009 (Aisha review fix): the deep-jam despawn gate. The blocking bug was a
// follower queued at a red — blocked by a STOPPED AI LEADER within stuck_gap —
// ratcheting its leader-gap-based `blocked` timer across red phases until it
// crossed the 30 s ceiling and got despawned out of the queue in front of the
// player. The fix gates the despawn on the immediate blocker being a NON-AI
// obstacle (leader_is_dynamic), which can never be true inside a signal queue.
// PCG-165 grew the signature (legacy toggle + last-resort preconditions); this
// suite pins the BASE semantics with the new preconditions satisfied
// (exhausted, far from the player), so the original PCG-009 pins are
// unchanged — and re-asserts the signal-queue exclusion in BOTH modes.
namespace {

// The PCG-009 base with the PCG-165 last-resort preconditions SATISFIED:
// ladder exhausted, player far. Isolates the original pins.
bool despawn_base(bool blocked, float blocked_s, float ceiling, bool dyn,
                  bool legacy = false) {
    return traffic_should_despawn_jam(blocked, blocked_s, ceiling, dyn,
                                      legacy, /*maneuvers_exhausted*/ true,
                                      /*dist_from_player*/ 500.f,
                                      /*min_player_dist*/ 60.f);
}

} // namespace

void test_jam_despawn_gate_excludes_signal_queue() {
    const float kCeiling = TrafficTuning{}.stuck_despawn_seconds;   // 30 s default

    // THE BUG, NOW PINNED: a car blocked WAY past the ceiling but by an AI
    // leader (leader_is_dynamic == false — the signal-queue / yield case) must
    // NEVER be flagged — in EITHER despawn mode. Test well past the ceiling to
    // cover the cross-red-phase ratchet that originally tripped this.
    REQUIRE(!despawn_base(/*blocked*/ true, kCeiling + 100.f, kCeiling,
                          /*leader_is_dynamic*/ false, /*legacy*/ false));
    REQUIRE(!despawn_base(true, kCeiling + 100.f, kCeiling, false,
                          /*legacy*/ true));

    // A car wedged behind a NON-AI obstacle (parked wreck / player) past the
    // ceiling IS flagged — the rolling-roadblock case the ticket is about.
    REQUIRE(despawn_base(true, kCeiling + 0.1f, kCeiling,
                         /*leader_is_dynamic*/ true));

    // Below the ceiling, even behind a dynamic obstacle, it is NOT flagged yet
    // (the car may still recover); strict `>` means exactly-at-ceiling is false.
    REQUIRE(!despawn_base(true, kCeiling - 0.1f, kCeiling, true));
    REQUIRE(!despawn_base(true, kCeiling, kCeiling, true));

    // Not blocked at all (e.g. moving again) is never a despawn, regardless of
    // how the other inputs look.
    REQUIRE(!despawn_base(/*blocked*/ false, kCeiling + 100.f, kCeiling, true));
}

// PCG-165: the despawn is a LAST RESORT. With the PCG-009 base satisfied
// (blocked past the ceiling behind a non-AI obstacle), the new mode also
// demands the recovery ladder exhausted AND the car far from the player; the
// legacy A/B toggle restores the shipped instant behaviour exactly.
void test_jam_despawn_last_resort_gate() {
    const float kCeiling = TrafficTuning{}.stuck_despawn_seconds;
    const float kMinDist = RecoveryTuning{}.giveup_min_player_dist;   // 60 m
    const float kPast    = kCeiling + 5.f;

    // Ladder NOT exhausted (the car can still try something): never despawn,
    // however far the player is.
    REQUIRE(!traffic_should_despawn_jam(true, kPast, kCeiling, true,
                                        /*legacy*/ false, /*exhausted*/ false,
                                        500.f, kMinDist));
    // Exhausted but NEAR the player: never blink out in view — hold instead.
    REQUIRE(!traffic_should_despawn_jam(true, kPast, kCeiling, true,
                                        false, true,
                                        kMinDist - 1.f, kMinDist));
    // Exhausted + far: the clean, unseen last resort.
    REQUIRE(traffic_should_despawn_jam(true, kPast, kCeiling, true,
                                       false, true, kMinDist, kMinDist));
    // No player reference (negative sentinel, PCG-157 convention) == far.
    REQUIRE(traffic_should_despawn_jam(true, kPast, kCeiling, true,
                                       false, true, -1.f, kMinDist));

    // Legacy A/B: the toggle restores the shipped PCG-009 instant despawn —
    // preconditions ignored (near player, not exhausted)...
    REQUIRE(traffic_should_despawn_jam(true, kPast, kCeiling, true,
                                       /*legacy*/ true, /*exhausted*/ false,
                                       5.f, kMinDist));
    // ...but the BASE is still required in legacy mode too (no new despawns).
    REQUIRE(!traffic_should_despawn_jam(true, kPast, kCeiling,
                                        /*leader_is_dynamic*/ false,
                                        true, true, 500.f, kMinDist));
    REQUIRE(!traffic_should_despawn_jam(true, kCeiling - 1.f, kCeiling, true,
                                        true, true, 500.f, kMinDist));
}

// =============================================================================
// PCG-157: recovery/despawn telemetry — read-only instrumentation for the
// maneuver-recovery epic. Pins the histogram bucketing (floor(dist/25), clamped,
// negative == no-player-reference files under the LAST bucket) and that the
// record helpers bump exactly one counter + one bucket per event.
// =============================================================================

void test_recovery_telemetry_bucketing() {
    const float bm = RecoveryTelemetry::BUCKET_M;   // 25 m
    const int   nb = RecoveryTelemetry::BUCKETS;    // 8

    // Bucket edges: [0,25) -> 0, [25,50) -> 1, ... exactly-at-edge rounds UP
    // into the next bucket (floor semantics).
    REQUIRE(telemetry_dist_bucket(0.f, bm, nb) == 0);
    REQUIRE(telemetry_dist_bucket(bm - 0.01f, bm, nb) == 0);
    REQUIRE(telemetry_dist_bucket(bm, bm, nb) == 1);
    REQUIRE(telemetry_dist_bucket(6.f * bm + 1.f, bm, nb) == 6);

    // Everything from the last edge (175 m) outward — including well past the
    // 200 m despawn radius — lands in the final bucket, never out of range.
    REQUIRE(telemetry_dist_bucket(7.f * bm, bm, nb) == nb - 1);
    REQUIRE(telemetry_dist_bucket(1000.f, bm, nb) == nb - 1);

    // No player reference (negative sentinel) files under the last bucket too —
    // it must never masquerade as a near-player despawn.
    REQUIRE(telemetry_dist_bucket(-1.f, bm, nb) == nb - 1);
}

void test_recovery_telemetry_record() {
    RecoveryTelemetry rt;

    // One jam event at 5 m (bucket 0): exactly one count, one bucket.
    rt.record_jam(5.f);
    REQUIRE(rt.jam_count == 1u);
    REQUIRE(rt.jam_hist[0] == 1u);
    REQUIRE(rt.give_up_count == 0u);

    // A give-up with no player reference: last bucket, independent counters.
    rt.record_give_up(-1.f);
    REQUIRE(rt.give_up_count == 1u);
    REQUIRE(rt.give_up_hist[RecoveryTelemetry::BUCKETS - 1] == 1u);
    REQUIRE(rt.jam_count == 1u);   // jam side untouched

    // Accumulation in a middle bucket (60 m -> bucket 2), then reset clears all.
    rt.record_jam(60.f);
    rt.record_jam(60.f);
    REQUIRE(rt.jam_count == 3u);
    REQUIRE(rt.jam_hist[2] == 2u);
    rt.reset();
    REQUIRE(rt.jam_count == 0u);
    REQUIRE(rt.give_up_count == 0u);
    REQUIRE(rt.jam_hist[0] == 0u);
    REQUIRE(rt.jam_hist[2] == 0u);
    REQUIRE(rt.give_up_hist[RecoveryTelemetry::BUCKETS - 1] == 0u);
}

// =============================================================================
// PCG-158: recovery_plan — the maneuver-recovery escalation kernel (epic #232).
// Decision table over RecoveryView: which action a deeply-blocked car runs
// next. Pins eligibility per blocker kind, the bounded budgets (an action past
// its budget escalates, never runs forever), monotone escalation within a
// cycle, and the GiveUp gate: far-from-player-only (near the player an
// exhausted car retries or holds — it must never blink out in view).
// =============================================================================

namespace {

RecoveryView recovery_view(RecoveryBlocker b, float shoulder, float rear,
                           float player_dist, bool reroute = false,
                           bool bidir = true) {
    RecoveryView v;
    v.blocker            = b;
    v.front_gap          = 3.f;
    v.rear_gap           = rear;
    v.shoulder_clear     = shoulder;
    v.oncoming_clear     = false;
    v.road_bidirectional = bidir;
    v.reroute_available  = reroute;
    v.dist_from_player   = player_dist;
    return v;
}

} // namespace

void test_recovery_plan_decision_table() {
    const RecoveryTuning t{};

    // Not deep-blocked -> no recovery, whatever else the view says.
    REQUIRE(recovery_plan(recovery_view(RecoveryBlocker::None, 9.f, 99.f, 500.f,
                                        true),
                          RecoveryAction::None, 0.f, t)
            == RecoveryAction::None);

    // A static wreck with a usable shoulder -> the cheap kinematic Nudge.
    REQUIRE(recovery_plan(recovery_view(RecoveryBlocker::StaticWreck,
                                        t.nudge_min_shoulder, 99.f, 500.f),
                          RecoveryAction::None, 0.f, t)
            == RecoveryAction::Nudge);
    // The player's stopped car is a static obstacle too.
    REQUIRE(recovery_plan(recovery_view(RecoveryBlocker::Player, 3.f, 99.f, 30.f),
                          RecoveryAction::None, 0.f, t)
            == RecoveryAction::Nudge);

    // No shoulder -> skip Nudge, Reverse if there is room behind.
    REQUIRE(recovery_plan(recovery_view(RecoveryBlocker::StaticWreck, 0.5f,
                                        t.reverse_min_rear, 500.f),
                          RecoveryAction::None, 0.f, t)
            == RecoveryAction::Reverse);

    // The mid-box standoff (junction-deadlock residual): NEVER Nudge — even
    // with apparent lateral room there is no shoulder in the box — Reverse is
    // the resolution.
    REQUIRE(recovery_plan(recovery_view(RecoveryBlocker::OpposingTraverser, 9.f,
                                        t.reverse_min_rear, 500.f),
                          RecoveryAction::None, 0.f, t)
            == RecoveryAction::Reverse);

    // An AI deep jam is a legitimate queue: never Nudge past it; with room
    // behind and a side street, back out / reroute.
    REQUIRE(recovery_plan(recovery_view(RecoveryBlocker::AiJam, 9.f, 99.f, 500.f,
                                        /*reroute*/ true),
                          RecoveryAction::None, 0.f, t)
            == RecoveryAction::Reverse);

    // A one-way road can't host a 3PT: with no shoulder and no room to
    // reverse, the ladder falls through to Reroute.
    REQUIRE(recovery_plan(recovery_view(RecoveryBlocker::StaticWreck, 0.5f, 1.f,
                                        500.f, /*reroute*/ true, /*bidir*/ false),
                          RecoveryAction::None, 0.f, t)
            == RecoveryAction::Reroute);
}

void test_recovery_plan_budgets_and_escalation() {
    const RecoveryTuning t{};
    const RecoveryView v = recovery_view(RecoveryBlocker::StaticWreck,
                                         t.nudge_min_shoulder,
                                         t.reverse_min_rear, 500.f,
                                         /*reroute*/ true);

    // Within budget + still eligible: keep the running action (no thrash).
    REQUIRE(recovery_plan(v, RecoveryAction::Nudge,
                          t.nudge_budget_s - 0.1f, t)
            == RecoveryAction::Nudge);
    // Past its bounded budget: escalate to the next eligible action.
    REQUIRE(recovery_plan(v, RecoveryAction::Nudge,
                          t.nudge_budget_s + 0.1f, t)
            == RecoveryAction::Reverse);
    // Eligibility lost mid-action (shoulder collapsed): escalate immediately.
    RecoveryView lost = v;
    lost.shoulder_clear = 0.f;
    REQUIRE(recovery_plan(lost, RecoveryAction::Nudge, 1.f, t)
            == RecoveryAction::Reverse);
    // Monotone: escalation never goes BACK down the ladder within a cycle —
    // an expired Reverse with a (re-)usable shoulder still moves FORWARD.
    REQUIRE(recovery_plan(v, RecoveryAction::Reverse,
                          t.reverse_budget_s + 0.1f, t)
            == RecoveryAction::ThreePointTurn);
}

void test_recovery_plan_giveup_is_far_from_player_only() {
    const RecoveryTuning t{};
    // Nothing eligible at all (no shoulder, boxed front AND rear, no reroute).
    const RecoveryView boxed =
        recovery_view(RecoveryBlocker::StaticWreck, 0.f, 0.5f, 500.f);

    // Far from the player: exhausted -> GiveUp (the clean, unseen despawn).
    REQUIRE(recovery_plan(boxed, RecoveryAction::None, 0.f, t)
            == RecoveryAction::GiveUp);
    // No player reference (negative sentinel, PCG-157 convention) == far.
    RecoveryView no_player = boxed;
    no_player.dist_from_player = -1.f;
    REQUIRE(recovery_plan(no_player, RecoveryAction::None, 0.f, t)
            == RecoveryAction::GiveUp);

    // NEAR the player: never GiveUp. Boxed-in -> hold (None, today's
    // behaviour)...
    RecoveryView near = boxed;
    near.dist_from_player = t.giveup_min_player_dist - 1.f;
    REQUIRE(recovery_plan(near, RecoveryAction::None, 0.f, t)
            == RecoveryAction::None);
    // ...and with something eligible, an exhausted ladder RETRIES from the
    // top instead of despawning in view.
    RecoveryView near_retry = near;
    near_retry.reroute_available = true;   // Reroute was the LAST rung...
    REQUIRE(recovery_plan(near_retry, RecoveryAction::Reroute,
                          t.reroute_budget_s + 0.1f, t)
            == RecoveryAction::Reroute);   // ...retried as the only eligible rung
    RecoveryView near_retry2 = near;
    near_retry2.shoulder_clear = 9.f;      // shoulder reopened after exhaustion
    REQUIRE(recovery_plan(near_retry2, RecoveryAction::Reroute, 99.f, t)
            == RecoveryAction::Nudge);     // cycle restarts at the cheapest rung

    // Boundary: exactly AT the give-up distance counts as far.
    RecoveryView at_edge = boxed;
    at_edge.dist_from_player = t.giveup_min_player_dist;
    REQUIRE(recovery_plan(at_edge, RecoveryAction::None, 0.f, t)
            == RecoveryAction::GiveUp);
}

// =============================================================================
// PCG-167: turner_should_yield — the permissive-left yield kernel. A pre-commit
// turner across oncoming holds for (ii) opposing committed cars, (iii) the
// closer of two opposing pre-commit turners (near-tie -> stable antisymmetric
// pick: deadlock-free), and (i) oncoming straight-through actually ROLLING at
// the box (stopped never holds — the anti-starvation half; an impatient waiter
// shrinks the acceptance window). Pure kernel; the producer
// (ai_turner_must_yield) fills the candidate views from the CarGrid.
// =============================================================================

namespace {
constexpr float kYieldLook  = 20.f;   // TrafficTuning::junction_lookahead default
constexpr float kYieldHold  = 11.f;   // STOP_BACK (6) + junction_conflict_r (5)
constexpr float kYieldStuck = 1.5f;   // TrafficTuning::stuck_speed default

TurnYieldCandidate yield_cand(bool crossing, bool committed, float to_jn,
                              float speed, bool tie_winner = false) {
    TurnYieldCandidate v;
    v.opposing    = true;
    v.crossing    = crossing;
    v.committed   = committed;
    v.to_junction = to_jn;
    v.speed       = speed;
    v.tie_winner  = tie_winner;
    return v;
}
} // namespace

void test_turn_yield_committed_holds() {
    // (ii) An opposing car past its line / mid-turn ALWAYS holds me — moving or
    // wedged (holding at the line beats joining the wedge), crossing or straight.
    REQUIRE(turner_should_yield(yield_cand(true, true, 0.f, 0.f), 8.f,
                                kYieldLook, kYieldHold, kYieldStuck, false));
    REQUIRE(turner_should_yield(yield_cand(false, true, 3.f, 6.f), 8.f,
                                kYieldLook, kYieldHold, kYieldStuck, false));
    // ...even for a long-waiting impatient turner: impatience only shrinks the
    // rolling-oncoming window, never licenses driving into a committed car.
    REQUIRE(turner_should_yield(yield_cand(true, true, 0.f, 0.f), 8.f,
                                kYieldLook, kYieldHold, kYieldStuck, true));
    // Beyond negotiation range: no constraint.
    REQUIRE(!turner_should_yield(yield_cand(true, true, kYieldLook + 1.f, 0.f),
                                 8.f, kYieldLook, kYieldHold, kYieldStuck, false));
    // Not on the opposite approach at all: never a constraint.
    TurnYieldCandidate same_way = yield_cand(true, true, 2.f, 6.f);
    same_way.opposing = false;
    REQUIRE(!turner_should_yield(same_way, 8.f, kYieldLook, kYieldHold,
                                 kYieldStuck, false));
}

void test_turn_yield_oncoming_rolling_gate() {
    // (i) Oncoming straight-through ROLLING within the lookahead holds me...
    REQUIRE(turner_should_yield(yield_cand(false, false, 15.f, 10.f), 8.f,
                                kYieldLook, kYieldHold, kYieldStuck, false));
    // ...but essentially stopped it is NOT entering the box: no hold. This is
    // the anti-starvation half — a stationary oncoming queue can't freeze a
    // turner; only its front car gates, as it rolls off.
    REQUIRE(!turner_should_yield(yield_cand(false, false, 15.f, 0.f), 8.f,
                                 kYieldLook, kYieldHold, kYieldStuck, false));
    REQUIRE(!turner_should_yield(yield_cand(false, false, 15.f, kYieldStuck),
                                 8.f, kYieldLook, kYieldHold, kYieldStuck, false));
    // Impatience (fed from traffic_profile_may_pass_jam over the accumulated
    // yield wait) shrinks the window to the hold zone: a roller at 15 m still
    // holds a patient turner but no longer an impatient one...
    REQUIRE(!turner_should_yield(yield_cand(false, false, 15.f, 10.f), 8.f,
                                 kYieldLook, kYieldHold, kYieldStuck, true));
    // ...while one about to enter the box (inside the hold zone) holds anyone.
    REQUIRE(turner_should_yield(yield_cand(false, false, 8.f, 10.f), 8.f,
                                kYieldLook, kYieldHold, kYieldStuck, true));
}

void test_turn_yield_opposing_turners_exactly_one() {
    // (iii) The founder's deadlock: two opposing pre-commit crossing turners.
    // Evaluate the pair SYMMETRICALLY (each judges the other; tie_winner is
    // antisymmetric — here A wins ties) and pin that within negotiation range
    // EXACTLY ONE yields, for closer/farther both ways, a near-tie, and the
    // sub-epsilon band — deadlock-free (never both hold) AND collision-safe
    // (never both proceed).
    struct Pair { float a_to_jn, b_to_jn; };
    const Pair cases[] = {
        {8.f, 12.f},     // A clearly closer -> B yields
        {12.f, 8.f},     // B clearly closer -> A yields
        {9.f, 9.f},      // exact tie        -> tie pick decides
        {9.3f, 9.0f},    // inside the 0.5 m epsilon band -> tie pick decides
        {6.5f, 19.5f},   // extremes of the negotiation range
    };
    for (const Pair& p : cases) {
        const bool a_yields = turner_should_yield(
            yield_cand(true, false, p.b_to_jn, 4.f, /*tie: B wins?*/ false),
            p.a_to_jn, kYieldLook, kYieldHold, kYieldStuck, false);
        const bool b_yields = turner_should_yield(
            yield_cand(true, false, p.a_to_jn, 4.f, /*tie: A wins?*/ true),
            p.b_to_jn, kYieldLook, kYieldHold, kYieldStuck, false);
        REQUIRE(a_yields != b_yields);   // exactly one proceeds
    }
    // Distance dominates the tie pick: the closer car proceeds even when the
    // farther one holds the tie-winner slot.
    REQUIRE(!turner_should_yield(yield_cand(true, false, 12.f, 4.f,
                                            /*tie_winner*/ true),
                                 8.f, kYieldLook, kYieldHold, kYieldStuck, false));
    // Committed-vs-waiting resolves one way: the pre-commit turner yields (the
    // committed one's own gate no longer runs — the caller is pre-commit-only).
    REQUIRE(turner_should_yield(yield_cand(true, true, 2.f, 3.f), 8.f,
                                kYieldLook, kYieldHold, kYieldStuck, false));
}

// =============================================================================
// PCG-008: overtake_gap_acceptable — the collision-aware go-around gap-acceptance
// the real lane-change is built on. Pins the MOBIL-style decision: enough hard
// clearance ahead (covering the whole pass) + behind, and a time-to-collision
// margin against any car CLOSING on either gap. Pure helper; the production band
// scan (ai_overtake_lane_clear) only fills the view in. This replaces the old
// always-reject ai_safe_to_shift trigger (PCG-004 reachability bug).
// =============================================================================

// An empty target lane (+inf gaps, the default view) is always acceptable — this
// is the single-close-blocker case the PCG-004 finding flagged as previously
// unreachable: nothing in the oncoming lane, so the go-around must be allowed.
void test_overtake_empty_lane_accepts() {
    OvertakeTuning t;
    OvertakeLaneView v;                      // all defaults: +inf gaps, 0 closing
    REQUIRE(overtake_gap_acceptable(v, /*pass_length*/ 24.f, t));
    // Still accepts even for a long pass, since the lane is wide open.
    REQUIRE(overtake_gap_acceptable(v, 55.f, t));
}

// A far oncoming car (closing head-on at the SUM of speeds) is fine when its
// time-to-collision clears safe_ttc; the same closing speed at a shorter gap
// (TTC below safe_ttc) rejects — the dominant safety term for an oncoming pass.
void test_overtake_oncoming_ttc_gate() {
    OvertakeTuning t;                        // min_front 16, safe_ttc 4
    OvertakeLaneView far;
    far.front_gap = 100.f; far.front_closing = 24.f;   // TTC 4.17 s > 4
    REQUIRE(overtake_gap_acceptable(far, 24.f, t));

    OvertakeLaneView near = far;
    near.front_gap = 50.f;                              // TTC 2.08 s < 4
    REQUIRE(!overtake_gap_acceptable(near, 24.f, t));
}

// The front clearance must cover the whole pass: a parked car ahead in the target
// lane (no closing) within pass_length rejects even though it isn't moving.
void test_overtake_front_must_cover_pass_length() {
    OvertakeTuning t;
    OvertakeLaneView v;
    v.front_gap = 20.f; v.front_closing = 0.f;         // static car 20 m ahead
    REQUIRE(overtake_gap_acceptable(v, /*pass_length*/ 18.f, t));  // 20 >= max(16,18)
    REQUIRE(!overtake_gap_acceptable(v, /*pass_length*/ 40.f, t)); // 20 <  max(16,40)
}

// Rear safety (MOBIL): don't pull in front of a faster car already in the target
// lane that would then have to brake hard for us. A close hard floor, or a low
// rear TTC, rejects; a car receding (or far) behind is fine.
void test_overtake_rear_safety_gate() {
    OvertakeTuning t;                        // min_rear 8, safe_ttc 4
    // Faster car closing from behind: TTC 10/5 = 2 s < 4 -> reject.
    OvertakeLaneView closing_rear;
    closing_rear.rear_gap = 10.f; closing_rear.rear_closing = 5.f;
    REQUIRE(!overtake_gap_acceptable(closing_rear, 24.f, t));

    // Same gap but the rear car is receding (negative closing) -> fine.
    OvertakeLaneView receding_rear;
    receding_rear.rear_gap = 10.f; receding_rear.rear_closing = -3.f;
    REQUIRE(overtake_gap_acceptable(receding_rear, 24.f, t));

    // Too close behind regardless of speed (hard floor) -> reject.
    OvertakeLaneView tight_rear;
    tight_rear.rear_gap = 5.f; tight_rear.rear_closing = 0.f;
    REQUIRE(!overtake_gap_acceptable(tight_rear, 24.f, t));
}

// =============================================================================
// PCG-006: assess_player_hazard — the off-corridor player-reactivity hook the
// Phase-B gameplay work is built on. These pin the two detectors (in-path leader
// + closing collision course), the gates that keep normal oncoming traffic from
// false-triggering, and the honk threshold. Pure helper; no sim state touched.
// =============================================================================

// (A) A player stopped straight ahead, inside the path corridor, is a hazard: the
// bumper gap is fwd_dist - car_length, the player is stationary (leader_speed 0),
// and at 8 m it's braking but not yet honking (honk_gap 6).
void test_player_hazard_ahead_in_path() {
    PlayerHazardTuning t;
    PlayerHazard h = assess_player_hazard(
        glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}, 10.f,
        glm::vec2{12.f, 0.f}, glm::vec2{0.f, 0.f}, t);
    REQUIRE(h.active);
    REQUIRE(std::abs(h.gap - 8.f) < 1e-3f);     // 12 m centre-to-centre - 4 m car
    REQUIRE(h.leader_speed == 0.f);             // stationary player
    REQUIRE(!h.honk);                           // 8 m gap > 6 m honk threshold
}

// A player past the range gate is invisible to the hazard pass (the leader scan
// still handles anything genuinely in-lane via its own wider query radius).
void test_player_hazard_out_of_range() {
    PlayerHazardTuning t;
    PlayerHazard h = assess_player_hazard(
        glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}, 10.f,
        glm::vec2{50.f, 0.f}, glm::vec2{0.f, 0.f}, t);
    REQUIRE(!h.active);
    REQUIRE(std::isinf(h.gap));
}

// A stationary player off to the side (outside half_width) on no collision course
// is NOT a hazard — reactivity is for the player in/closing-on the path, not for
// a parked car beside the lane.
void test_player_hazard_beside_clear() {
    PlayerHazardTuning t;
    PlayerHazard h = assess_player_hazard(
        glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}, 10.f,
        glm::vec2{10.f, 5.f}, glm::vec2{0.f, 0.f}, t);
    REQUIRE(!h.active);
}

// (B) A player crossing into the path off-corridor (predicted closest approach
// breaches collision_r within ttc_horizon) is caught BEFORE contact: active, with
// a finite brake gap toward the conflict point (stationary virtual leader).
void test_player_hazard_crossing_course() {
    PlayerHazardTuning t;
    PlayerHazard h = assess_player_hazard(
        glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}, 10.f,
        glm::vec2{12.f, 8.f}, glm::vec2{0.f, -8.f}, t);   // crosses right into us
    REQUIRE(h.active);
    REQUIRE(std::isfinite(h.gap));
    REQUIRE(h.gap > 5.f);
    REQUIRE(h.gap < 9.f);
    REQUIRE(h.leader_speed == 0.f);              // brake toward the conflict point
}

// The key false-positive guard: a player driving the OTHER way in the adjacent
// (oncoming) lane — abreast at ~4 m, never crossing into the path — must NOT make
// AI traffic brake. half_width (2.6) skips detector (A); the predicted closest
// approach (~4 m) clears collision_r (2.6) so detector (B) skips too.
void test_player_hazard_oncoming_in_adjacent_lane_ignored() {
    PlayerHazardTuning t;
    PlayerHazard h = assess_player_hazard(
        glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}, 10.f,
        glm::vec2{10.f, 4.f}, glm::vec2{-10.f, 0.f}, t);  // passing the other way
    REQUIRE(!h.active);
}

// Close cut-off: a player right in front (under honk_gap) trips the honk-AT-player
// flag, distinct from the jam honk.
void test_player_hazard_honk_when_close() {
    PlayerHazardTuning t;
    PlayerHazard h = assess_player_hazard(
        glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}, 5.f,
        glm::vec2{8.f, 0.f}, glm::vec2{0.f, 0.f}, t);
    REQUIRE(h.active);
    REQUIRE(std::abs(h.gap - 4.f) < 1e-3f);
    REQUIRE(h.honk);
}

// A degenerate (zero) heading can't define a path; the helper bails safely rather
// than dividing by zero.
void test_player_hazard_degenerate_heading() {
    PlayerHazardTuning t;
    PlayerHazard h = assess_player_hazard(
        glm::vec2{0.f, 0.f}, glm::vec2{0.f, 0.f}, 10.f,
        glm::vec2{5.f, 0.f}, glm::vec2{0.f, 0.f}, t);
    REQUIRE(!h.active);
}

// (Aisha review gap, folded into PCG-007) detector (A) with a MOVING in-corridor
// player: leader_speed is the player's velocity component ALONG the AI heading
// (the IDM closing term is ai_speed - leader_speed). A player pulling away ahead
// (same heading) yields a positive leader_speed, so the AI isn't told to brake as
// if for a wall. Pin that the moving-leader speed is reported (was untested — only
// the stationary leader_speed == 0 case was covered).
void test_player_hazard_moving_leader_speed() {
    PlayerHazardTuning t;
    // Player 12 m straight ahead (gap 8), pulling away at 6 m/s in the AI's own
    // heading; AI doing 10. Closing TTC here is 12/4 = 3 s > ttc_horizon (2), so
    // detector (B) bails and we isolate detector (A)'s leader_speed.
    PlayerHazard h = assess_player_hazard(
        glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}, 10.f,
        glm::vec2{12.f, 0.f}, glm::vec2{6.f, 0.f}, t);
    REQUIRE(h.active);
    REQUIRE(std::abs(h.gap - 8.f) < 1e-3f);          // 12 m c2c - 4 m car
    REQUIRE(std::abs(h.leader_speed - 6.f) < 1e-3f); // +ve == player pulling away
}

// (Aisha review gap, folded into PCG-007) detector (B) bails on BOTH ends of its
// time window: a closing course whose predicted closest approach is beyond
// ttc_horizon (the conflict is too far off in time to react to yet), and a
// player that is RECEDING (ttc < 0 — closest approach already in the past). Both
// were previously untested; pin that neither false-triggers a brake.
void test_player_hazard_crossing_beyond_horizon_or_receding() {
    PlayerHazardTuning t;   // ttc_horizon 2 s, collision_r 2.6 m, half_width 2.6 m

    // Off-corridor crosser (lat 8 m > half_width, so (A) skips), AI stationary so
    // (B) sees only the player's motion. Moving toward the path at 3 m/s: closest
    // approach is at ttc = 8/3 = 2.67 s > ttc_horizon -> (B) bails (too far off).
    PlayerHazard far = assess_player_hazard(
        glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}, 0.f,
        glm::vec2{10.f, 8.f}, glm::vec2{0.f, -3.f}, t);
    REQUIRE(!far.active);

    // Same off-corridor player but RECEDING (velocity points away): ttc < 0, so
    // the closest approach is behind us -> (B) bails on the negative-ttc guard.
    PlayerHazard receding = assess_player_hazard(
        glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}, 0.f,
        glm::vec2{10.f, 5.f}, glm::vec2{5.f, 5.f}, t);
    REQUIRE(!receding.active);
}

// =============================================================================
// PCG-156: turn-time PATH-AWARE gate on detector (A). During a turn the physical
// heading sweeps through the corner arc, so (A)'s swept-heading leader cone rakes
// across a stopped player who is NOT on the lane the car is turning into — a
// false mid-turn brake. The production caller (ai_update_speed) resolves whether
// the player is on the destination lane and passes consider_path_leader; these
// pin the kernel half of that contract: (A) is suppressed when off, unchanged
// when on, and detector (B) (a genuine closing course) is NEVER gated.
// =============================================================================

// consider_path_leader = false suppresses detector (A): a player dead-ahead in
// the corridor — the exact case_(A) trigger from test_player_hazard_ahead_in_path
// — no longer registers, and with consider_path_leader = true (the default /
// straight-line path) the SAME player still brakes the car (behaviour unchanged).
// AI speed 0 isolates (A): with no forward speed there is no closing course, so
// detector (B) cannot mask the difference.
void test_player_hazard_turn_gate_suppresses_case_a() {
    PlayerHazardTuning t;
    const glm::vec2 ai{0.f, 0.f}, fwd{1.f, 0.f};
    const glm::vec2 pp{12.f, 0.f}, pv{0.f, 0.f};   // stationary, straight ahead

    // On-path / straight driving (flag true): braked, gap 8 — the shipped result.
    PlayerHazard on = assess_player_hazard(ai, fwd, 0.f, pp, pv, t, 4.0f, true);
    REQUIRE(on.active);
    REQUIRE(std::abs(on.gap - 8.f) < 1e-3f);

    // Off-path mid-turn (flag false): detector (A) suppressed -> no hazard.
    PlayerHazard off = assess_player_hazard(ai, fwd, 0.f, pp, pv, t, 4.0f, false);
    REQUIRE(!off.active);
    REQUIRE(std::isinf(off.gap));
}

// Detector (B) is INDEPENDENT of the gate: even with case (A) suppressed
// (consider_path_leader = false), a car driving toward a stopped player dead in
// its instantaneous path still brakes for the predicted collision. Here BOTH
// detectors would fire (player straight ahead AND a closing course), so an active
// result with the flag OFF proves it came from (B) — a real "about to hit them"
// is never weakened by the turn gate.
void test_player_hazard_turn_gate_keeps_case_b() {
    PlayerHazardTuning t;
    // AI rolling at 6 m/s straight at a stationary player 10 m ahead. ttc =
    // 10/6 = 1.67 s < ttc_horizon, closest approach 0 < collision_r -> (B) fires
    // with gap = 6*1.67 - 4 = 6 m; leader_speed 0 (brake toward the point).
    PlayerHazard h = assess_player_hazard(
        glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}, 6.f,
        glm::vec2{10.f, 0.f}, glm::vec2{0.f, 0.f}, t, 4.0f,
        /*consider_path_leader=*/false);
    REQUIRE(h.active);                          // (B) survived the (A) suppression
    REQUIRE(std::abs(h.gap - 6.f) < 1e-3f);
    REQUIRE(h.leader_speed == 0.f);
}

// =============================================================================
// PCG-134: assess_player_hazard with the ON-FOOT tuning — AI cars brake/yield for
// the WALKING player. Same assessor as PCG-006, but TrafficTuning::
// player_hazard_on_foot narrows the corridor from a 2.6 m car to a 1.5 m person
// (car half-width 1.0 + player radius 0.4) so a car brakes for someone in its
// swept path but NOT for the far sidewalk. An ACTIVE hazard with a finite gap is
// precisely the virtual-leader brake term folded into the IDM's std::min in
// ai_update_speed (a_idm = min(a_idm, idm_accel(.., ph.gap, ..))) — so pinning
// "active + finite gap when ahead-in-corridor; inactive off-path/behind" pins the
// braking input the same way the PCG-006 tests do. (idm_accel lives in
// traffic_drive.cpp, a TU this lightweight target doesn't link.)
// =============================================================================

// A player standing straight ahead IN the car's swept path is a hazard: the
// narrow on-foot corridor (1.5 m) still catches dead-centre, the bumper gap is
// fwd_dist - car_length, and a stationary player reports leader_speed 0 — the
// IDM then brakes for them as a stopped virtual leader and stops short.
void test_player_hazard_on_foot_ahead_in_path() {
    TrafficTuning td;
    const PlayerHazardTuning& t = td.player_hazard_on_foot;
    PlayerHazard h = assess_player_hazard(
        glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}, 10.f,
        glm::vec2{10.f, 0.f}, glm::vec2{0.f, 0.f}, t);
    REQUIRE(h.active);
    REQUIRE(std::abs(h.gap - 6.f) < 1e-3f);   // 10 m centre-to-centre - 4 m car
    REQUIRE(h.leader_speed == 0.f);           // stationary pedestrian
}

// The trigger cone is now NARROW (PCG-143): half_width 1.5 m == the car+player
// collision width, measured from the car's driving line. So the car reacts only
// to someone in its actual path, not to anyone loosely "in the road":
//   * 1.0 m lateral = directly in the car's path -> ACTIVE (it would hit them).
//   * 2.0 m lateral = the road centre, off the car's line -> IGNORED now (you can
//     stand just off the driving line and traffic flows past). This intentionally
//     reverses the earlier 2.5 m widening, per the "run in front" direction.
//   * 3.5 m lateral = the near sidewalk -> IGNORED (as before).
void test_player_hazard_on_foot_narrow_cone() {
    TrafficTuning td;
    PlayerHazard in_path = assess_player_hazard(
        glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}, 10.f,
        glm::vec2{10.f, 1.f}, glm::vec2{0.f, 0.f}, td.player_hazard_on_foot);
    REQUIRE(in_path.active);                    // directly in the path: reacts

    PlayerHazard road_centre = assess_player_hazard(
        glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}, 10.f,
        glm::vec2{10.f, 2.f}, glm::vec2{0.f, 0.f}, td.player_hazard_on_foot);
    REQUIRE(!road_centre.active);               // off the driving line: ignored now

    PlayerHazard sidewalk = assess_player_hazard(
        glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}, 10.f,
        glm::vec2{10.f, 3.5f}, glm::vec2{0.f, 0.f}, td.player_hazard_on_foot);
    REQUIRE(!sidewalk.active);                  // sidewalk: ignored
}

// A player BEHIND the car (behind the bumper, moving-away closest approach in the
// past) is never a hazard: detector (A) rejects the negative fwd_dist, detector
// (B) rejects the negative ttc. A car shouldn't brake for a pedestrian it has
// already passed.
void test_player_hazard_on_foot_behind_ignored() {
    TrafficTuning td;
    PlayerHazard h = assess_player_hazard(
        glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}, 10.f,
        glm::vec2{-6.f, 0.f}, glm::vec2{0.f, 0.f}, td.player_hazard_on_foot);
    REQUIRE(!h.active);
}

// The knockdown-composition input: a player DARTING off the kerb into the path
// (just outside the corridor but on a closing collision course) is caught by
// detector (B) as an active hazard with a SHORT brake gap (~4 m here, inside the
// ~9 m stopping distance at 12 m/s). The IDM brakes as hard as it can, but the
// clamp in ai_update_speed means it can't stop in time and keeps rolling — and
// the PCG-132/133 player_car_hit::evaluate path then fires the knockdown. This
// pins the hazard INPUT to that emergent composition.
void test_player_hazard_on_foot_darting_in() {
    TrafficTuning td;
    PlayerHazard h = assess_player_hazard(
        glm::vec2{0.f, 0.f}, glm::vec2{1.f, 0.f}, 12.f,
        glm::vec2{8.f, 1.6f}, glm::vec2{0.f, -3.f}, td.player_hazard_on_foot);
    REQUIRE(h.active);
    REQUIRE(std::isfinite(h.gap));
    REQUIRE(h.gap < 6.f);                     // short gap -> can't stop in time
    REQUIRE(h.leader_speed == 0.f);           // brake toward the conflict point
}

// =============================================================================
// PCG-143: person_brake_accel — the SHARP emergency brake for a person in the
// path (vs the smooth IDM glide). Coast until a hard stop is genuinely needed,
// then command exactly that decel, capped. + recoil_step, the nose-dive spring.
// =============================================================================

// The reaction window is short: a person beyond react_base + react_per_mps*v is
// ignored (cruise). At 12 m/s the window is ~5 m, so a person 30 m out — or even
// 8 m out — gets NO brake. This is the "less responsive" behaviour the founder
// asked for (was reacting ~12 m out).
void test_person_brake_ignores_person_beyond_window() {
    PersonBrakeTuning t;                       // base 2, per_mps 0.25
    REQUIRE(person_brake_accel(12.f, 30.f, t) == 0.f);  // window ~5 m, 30 m -> cruise
    REQUIRE(person_brake_accel(12.f, 8.f,  t) == 0.f);  // even 8 m out -> cruise
    // A SLOW car has an even shorter window (~3 m at 4 m/s): 6 m out -> cruise.
    REQUIRE(person_brake_accel(4.f, 6.f, t) == 0.f);
}

// The window scales with speed: at the SAME gap a fast car reacts where a slow
// car still cruises. gap 6 m: ignored at 4 m/s (window ~3 m) but braked at 16 m/s
// (window ~6 m). "Maybe more dependent on speed of the car."
void test_person_brake_window_scales_with_speed() {
    PersonBrakeTuning t;
    REQUIRE(person_brake_accel(4.f,  6.f, t) == 0.f);   // slow: 6 m > ~3 m window
    REQUIRE(person_brake_accel(16.f, 6.f, t) <  0.f);   // fast: 6 m <= ~6 m window
}

// Once inside the window it slams the brakes — the decel to stop at the standoff,
// capped at max_decel (no unbounded teleport-decel). At cruise the window is so
// short the brake is always at the cap.
void test_person_brake_slams_when_inside_window() {
    PersonBrakeTuning t;
    // v=12, gap=3 (<= ~5 m window) -> g=1, a_req=144/2=72 -> capped at -10.
    REQUIRE(std::fabs(person_brake_accel(12.f, 3.f, t) - (-10.f)) < 1e-3f);
    REQUIRE(person_brake_accel(12.f, 1.5f, t) == -10.f);   // inside standoff: full stop
    // Uncapped formula at low speed: v=4, gap=2.5 (window ~3 m) -> g=0.5,
    // a_req = 16/1 = 16 -> still capped... use gap 3 for an uncapped sample:
    // v=4, gap=3 -> window ~3 m engages, g=1, a_req=16/2=8 -> -8.
    REQUIRE(std::fabs(person_brake_accel(4.f, 3.f, t) - (-8.f)) < 1e-3f);
}

// The recoil spring dives the nose under a hard decel, then OVERSHOOTS level on
// release (underdamped) — the bounce — and ultimately settles back near zero.
void test_recoil_dives_then_bounces_then_settles() {
    RecoilTuning t;
    float pitch = 0.f, vel = 0.f;
    const float dt = 1.f / 60.f;
    // Brake hard for ~0.25 s: the nose pitches down (positive).
    for (int i = 0; i < 15; ++i) recoil_step(pitch, vel, 10.f, dt, t);
    REQUIRE(pitch > 0.02f);                    // visibly dived
    // Release (decel 0): the underdamped return overshoots past level at some
    // frame (nose tips up == negative) — the bounce.
    bool overshot = false;
    for (int i = 0; i < 120 && !overshot; ++i) {
        recoil_step(pitch, vel, 0.f, dt, t);
        if (pitch < -1e-3f) overshot = true;
    }
    REQUIRE(overshot);
    // And it does not run away: keep stepping, it settles near level.
    for (int i = 0; i < 600; ++i) recoil_step(pitch, vel, 0.f, dt, t);
    REQUIRE(std::fabs(pitch) < 1e-3f);
}

// =============================================================================
// PCG-022: honk_should_fire — the debounce that turns the per-frame ai_honking
// level into one-shot honk EVENTS. Pin the rising-edge behaviour and the min
// re-honk interval so a sustained jam / flickering trigger can't machine-gun.
// =============================================================================

// Rising edge fires once; the same flag held true does NOT re-fire (we honk on
// edges, not periodically) even long past the interval — a steady hold is one
// honk event.
void test_honk_debounce_rising_edge_only() {
    HonkDebounceState st;
    REQUIRE(honk_should_fire(true, 0.0, 1.5, st));      // false->true: honk
    REQUIRE(!honk_should_fire(true, 0.1, 1.5, st));     // still held: silent
    REQUIRE(!honk_should_fire(true, 1.0, 1.5, st));     // still held: silent
    REQUIRE(!honk_should_fire(true, 10.0, 1.5, st));    // held past interval: silent
    REQUIRE(!honk_should_fire(true, 100.0, 1.5, st));   // ...still silent
}

// The min-interval guard: after a honk, a flag that drops and re-raises within
// the interval is suppressed (the flicker / machine-gun guard); a re-raise once
// the interval has elapsed honks again. Interval is measured from the last HONK.
void test_honk_debounce_min_interval() {
    HonkDebounceState st;
    REQUIRE(honk_should_fire(true, 0.0, 1.5, st));      // honk at t=0

    REQUIRE(!honk_should_fire(false, 1.0, 1.5, st));    // falling edge
    REQUIRE(!honk_should_fire(true, 1.2, 1.5, st));     // re-raise 1.2 s after honk
                                                        //   (< 1.5): suppressed
    REQUIRE(!honk_should_fire(false, 1.3, 1.5, st));    // falling edge
    REQUIRE(honk_should_fire(true, 2.0, 1.5, st));      // re-raise 2.0 s after honk
                                                        //   (>= 1.5): honk
}

// A flag that is never true never honks.
void test_honk_debounce_never_honks_when_idle() {
    HonkDebounceState st;
    REQUIRE(!honk_should_fire(false, 0.0, 1.5, st));
    REQUIRE(!honk_should_fire(false, 50.0, 1.5, st));
    REQUIRE(!honk_should_fire(false, 100.0, 1.5, st));
}

// =============================================================================
// PCG-007: panic_should_trigger + panic_tick — the reckless-player startle/flee
// state machine, factored out as pure helpers (like assess_player_hazard /
// honk_should_fire). The trigger pins the close-AND-fast gate; panic_tick pins
// the Startle->Flee->None lifecycle, the deterministic decay (never stuck), and
// that a sustained menace keeps the car fleeing rather than re-jolting.
// =============================================================================

// The trigger needs BOTH: a close hazard (gap <= trigger_gap) AND a fast mutual
// closing (>= trigger_closing). A close-but-slow approach (the AI calmly braking
// for a stopped player) does NOT panic; a fast-but-far player does not either; an
// inactive hazard never does. The gates are inclusive (>= / <=) at the threshold.
void test_panic_trigger_gates() {
    PanicTuning t;   // trigger_gap 7, trigger_closing 9

    PlayerHazard close;
    close.active = true;
    close.gap    = 3.f;
    REQUIRE(panic_should_trigger(close, 12.f, t));    // close + fast: panic
    REQUIRE(!panic_should_trigger(close, 4.f, t));    // close but slow: no panic

    PlayerHazard far;
    far.active = true;
    far.gap    = 12.f;
    REQUIRE(!panic_should_trigger(far, 20.f, t));     // fast but far: no panic

    PlayerHazard inactive;                            // active == false, gap == +inf
    REQUIRE(!panic_should_trigger(inactive, 30.f, t));

    // Exactly at both thresholds trips (inclusive gates).
    PlayerHazard edge;
    edge.active = true;
    edge.gap    = t.trigger_gap;
    REQUIRE(panic_should_trigger(edge, t.trigger_closing, t));
}

// The panic lifecycle: idle stays None with a zeroed timer; a trigger from idle
// opens in Startle with the timer armed; with no further trigger it escalates to
// Flee and then decays cleanly to None — a deterministic, bounded return.
void test_panic_tick_startle_flee_return() {
    PanicTuning t;          // duration 2.5, startle_frac 0.35
    const float dt = 1.f / 60.f;
    float timer = 0.f;

    // Idle: no trigger -> None, timer untouched.
    REQUIRE(panic_tick(timer, false, dt, t) == PanicPhase::None);
    REQUIRE(timer == 0.f);

    // Trigger from idle: opens in Startle, timer armed near the full duration.
    REQUIRE(panic_tick(timer, true, dt, t) == PanicPhase::Startle);
    REQUIRE(timer > 0.f);
    REQUIRE(timer <= t.duration);

    // Decay with no further trigger: must pass through Flee, then reach None.
    bool saw_startle_then = false, saw_flee = false;
    PanicPhase ph = PanicPhase::Startle;
    for (int i = 0; i < 10000 && timer > 0.f; ++i) {
        ph = panic_tick(timer, false, dt, t);
        if (ph == PanicPhase::Startle) saw_startle_then = true;
        if (ph == PanicPhase::Flee)    saw_flee = true;
    }
    REQUIRE(saw_startle_then);          // startle persisted for its window
    REQUIRE(saw_flee);                  // escalated to flee
    REQUIRE(timer == 0.f);             // decayed all the way down
    // Clean return: a further idle tick stays None.
    REQUIRE(panic_tick(timer, false, dt, t) == PanicPhase::None);
}

// Never stuck: a sustained every-frame trigger keeps the car panicking but
// STABILISES in Flee (a fleeing car keeps fleeing, it does not re-jolt into
// startle each frame). The moment the menace stops, the timer decays to zero
// within ~duration — there is no way to wedge a car permanently in Panic.
void test_panic_tick_sustained_then_clears() {
    PanicTuning t;
    const float dt = 1.f / 60.f;
    float timer = 0.f;

    panic_tick(timer, true, dt, t);     // open the panic (startle)
    PanicPhase ph = PanicPhase::Startle;
    for (int i = 0; i < 600; ++i) ph = panic_tick(timer, true, dt, t);
    REQUIRE(ph == PanicPhase::Flee);    // sustained menace -> sustained flee
    REQUIRE(timer > 0.f);              // still panicking while menaced

    // Menace stops: bounded, deterministic decay to None.
    int frames = 0;
    const int cap = static_cast<int>((t.duration + 0.5f) * 60.f);
    while (panic_tick(timer, false, dt, t) != PanicPhase::None) {
        REQUIRE(++frames <= cap);       // can't outlast the duration -> never stuck
    }
    REQUIRE(timer == 0.f);
}

// =============================================================================
// PCG-244 — yield to emergency vehicles: the classification kernel + the
// per-car resume latch (stagger hysteresis). Geometry convention: vec2 == world
// XZ, ego at the origin facing +x unless stated.
// =============================================================================

// Same direction, responder closing from behind -> PullOverRight; a responder
// we are BEHIND (already ahead of us, driving away) -> None.
void test_emergency_yield_same_direction() {
    const EmergencyYieldTuning t{};   // detect 42, resume_behind 15
    const glm::vec2 ego{0.f, 0.f}, fwd{1.f, 0.f};

    EmergencyResponderView behind;
    behind.pos = {-25.f, 0.f};
    behind.vel = {22.f, 0.f};          // same heading, closing from behind
    REQUIRE(emergency_yield_classify(ego, fwd, false, false, behind, t)
            == EmergencyYield::PullOverRight);

    EmergencyResponderView ahead = behind;
    ahead.pos = {25.f, 0.f};           // we are behind the responder
    REQUIRE(emergency_yield_classify(ego, fwd, false, false, ahead, t)
            == EmergencyYield::None);

    // Out of detection range entirely -> None.
    EmergencyResponderView far = behind;
    far.pos = {-50.f, 0.f};
    REQUIRE(emergency_yield_classify(ego, fwd, false, false, far, t)
            == EmergencyYield::None);
}

// Oncoming responder ahead -> SlowEdgeRight; once it has crossed and is past
// the resume margin behind us -> None (with the hysteresis window in between).
void test_emergency_yield_oncoming() {
    const EmergencyYieldTuning t{};
    const glm::vec2 ego{0.f, 0.f}, fwd{1.f, 0.f};

    EmergencyResponderView on;
    on.pos = {30.f, 3.f};              // ahead, one lane over
    on.vel = {-20.f, 0.f};             // head-on
    REQUIRE(emergency_yield_classify(ego, fwd, false, false, on, t)
            == EmergencyYield::SlowEdgeRight);

    on.pos = {-10.f, 3.f};             // just crossed: inside the resume margin
    REQUIRE(emergency_yield_classify(ego, fwd, false, false, on, t)
            == EmergencyYield::SlowEdgeRight);

    on.pos = {-16.f, 3.f};             // past the margin: encounter over
    REQUIRE(emergency_yield_classify(ego, fwd, false, false, on, t)
            == EmergencyYield::None);
}

// The classify-side release hysteresis for the same-direction case: a responder
// that has PASSED keeps the yield until it is resume_behind_m ahead.
void test_emergency_yield_pass_hysteresis() {
    const EmergencyYieldTuning t{};
    const glm::vec2 ego{0.f, 0.f}, fwd{1.f, 0.f};

    EmergencyResponderView r;
    r.vel = {22.f, 0.f};
    r.pos = {10.f, 0.f};               // just past us, within the margin
    REQUIRE(emergency_yield_classify(ego, fwd, false, false, r, t)
            == EmergencyYield::PullOverRight);
    r.pos = {t.resume_behind_m + 1.f, 0.f};   // beyond the margin -> release
    REQUIRE(emergency_yield_classify(ego, fwd, false, false, r, t)
            == EmergencyYield::None);
}

// Gates: a responder on a parallel street (lateral gate), a crossing responder
// (junction negotiation owns it), a stopped/dwelling responder, and a
// degenerate ego heading all classify None.
void test_emergency_yield_gates() {
    const EmergencyYieldTuning t{};   // lateral_gate 10, min_responder_speed 3
    const glm::vec2 ego{0.f, 0.f}, fwd{1.f, 0.f};

    EmergencyResponderView par;        // parallel street, a block over
    par.pos = {-20.f, 12.f};
    par.vel = {22.f, 0.f};
    REQUIRE(emergency_yield_classify(ego, fwd, false, false, par, t)
            == EmergencyYield::None);

    EmergencyResponderView cross;      // perpendicular heading: crossing
    cross.pos = {15.f, 5.f};
    cross.vel = {0.f, 20.f};
    REQUIRE(emergency_yield_classify(ego, fwd, false, false, cross, t)
            == EmergencyYield::None);

    EmergencyResponderView dwell;      // on-scene ambulance: essentially stopped
    dwell.pos = {-20.f, 0.f};
    dwell.vel = {0.5f, 0.f};
    REQUIRE(emergency_yield_classify(ego, fwd, false, false, dwell, t)
            == EmergencyYield::None);

    EmergencyResponderView ok;         // sanity: the same geometry does yield
    ok.pos = {-20.f, 0.f};
    ok.vel = {22.f, 0.f};
    REQUIRE(emergency_yield_classify(ego, fwd, false, false, ok, t)
            == EmergencyYield::PullOverRight);
    REQUIRE(emergency_yield_classify(ego, {0.f, 0.f}, false, false, ok, t)
            == EmergencyYield::None);  // degenerate ego heading
}

// Ego context: mid-junction suppresses a NEW yield (finish the traversal
// first); a car queued at a light maps to HoldQueued (stay put, no kerb dart).
void test_emergency_yield_junction_and_queue() {
    const EmergencyYieldTuning t{};
    const glm::vec2 ego{0.f, 0.f}, fwd{1.f, 0.f};

    EmergencyResponderView r;
    r.pos = {-20.f, 0.f};
    r.vel = {22.f, 0.f};
    REQUIRE(emergency_yield_classify(ego, fwd, /*in_junction=*/true, false, r, t)
            == EmergencyYield::None);
    REQUIRE(emergency_yield_classify(ego, fwd, false, /*queued=*/true, r, t)
            == EmergencyYield::HoldQueued);
}

// The resume latch: a live classification holds; once it drops, the latch
// persists for the per-car stagger and then releases — and a responder
// reappearing mid-countdown re-arms it.
void test_emergency_yield_resume_latch() {
    const float dt = 1.f / 60.f;
    EmergencyYield latched = EmergencyYield::None;
    float resume_s = 0.f;

    // Idle stays None.
    REQUIRE(emergency_yield_tick(latched, resume_s, EmergencyYield::None,
                                 1.5f, dt) == EmergencyYield::None);

    // Live classification latches and re-arms every frame.
    for (int i = 0; i < 60; ++i)
        REQUIRE(emergency_yield_tick(latched, resume_s,
                                     EmergencyYield::PullOverRight, 1.5f, dt)
                == EmergencyYield::PullOverRight);

    // Classification drops: the latch holds ~stagger seconds, then releases.
    int held = 0;
    while (emergency_yield_tick(latched, resume_s, EmergencyYield::None,
                                1.5f, dt) != EmergencyYield::None) {
        REQUIRE(++held <= 95);          // ~1.5 s at 60 Hz, +margin: bounded
    }
    REQUIRE(held >= 85);                // ...but it did genuinely hold
    REQUIRE(latched == EmergencyYield::None);

    // Re-trigger mid-countdown re-arms the full stagger.
    emergency_yield_tick(latched, resume_s, EmergencyYield::SlowEdgeRight,
                         1.5f, dt);
    for (int i = 0; i < 30; ++i)
        emergency_yield_tick(latched, resume_s, EmergencyYield::None, 1.5f, dt);
    REQUIRE(latched == EmergencyYield::SlowEdgeRight);   // still inside stagger
    emergency_yield_tick(latched, resume_s, EmergencyYield::SlowEdgeRight,
                         1.5f, dt);
    REQUIRE(resume_s >= 1.5f - 1e-4f);                   // re-armed in full
}

// The stagger hash: always inside [min, max), and neighbouring positions land
// on different delays (the progressive un-freeze).
void test_emergency_yield_stagger_spread() {
    const EmergencyYieldTuning t{};   // band [1, 2)
    float lo = 1e9f, hi = -1e9f;
    float prev = -1.f;
    bool  varied = false;
    for (int i = 0; i < 32; ++i) {
        const glm::vec2 p{10.f + 3.7f * static_cast<float>(i),
                          -40.f + 5.1f * static_cast<float>(i)};
        const float s = emergency_yield_stagger(p, t);
        REQUIRE(s >= t.resume_stagger_min_s);
        REQUIRE(s < t.resume_stagger_max_s + 1e-4f);
        lo = std::min(lo, s);
        hi = std::max(hi, s);
        if (prev >= 0.f && std::abs(s - prev) > 1e-3f) varied = true;
        prev = s;
    }
    REQUIRE(varied);            // not a constant
    REQUIRE(hi - lo > 0.3f);    // real spread across the band
}

// =============================================================================
// PCG-038: go-around steered-arc kinematics. PCG-008 executed the chosen lateral
// move by translating the body sideways on a wall-clock ramp and facing it
// down-lane with a clamped lean (slide + crab). These pin the model-level fix:
// lateral rate bounded by speed*tan(steer) (ZERO at zero speed — the no-slide
// invariant), the S-curve offset over forward travel, the coupled+bounded advance
// (a stopped car can't shift; a rolling one arcs within the lock and reaches the
// target), and heading-from-true-path-tangent (which reduces EXACTLY to the
// forward tangent when there's no lateral motion — the no-regression case).
// =============================================================================

void test_go_around_max_lateral_rate() {
    const float steer = glm::radians(35.f);
    // No-slide invariant: a stopped car can achieve ZERO lateral rate, and a
    // negative speed clamps to zero (never a phantom sideways crawl).
    REQUIRE(go_around_max_lateral_rate(0.f, steer) == 0.f);
    REQUIRE(go_around_max_lateral_rate(-3.f, steer) == 0.f);
    // Equals speed*tan(steer), and grows with speed.
    REQUIRE(std::abs(go_around_max_lateral_rate(10.f, steer) - 10.f * std::tan(steer))
            < 1e-4f);
    REQUIRE(go_around_max_lateral_rate(20.f, steer)
            > go_around_max_lateral_rate(10.f, steer));
    // A looser (larger) steering lock allows more lateral rate at the same speed.
    REQUIRE(go_around_max_lateral_rate(10.f, glm::radians(45.f))
            > go_around_max_lateral_rate(10.f, glm::radians(20.f)));
}

void test_go_around_shift_offset_s_curve() {
    const float from = 0.f, target = -4.f;
    // Endpoints exact; clamps outside [0,1].
    REQUIRE(go_around_shift_offset(from, target, 0.f) == from);
    REQUIRE(go_around_shift_offset(from, target, 1.f) == target);
    REQUIRE(go_around_shift_offset(from, target, -0.5f) == from);
    REQUIRE(go_around_shift_offset(from, target, 1.5f) == target);
    // Monotone toward the target across the whole sweep.
    float prev = from;
    for (float p = 0.f; p <= 1.0001f; p += 0.1f) {
        float v = go_around_shift_offset(from, target, p);
        REQUIRE(std::abs(v - from) >= std::abs(prev - from) - 1e-5f);
        prev = v;
    }
    // Eased at the ends: a 0.1-phase step near a boundary moves LESS offset than a
    // 0.1-phase step through the middle (ease-in / ease-out, not a linear scoot).
    const float end_step = std::abs(go_around_shift_offset(from, target, 0.1f)
                                    - go_around_shift_offset(from, target, 0.0f));
    const float mid_step = std::abs(go_around_shift_offset(from, target, 0.55f)
                                    - go_around_shift_offset(from, target, 0.45f));
    REQUIRE(mid_step > end_step);
    // Symmetric return: the back-shift (target -> from) mirrors the out-shift at
    // the midpoint.
    REQUIRE(std::abs(go_around_shift_offset(target, from, 0.5f)
                     - go_around_shift_offset(from, target, 0.5f)) < 1e-5f);
}

void test_go_around_advance_no_slide_and_bound() {
    const float steer = glm::radians(35.f);
    const float dt = 1.f / 60.f, shift = 1.6f, minspd = 1.5f;

    // A stopped (or sub-min-speed) car makes ZERO lateral progress, however long it
    // sits — it must accelerate first. Phase stays put too (no creeping the S-curve).
    {
        float off = 0.f, phase = 0.f;
        for (int i = 0; i < 600; ++i)
            off = go_around_advance_offset(off, phase, 0.f, -4.f, 0.f, dt, steer,
                                           shift, minspd);
        REQUIRE(off == 0.f);
        REQUIRE(phase == 0.f);
        // Just under the gate: still frozen.
        off = go_around_advance_offset(off, phase, 0.f, -4.f, minspd - 0.01f, dt,
                                       steer, shift, minspd);
        REQUIRE(off == 0.f);
    }

    // A rolling car arcs out: monotone toward the target, every frame's lateral
    // rate within speed*tan(steer), and it reaches the target.
    {
        float off = 0.f, phase = 0.f;
        const float speed = 8.f;
        const float bound = go_around_max_lateral_rate(speed, steer);
        float prev = 0.f;
        bool reached = false;
        for (int i = 0; i < 4000; ++i) {
            off = go_around_advance_offset(off, phase, 0.f, -4.f, speed, dt, steer,
                                           shift, minspd);
            REQUIRE(off <= prev + 1e-5f);                         // monotone (target < 0)
            REQUIRE(std::abs(off - prev) <= bound * dt + 1e-5f);  // steer-bounded rate
            prev = off;
            if (std::abs(off - (-4.f)) < 1e-3f) { reached = true; break; }
        }
        REQUIRE(reached);
    }

    // Steer-bounded throttling: at low speed the lock caps the lateral rate, so the
    // SAME shift takes more frames than at high speed (it can't out-steer the wheels).
    auto frames_to_complete = [&](float speed) {
        float off = 0.f, phase = 0.f;
        int i = 0;
        for (; i < 20000; ++i) {
            off = go_around_advance_offset(off, phase, 0.f, -4.f, speed, dt, steer,
                                           shift, minspd);
            if (std::abs(off - (-4.f)) < 1e-3f) break;
        }
        return i;
    };
    REQUIRE(frames_to_complete(2.f) > frames_to_complete(12.f));
}

void test_go_around_motion_dir_reduces_and_arcs() {
    const glm::vec2 fwd{1.f, 0.f};      // heading +x
    const glm::vec2 right{0.f, 1.f};    // lane normal +z (test basis)

    // No lateral motion -> heading is EXACTLY the forward tangent (the no-regression
    // reduction to plain lane-following; this is why a non-shifting car is unchanged).
    {
        glm::vec2 d = go_around_motion_dir(fwd, right, 12.f, 0.f);
        REQUIRE(std::abs(d.x - 1.f) < 1e-5f);
        REQUIRE(std::abs(d.y - 0.f) < 1e-5f);
    }
    // Lateral motion rotates the heading toward the motion, by atan(lat/speed), and
    // exactly the steering lock when the rate sits at speed*tan(lock).
    {
        const float steer = glm::radians(35.f);
        const float speed = 8.f;
        const float lat   = go_around_max_lateral_rate(speed, steer);  // at the bound
        glm::vec2 d = go_around_motion_dir(fwd, right, speed, lat);
        const float dev = std::atan2(d.y, d.x);   // deviation from +x toward +right
        REQUIRE(dev > 0.f);
        REQUIRE(std::abs(dev - steer) < 1e-4f);   // never exceeds the lock — no crab clamp needed
        // Half the lateral rate -> a smaller, still-within-lock deviation.
        glm::vec2 d2 = go_around_motion_dir(fwd, right, speed, lat * 0.5f);
        const float dev2 = std::atan2(d2.y, d2.x);
        REQUIRE(dev2 > 0.f);
        REQUIRE(dev2 < dev);
    }
    // Degenerate (stopped, no lateral) -> falls back to the forward tangent.
    {
        glm::vec2 d = go_around_motion_dir(fwd, right, 0.f, 0.f);
        REQUIRE(std::abs(d.x - 1.f) < 1e-5f);
    }
}

// =============================================================================
// The driver-profile draw, re-keyed (PENG-29).
//
// probablecause pulled a driver off a std::mt19937 held by the spawner and this
// test seeded one to match. apricot forbids that: a stream's answer depends on
// how many cars were spawned before this one, which in a streamed city is a
// function of which way the player drove in.
//
// So there are two tests now where there was one. The first pins the MIX
// against the bucketing alone, which is the part that had to survive the
// re-key. The second pins the property the re-key was FOR — that a given car
// gets the same driver no matter what was drawn in between, which is exactly
// what a stream cannot promise.
// =============================================================================

void test_driver_profile_mix_from_roll() {
    // The cut points, exercised directly on the bucketing: 18 / 60 / 16 / 6.
    REQUIRE(driver_profile_from_roll(0.00f).kind == DriverProfileKind::Cautious);
    REQUIRE(driver_profile_from_roll(0.17f).kind == DriverProfileKind::Cautious);
    REQUIRE(driver_profile_from_roll(0.18f).kind == DriverProfileKind::Normal);
    REQUIRE(driver_profile_from_roll(0.77f).kind == DriverProfileKind::Normal);
    REQUIRE(driver_profile_from_roll(0.78f).kind == DriverProfileKind::Impatient);
    REQUIRE(driver_profile_from_roll(0.93f).kind == DriverProfileKind::Impatient);
    REQUIRE(driver_profile_from_roll(0.94f).kind == DriverProfileKind::AggressiveLite);
    REQUIRE(driver_profile_from_roll(0.999f).kind == DriverProfileKind::AggressiveLite);

    // A roll of exactly 1.0 cannot happen (next_float() is [0,1)), but the
    // bucketing must not fall off the end if somebody hands it one.
    REQUIRE(driver_profile_from_roll(1.0f).kind == DriverProfileKind::AggressiveLite);
    apricot_test::pass("driver profile cut points are 18 / 78 / 94");
}

void test_driver_profile_keyed_mix_and_stability() {
    constexpr uint64_t kSeed = 0x0BADC0DE'5EED1234ull;
    constexpr int kSide = 400;   // 160,000 cars' worth of spawn cells

    int counts[4] = {0, 0, 0, 0};
    for (int32_t z = 0; z < kSide; ++z) {
        for (int32_t x = 0; x < kSide; ++x) {
            const DriverProfile p = driver_profile_for(kSeed, x, z, /*slot*/ 0u);
            ++counts[static_cast<int>(p.kind)];
        }
    }
    const float n = static_cast<float>(kSide) * static_cast<float>(kSide);
    const float cautious   = static_cast<float>(counts[0]) / n;
    const float normal     = static_cast<float>(counts[1]) / n;
    const float impatient  = static_cast<float>(counts[2]) / n;
    const float aggressive = static_cast<float>(counts[3]) / n;

    // Within 1.5 points of the configured mix. Wide enough not to be flaky on a
    // finite lattice, tight enough that a swapped bucket or a channel that has
    // stopped decorrelating shows up.
    REQUIRE_MSG(cautious   > 0.165f && cautious   < 0.195f, "cautious share",   "mix");
    REQUIRE_MSG(normal     > 0.585f && normal     < 0.615f, "normal share",     "mix");
    REQUIRE_MSG(impatient  > 0.145f && impatient  < 0.175f, "impatient share",  "mix");
    REQUIRE_MSG(aggressive > 0.045f && aggressive < 0.075f, "aggressive share", "mix");

    // Every kind actually occurs — a mix that is 100% Normal would still pass a
    // sloppier band and would be invisible in game as "traffic feels samey".
    REQUIRE(counts[0] > 0 && counts[1] > 0 && counts[2] > 0 && counts[3] > 0);

    // THE POINT OF THE RE-KEY. Re-ask for one car's driver after thousands of
    // unrelated draws and get the identical answer.
    const DriverProfileKind before = driver_profile_for(kSeed, 11, -7, 2u).kind;
    for (int32_t z = -25; z < 25; ++z)
        for (int32_t x = -25; x < 25; ++x)
            (void)driver_profile_for(kSeed, x, z, 5u);
    REQUIRE(driver_profile_for(kSeed, 11, -7, 2u).kind == before);

    // A different car in the same cell is a different draw, and a different run
    // seed is a different city. Neither may echo the first.
    bool slot_differs = false;
    for (uint32_t s = 1u; s < 12u; ++s)
        if (driver_profile_for(kSeed, 11, -7, s).kind != before) slot_differs = true;
    REQUIRE(slot_differs);

    bool seed_differs = false;
    for (uint64_t d = 1u; d < 12u; ++d)
        if (driver_profile_for(kSeed + d, 11, -7, 2u).kind != before) seed_differs = true;
    REQUIRE(seed_differs);
    apricot_test::pass("the keyed driver draw holds its mix and its answers");
}

// =============================================================================
// nudge_pick_target — the static-blocker edge-around side-pick.
//
// WEAKER THAN ITS ORIGINAL, deliberately and named as such. probablecause read
// `mid_offset` off a lane built by the real producer, so the pins held at
// whatever geometry the builders actually baked. There is no lane builder here,
// so mid_offset is computed as the width/4 the builder used to bake — the
// arithmetic is pinned, the producer is not. When the lane graph lands, this
// test should go back to asking it.
//
// Body constants mirror the drive loop's NUDGE_* (blocker half 1.1, our half
// 1.0, margin 0.3).
// =============================================================================

void test_nudge_pick_target_geometry() {
    constexpr float BH = 1.1f, MH = 1.0f, MG = 0.3f;

    const float mid =
        road_type_def(RoadType::Street).carriageway_width_m * 0.25f;
    REQUIRE(mid >= 2.5f);   // every canonical paved width (>= 10 m) gives this

    // A wreck hugging the kerb: pass on the CENTRELINE side. Corridor
    // 2*mid - 1.6 >= 3.4 for any mid >= 2.5, so this is viable at every
    // canonical width; the target sits inboard of the blocker and never
    // crosses the road centreline.
    {
        const NudgePlan np = nudge_pick_target(mid - 0.5f, BH, MH, MG, mid);
        REQUIRE(np.viable);
        REQUIRE(np.target < mid - 0.5f);
        REQUIRE(np.target >= -mid - 1e-4f);
        REQUIRE(np.corridor >= RecoveryTuning{}.nudge_min_shoulder);
    }
    // A wreck hugging the centreline: pass on the KERB side; the body stays
    // on the pavement (centre <= mid - my_half).
    {
        const NudgePlan np = nudge_pick_target(-mid + 0.5f, BH, MH, MG, mid);
        REQUIRE(np.viable);
        REQUIRE(np.target > -mid + 0.5f);
        REQUIRE(np.target <= mid - MH + 1e-4f);
    }
    // Dead-centre blocker on a WIDE road (16 m -> mid 4): both sides fit; the
    // pick is the minimal deviation and respects the kerb bound.
    {
        const float wmid = 16.f * 0.25f;
        const NudgePlan np = nudge_pick_target(0.f, BH, MH, MG, wmid);
        REQUIRE(np.viable);
        REQUIRE(std::abs(np.target) <= wmid - MH + 1e-4f);
        REQUIRE(std::abs(np.target - (BH + MH + MG)) < 1e-4f);  // hugs the blocker
    }
    // Dead-centre blocker on a NARROW road (9 m -> mid 2.25): neither side fits
    // a car + margin -> NOT viable, and the drive loop falls through to holding
    // rather than wedging itself half-way past.
    {
        const float nmid = 9.f * 0.25f;
        REQUIRE(!nudge_pick_target(0.f, BH, MH, MG, nmid).viable);
    }
    apricot_test::pass("nudge picks the side that fits, or refuses");
}

}  // namespace

int main() {
    // Driver model: the four personalities and the curves they feed.
    test_driver_math();
    test_turn_classification_matrix();
    test_dir_info_consistency();
    test_follow_speed_curve();
    test_yellow_light_gate();
    test_jam_pass_thresholds();
    test_driver_profile_ordering();
    test_driver_profile_mix_from_roll();
    test_driver_profile_keyed_mix_and_stability();

    // Follow gap vs car length, and the deep-jam despawn gate.
    test_effective_min_gap_at_least_car_length();
    test_jam_despawn_gate_excludes_signal_queue();
    test_jam_despawn_last_resort_gate();
    test_recovery_telemetry_bucketing();
    test_recovery_telemetry_record();

    // The maneuver-recovery escalation kernel: decision table, bounded budgets,
    // monotone escalation, far-from-player-only GiveUp.
    test_recovery_plan_decision_table();
    test_recovery_plan_budgets_and_escalation();
    test_recovery_plan_giveup_is_far_from_player_only();

    // Permissive-left yield: turners yield to oncoming, and an opposing pair
    // resolves exactly-one-proceeds rather than wedging nose to nose.
    test_turn_yield_committed_holds();
    test_turn_yield_oncoming_rolling_gate();
    test_turn_yield_opposing_turners_exactly_one();

    // The static-blocker edge-around side-pick.
    test_nudge_pick_target_geometry();

    // Overtake gap acceptance (the collision-aware go-around).
    test_overtake_empty_lane_accepts();
    test_overtake_oncoming_ttc_gate();
    test_overtake_front_must_cover_pass_length();
    test_overtake_rear_safety_gate();

    // Player-hazard reactivity: the player as a prioritised off-corridor hazard.
    test_player_hazard_ahead_in_path();
    test_player_hazard_out_of_range();
    test_player_hazard_beside_clear();
    test_player_hazard_crossing_course();
    test_player_hazard_oncoming_in_adjacent_lane_ignored();
    test_player_hazard_honk_when_close();
    test_player_hazard_degenerate_heading();
    test_player_hazard_moving_leader_speed();
    test_player_hazard_crossing_beyond_horizon_or_receding();
    test_player_hazard_turn_gate_suppresses_case_a();
    test_player_hazard_turn_gate_keeps_case_b();
    test_player_hazard_on_foot_ahead_in_path();
    test_player_hazard_on_foot_narrow_cone();
    test_player_hazard_on_foot_behind_ignored();
    test_player_hazard_on_foot_darting_in();

    // The short, speed-scaled person reaction window, and the recoil spring.
    test_person_brake_ignores_person_beyond_window();
    test_person_brake_window_scales_with_speed();
    test_person_brake_slams_when_inside_window();
    test_recoil_dives_then_bounces_then_settles();

    // Honk debounce: rising edge only, with a minimum re-honk interval.
    test_honk_debounce_rising_edge_only();
    test_honk_debounce_min_interval();
    test_honk_debounce_never_honks_when_idle();

    // Reckless-player panic: trigger gates and the startle/flee lifecycle.
    test_panic_trigger_gates();
    test_panic_tick_startle_flee_return();
    test_panic_tick_sustained_then_clears();

    // Yield to an emergency responder, and the resume latch behind it.
    test_emergency_yield_same_direction();
    test_emergency_yield_oncoming();
    test_emergency_yield_pass_hysteresis();
    test_emergency_yield_gates();
    test_emergency_yield_junction_and_queue();
    test_emergency_yield_resume_latch();
    test_emergency_yield_stagger_spread();

    // Go-around steered-arc kinematics: the no-slide bound, the S-curve over
    // forward distance, the coupled advance and heading from the path tangent.
    test_go_around_max_lateral_rate();
    test_go_around_shift_offset_s_curve();
    test_go_around_advance_no_slide_and_bound();
    test_go_around_motion_dir_reduces_and_arcs();

    return apricot_test::done("traffic_ai_tests");
}
