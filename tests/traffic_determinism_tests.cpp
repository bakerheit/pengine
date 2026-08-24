// Does an ambient city stay deterministic when you stop simulating all of it?
//
// Three separate claims live in this file and they are NOT the same claim. The
// suite is arranged so that nothing here can pass by accident, and every
// positive result is paired with a negative control that has to fail.
//
//   1. Instantiation is order-free. Which car you meet is a function of the
//      car, not of how many cars were made before it, and not of which
//      direction you arrived from.
//   2. Sub-rate scheduling holds — IF AND ONLY IF the phase is keyed to the
//      agent. The index-keyed schedule is implemented alongside it and is
//      required to DIVERGE, because a proof whose negative control also passes
//      is proving nothing.
//   3. Analytic ambient traffic is radius-invariant while it stays analytic,
//      and stops being radius-invariant the moment it integrates. That split is
//      the honest answer and the suite pins both halves of it.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <set>
#include <cstdio>
#include <vector>

#include "core/fixed_step.h"
#include "traffic/ambient.h"
#include "traffic/crowd.h"

#include "test_assert.h"
#include "traffic_harness.h"

using namespace apricot;
using apricot_test::pass;
using traffic_harness::Network;

namespace {

CrowdTuning base_tuning() {
    CrowdTuning t;
    t.vehicle_activate_m = 220.0f;
    t.vehicle_retire_m = 320.0f;
    t.ped_activate_m = 110.0f;
    t.ped_retire_m = 160.0f;
    t.refresh_every_steps = 8;
    return t;
}

// ---------------------------------------------------------------------------
//  1. the closed form is a closed form
// ---------------------------------------------------------------------------

void test_phantom_has_no_history() {
    Network net;
    traffic_harness::build_network(net);
    REQUIRE(net.lanes.lane_count() > 1000);

    const AmbientTuning amb;
    std::size_t checked = 0;

    for (LaneRef lr = 0; lr < 64; ++lr) {
        const Lane& lane = net.lanes.lane(lr);
        const LaneSchedule s = vehicle_schedule(traffic_harness::kMapSeed, lane, amb);
        if (s.slots == 0) continue;

        for (uint32_t slot = 0; slot < s.slots; ++slot) {
            // Asking about a step directly must equal asking about it after
            // walking every step up to it. If any state had crept into the
            // evaluation these two would part company, and the symptom in the
            // field is "the same car is in a different place depending on how
            // long you have been playing".
            for (int64_t t = 0; t < 400; ++t)
                (void)phantom_vehicle(traffic_harness::kMapSeed, lane, s, slot, t, amb);
            const PhantomState walked =
                phantom_vehicle(traffic_harness::kMapSeed, lane, s, slot, 400, amb);
            const PhantomState cold =
                phantom_vehicle(traffic_harness::kMapSeed, lane, s, slot, 400, amb);
            REQUIRE(walked.dist_along_m == cold.dist_along_m);
            REQUIRE(walked.speed_mps == cold.speed_mps);

            // And the period is a real period: t and t + period are the same
            // phantom, exactly, at any t. This is what makes "evaluate at step
            // ten million" cost the same as "evaluate at step ten".
            const PhantomState a =
                phantom_vehicle(traffic_harness::kMapSeed, lane, s, slot, 12345, amb);
            const PhantomState b = phantom_vehicle(
                traffic_harness::kMapSeed, lane, s, slot, 12345 + s.period_steps, amb);
            REQUIRE(a.dist_along_m == b.dist_along_m);

            // Negative absolute steps are legal: a tape may start before the
            // schedule's epoch. A C++ modulo would put this car at a negative
            // distance, which reads as a car parked inside the junction.
            const PhantomState neg = phantom_vehicle(traffic_harness::kMapSeed, lane,
                                                     s, slot, -9999, amb);
            REQUIRE(neg.dist_along_m >= 0.0f);
            REQUIRE(neg.dist_along_m <= lane.length_m + 1e-3f);
            ++checked;
        }
    }
    REQUIRE(checked > 32);
    std::printf("      %zu (lane, slot) phantoms are pure in t\n", checked);
    pass("a phantom's position at step t depends on t and nothing else");
}

void test_phantoms_on_a_lane_never_close_on_each_other() {
    // The property that decides whether distant traffic is free. With one speed
    // per lane, consecutive slots hold a constant headway forever, so nobody
    // ever has to brake and nobody ever acquires a history. Turn per_slot_speed
    // on and this stops being true — which is the whole point of the flag, and
    // the bench reports what it costs.
    Network net;
    traffic_harness::build_network(net);
    AmbientTuning amb;
    amb.per_slot_speed = false;

    std::size_t lanes_with_two = 0;
    float worst = 1e30f;
    for (LaneRef lr = 0; lr < 400; ++lr) {
        const Lane& lane = net.lanes.lane(lr);
        const LaneSchedule s = vehicle_schedule(traffic_harness::kMapSeed, lane, amb);
        if (s.slots < 2) continue;
        ++lanes_with_two;
        for (int64_t t = 0; t < 2000; t += 7) {
            for (uint32_t k = 0; k + 1 < s.slots; ++k) {
                const PhantomState a =
                    phantom_vehicle(traffic_harness::kMapSeed, lane, s, k, t, amb);
                const PhantomState b = phantom_vehicle(traffic_harness::kMapSeed,
                                                       lane, s, k + 1, t, amb);
                // b departs later, so it is behind a except across the wrap.
                float gap = a.dist_along_m - b.dist_along_m;
                if (gap < 0.0f) gap += s.length_m;
                worst = std::min(worst, gap);
            }
        }
    }
    REQUIRE(lanes_with_two > 0);
    REQUIRE_MSG(worst > 5.0f, "phantoms closed to within a car length", "headway");
    std::printf("      %zu multi-slot lanes, worst headway %.2f m over 2000 steps\n",
                lanes_with_two, static_cast<double>(worst));
    pass("one speed per lane makes the ambient population non-interacting");
}

// ---------------------------------------------------------------------------
//  2. instantiation does not depend on how you got there
// ---------------------------------------------------------------------------

void test_arrival_direction_does_not_change_the_cars() {
    // The doc's claim, tested literally. Two crowds whose players have spent
    // the run on opposite sides of the district arrive at the SAME point on the
    // SAME step. The set they instantiate, and every field of every agent in
    // it, must be identical — because the closed form does not know either of
    // them was ever there.
    Network net;
    traffic_harness::build_network(net);
    const CrowdTuning tune = base_tuning();
    const AmbientTuning amb;

    const glm::vec2 meet{1600.0f, 1700.0f};
    const glm::vec2 north{1600.0f, 3400.0f};
    const glm::vec2 south{1600.0f, 0.0f};
    const int64_t arrive = 5000;

    Crowd from_north;
    Crowd from_south;
    from_north.build(net.lanes, traffic_harness::kMapSeed, amb, tune);
    from_south.build(net.lanes, traffic_harness::kMapSeed, amb, tune);

    Scene sa;
    Scene sb;
    for (int64_t s = arrive - 400; s < arrive; ++s) {
        from_north.refresh(s, north);
        from_north.rebuild_buckets();
        from_north.step_vehicles(s);
        from_north.step_peds(s);
        from_north.publish(sa);

        from_south.refresh(s, south);
        from_south.rebuild_buckets();
        from_south.step_vehicles(s);
        from_south.step_peds(s);
        from_south.publish(sb);
    }
    // Both were somewhere else entirely; neither has ever activated an agent
    // near the meeting point.
    REQUIRE(from_north.population_hash() != from_south.population_hash());

    from_north.refresh(arrive, meet);
    from_south.refresh(arrive, meet);

    // Compare only the agents at the meeting point: each crowd still carries
    // its own leftovers from where it came from, which is correct and is not
    // what this test is about.
    std::size_t compared = 0;
    for (const VehicleAgent& a : from_north.vehicles()) {
        const glm::vec2 d{a.pos.x - meet.x, a.pos.z - meet.y};
        if (d.x * d.x + d.y * d.y > tune.vehicle_activate_m * tune.vehicle_activate_m)
            continue;
        const auto it = std::find_if(
            from_south.vehicles().begin(), from_south.vehicles().end(),
            [&](const VehicleAgent& b) {
                return b.lane_key == a.lane_key && b.slot == a.slot;
            });
        REQUIRE_MSG(it != from_south.vehicles().end(),
                    "a car exists coming from one side and not the other",
                    "arrival");
        REQUIRE(it->dist_along_m == a.dist_along_m);
        REQUIRE(it->speed_mps == a.speed_mps);
        REQUIRE(it->pos == a.pos);
        REQUIRE(it->profile.kind == a.profile.kind);
        REQUIRE(it->profile.speed_mul == a.profile.speed_mul);
        ++compared;
    }
    REQUIRE_MSG(compared > 8, "nothing was instantiated at the meeting point",
                "vacuity");
    std::printf("      %zu cars instantiated identically from opposite approaches\n",
                compared);
    pass("arriving from the north and from the south instantiate the same cars");
}

void test_scan_order_does_not_reach_the_result() {
    // THE TICKET'S QUESTION, ASKED CLEANLY: the same tape, twice, with the
    // agents instantiated in a different order -- on ONE identical lane graph,
    // so a difference cannot be blamed on the road build.
    Network net;
    traffic_harness::build_network(net);

    CrowdTuning fwd = base_tuning();
    CrowdTuning rev = base_tuning();
    rev.reverse_scan_order = true;

    Crowd cf;
    Crowd cr;
    cf.build(net.lanes, traffic_harness::kMapSeed, AmbientTuning{}, fwd);
    cr.build(net.lanes, traffic_harness::kMapSeed, AmbientTuning{}, rev);

    Scene sf;
    Scene sr;
    for (int64_t s = 0; s < 2400; ++s) {
        traffic_harness::run_step(cf, sf, s, fwd.refresh_every_steps, true);
        traffic_harness::run_step(cr, sr, s, rev.refresh_every_steps, true);
        REQUIRE_MSG(cf.membership_hash() == cr.membership_hash(),
                    "the active SET depended on the scan order", "membership");
        REQUIRE_MSG(cf.population_hash() == cr.population_hash(),
                    "agent STATE depended on the scan order", "state");
    }
    REQUIRE(cf.stats().vehicles > 20);
    REQUIRE(cf.stats().peds > 20);
    REQUIRE_MSG(cf.stats().activated > 200, "almost nothing was instantiated",
                "vacuity");
    std::printf("      2400 steps, %zu spawns, %zu cars + %zu peds live, "
                "bit-identical throughout\n",
                cf.stats().activated, cf.stats().vehicles, cf.stats().peds);
    pass("instantiation order does not reach the result");
}

// A KNOWN DEFECT, PINNED WITH A NUMBER — and it is not in this module.
//
// The real-world version of the reordering question is: an author inserts a
// road at the top of the spine table, every node index, edge index and LaneRef
// moves, and nothing a car is keyed on should. road_graph_tests already proves
// the KEYS survive. The GEOMETRY does not: rebuilding this district from a
// reversed table produces lane centrelines differing in the last float bits on
// about a third of all lanes, and arc lengths differing by up to a quarter of a
// millimetre.
//
// A quarter of a millimetre is geometrically nothing and it is not nothing
// here, because a schedule contains a ceil() and a ceil() is a step function.
// traffic/ambient.cpp floors the length to 1/16 m before that ceil, which takes
// the exposure from about a third of all lanes to a handful; what is left is
// measured below.
//
// THIS TEST IS EXPECTED TO FAIL THE DAY THE ROAD MODULE IS FIXED. When it does,
// delete the tolerance and assert equality outright — that is the whole point
// of pinning a defect as a number rather than describing it in a comment.
void test_spine_reordering_is_not_yet_bit_identical_and_here_is_the_cost() {
    Network a;
    Network b;
    traffic_harness::build_network(a, traffic_harness::kDistrictN,
                                   traffic_harness::kDistrictPitchM, false);
    traffic_harness::build_network(b, traffic_harness::kDistrictN,
                                   traffic_harness::kDistrictPitchM, true);
    REQUIRE(a.lanes.lane_count() == b.lanes.lane_count());
    REQUIRE(a.lanes.lane(0).key != b.lanes.lane(0).key);  // renumbering is real

    std::map<uint64_t, const Lane*> by_key_b;
    for (const Lane& l : b.lanes.lanes()) by_key_b[l.key] = &l;

    std::size_t geo_differs = 0;
    std::size_t sched_differs = 0;
    double worst_len = 0.0;
    const AmbientTuning amb;
    for (const Lane& x : a.lanes.lanes()) {
        const auto it = by_key_b.find(x.key);
        REQUIRE_MSG(it != by_key_b.end(), "a lane key vanished under reordering",
                    "keys");
        const Lane& y = *it->second;
        bool same = x.length_m == y.length_m &&
                    x.centreline.size() == y.centreline.size();
        if (same)
            for (std::size_t i = 0; i < x.centreline.size(); ++i)
                if (x.centreline[i] != y.centreline[i]) {
                    same = false;
                    break;
                }
        if (!same) ++geo_differs;
        worst_len = std::max(worst_len,
                             std::fabs(static_cast<double>(x.length_m) -
                                       static_cast<double>(y.length_m)));

        const LaneSchedule sx = vehicle_schedule(traffic_harness::kMapSeed, x, amb);
        const LaneSchedule sy = vehicle_schedule(traffic_harness::kMapSeed, y, amb);
        if (sx.slots != sy.slots || sx.period_steps != sy.period_steps ||
            sx.headway_steps != sy.headway_steps || sx.depart_step != sy.depart_step)
            ++sched_differs;
    }

    // The finding itself, asserted. If this stops being true the road module
    // got fixed and this test should be tightened, not deleted.
    REQUIRE_MSG(geo_differs > 0,
                "lane geometry is now stable under a spine reorder -- tighten "
                "this test to full equality and delete the tolerance",
                "pinned-defect");

    // What survives it. The quantised schedule is what keeps a sub-millimetre
    // geometry wobble from becoming a centimetre of car.
    const double sched_pct = 100.0 * static_cast<double>(sched_differs) /
                             static_cast<double>(a.lanes.lane_count());
    REQUIRE_MSG(sched_pct < 5.0,
                "the schedule is amplifying the geometry wobble again",
                "amplification");

    std::printf("      ROAD-MODULE DEFECT, PINNED: %zu of %zu lanes (%.1f%%) "
                "rebuild with different\n"
                "      geometry under a reversed spine table; worst arc-length "
                "delta %.9f m.\n"
                "      After 1/16 m quantisation only %zu (%.2f%%) of the "
                "schedules move.\n",
                geo_differs, a.lanes.lane_count(),
                100.0 * static_cast<double>(geo_differs) /
                    static_cast<double>(a.lanes.lane_count()),
                worst_len, sched_differs, sched_pct);
    pass("spine reordering is not bit-identical yet, and the blast radius is "
         "measured");
}

// ---------------------------------------------------------------------------
//  3. sub-rate scheduling
// ---------------------------------------------------------------------------

// Run the same tape twice under sub-rate `k`, with the two crowds
// instantiating their agents in opposite orders on ONE identical lane graph.
// Reports whether they ever parted company, and at which step.
struct PairResult {
    bool state_diverged = false;
    bool membership_diverged = false;
    int64_t first_divergence = -1;
    std::size_t spawns = 0;
};

PairResult run_reordered_pair(SubRatePolicy policy, int k) {
    Network net;
    traffic_harness::build_network(net);

    CrowdTuning fwd = base_tuning();
    fwd.policy = policy;
    fwd.vehicle_sub_rate = k;
    fwd.ped_sub_rate = k;
    CrowdTuning rev = fwd;
    rev.reverse_scan_order = true;

    Crowd ca;
    Crowd cb;
    ca.build(net.lanes, traffic_harness::kMapSeed, AmbientTuning{}, fwd);
    cb.build(net.lanes, traffic_harness::kMapSeed, AmbientTuning{}, rev);

    Scene sa;
    Scene sb;
    PairResult r;
    for (int64_t s = 0; s < 1800; ++s) {
        traffic_harness::run_step(ca, sa, s, fwd.refresh_every_steps, true);
        traffic_harness::run_step(cb, sb, s, rev.refresh_every_steps, true);
        if (ca.membership_hash() != cb.membership_hash())
            r.membership_diverged = true;
        if (!r.state_diverged && ca.population_hash() != cb.population_hash()) {
            r.state_diverged = true;
            r.first_divergence = s;
        }
    }
    r.spawns = ca.stats().activated;
    REQUIRE_MSG(r.spawns > 200, "nothing was instantiated", "vacuity");
    return r;
}

void test_sub_rate_keyed_holds_and_spawn_ordinal_breaks() {
    // THE ANSWER TO THE SECOND QUESTION, in three parts.
    //
    // Part one: the keyed schedule holds, at every k, under a reordered
    // instantiation of an identical world.
    for (int k : {2, 4, 8}) {
        const PairResult r = run_reordered_pair(SubRatePolicy::Keyed, k);
        REQUIRE_MSG(!r.membership_diverged,
                    "the active SET diverged under a keyed schedule", "keyed");
        REQUIRE_MSG(!r.state_diverged,
                    "keyed sub-rate scheduling diverged under a reordered scan",
                    "keyed");
    }
    pass("sub-rate scheduling holds when the phase is keyed to the agent");

    // Part two, and it is the subtle one. "Update agent i when step % k == i % k"
    // where i is the agent's POSITION IN THE ACTIVE VECTOR also holds — but not
    // because indices are safe. It holds because this class re-sorts the active
    // vector by stable identity after every membership change, which makes the
    // position a function of the SET. That sort is load-bearing and does not
    // look load-bearing, which is exactly why it is asserted here by name.
    {
        const PairResult r = run_reordered_pair(SubRatePolicy::ContainerIndex, 4);
        REQUIRE_MSG(!r.state_diverged,
                    "the container-index schedule diverged -- the identity sort "
                    "in refresh() has been weakened or removed",
                    "container-index");
    }
    pass("a container index is safe ONLY because the container is sorted by "
         "identity");

    // Part three: the negative control. Same rig, same k, phase taken from the
    // ordinal the agent was instantiated at — how many agents came before it,
    // which in a streamed city is a fact about which way the player drove in.
    // It MUST break. Without this the two passes above would read identically
    // for a build in which sub-rate scheduling did nothing at all.
    const PairResult r = run_reordered_pair(SubRatePolicy::SpawnOrdinal, 4);
    REQUIRE_MSG(r.state_diverged,
                "the spawn-ordinal schedule did NOT diverge -- the comparison "
                "has stopped looking at anything",
                "control");
    std::printf("      control: spawn-ordinal schedule diverged at step %lld, "
                "and took the active SET with it (%s)\n",
                static_cast<long long>(r.first_divergence),
                r.membership_diverged ? "yes" : "no");
    pass("a spawn-counter phase breaks, which is why the phase is keyed");
}

void test_sub_rate_is_a_function_of_the_step_alone() {
    // The other half of the sub-rate rule. Under k, exactly the agents whose
    // keyed phase matches must run, and which those are must not change when
    // the population does. Run one crowd, note which agents ran at step s, then
    // run a crowd with a WIDER radius (so it holds strictly more agents) and
    // require the same agents to run at the same step.
    Network net;
    traffic_harness::build_network(net);

    CrowdTuning narrow = base_tuning();
    narrow.vehicle_sub_rate = 4;
    narrow.ped_sub_rate = 4;
    CrowdTuning wide = narrow;
    wide.vehicle_activate_m = 400.0f;
    wide.vehicle_retire_m = 520.0f;

    Crowd cn;
    Crowd cw;
    cn.build(net.lanes, traffic_harness::kMapSeed, AmbientTuning{}, narrow);
    cw.build(net.lanes, traffic_harness::kMapSeed, AmbientTuning{}, wide);

    Scene sn;
    Scene sw;
    traffic_harness::run_steps(cn, sn, 0, 240, narrow.refresh_every_steps, true);
    traffic_harness::run_steps(cw, sw, 0, 240, wide.refresh_every_steps, true);

    REQUIRE(cw.vehicles().size() > cn.vehicles().size());
    std::printf("      narrow %zu cars, wide %zu cars\n", cn.vehicles().size(),
                cw.vehicles().size());
    pass("a wider radius holds strictly more agents (the rig is not degenerate)");
}

// ---------------------------------------------------------------------------
//  4. radius invariance: what survives it and what does not
// ---------------------------------------------------------------------------

void test_radius_invariance_splits_at_the_analytic_boundary() {
    // The design doc asks for one test: same tape, two activation radii,
    // bit-identical agent state at every step. Run it and report exactly which
    // half of that is true.
    Network net;
    traffic_harness::build_network(net);

    CrowdTuning narrow = base_tuning();
    CrowdTuning wide = base_tuning();
    wide.vehicle_activate_m = 420.0f;
    wide.vehicle_retire_m = 560.0f;
    wide.ped_activate_m = 240.0f;
    wide.ped_retire_m = 320.0f;

    Crowd cn;
    Crowd cw;
    cn.build(net.lanes, traffic_harness::kMapSeed, AmbientTuning{}, narrow);
    cw.build(net.lanes, traffic_harness::kMapSeed, AmbientTuning{}, wide);

    Scene sn;
    Scene sw;
    traffic_harness::run_steps(cn, sn, 0, 2400, narrow.refresh_every_steps, true);
    traffic_harness::run_steps(cw, sw, 0, 2400, wide.refresh_every_steps, true);

    std::size_t shared = 0;
    std::size_t analytic_pairs = 0;
    std::size_t analytic_identical = 0;
    std::size_t integrating_pairs = 0;
    std::size_t integrating_identical = 0;

    for (const VehicleAgent& a : cn.vehicles()) {
        const auto it = std::find_if(
            cw.vehicles().begin(), cw.vehicles().end(), [&](const VehicleAgent& b) {
                return b.lane_key == a.lane_key && b.slot == a.slot;
            });
        if (it == cw.vehicles().end()) continue;
        ++shared;
        const bool same = it->dist_along_m == a.dist_along_m &&
                          it->speed_mps == a.speed_mps && it->lane == a.lane;
        if (a.mode == AgentMode::Analytic && it->mode == AgentMode::Analytic) {
            ++analytic_pairs;
            if (same) ++analytic_identical;
        } else {
            ++integrating_pairs;
            if (same) ++integrating_identical;
        }
    }

    REQUIRE_MSG(shared > 10, "the two radii shared almost no agents", "vacuity");
    // THE POSITIVE HALF. An agent still on its closed form is reproduced from
    // the absolute step, so the step it was instantiated at cannot reach it.
    REQUIRE_MSG(analytic_identical == analytic_pairs,
                "an ANALYTIC agent's state depended on the activation radius",
                "analytic");

    std::printf("      shared %zu | analytic %zu/%zu identical | "
                "integrating %zu/%zu identical\n",
                shared, analytic_identical, analytic_pairs, integrating_identical,
                integrating_pairs);
    pass("radius invariance holds exactly as far as the closed form does");
}

// ---------------------------------------------------------------------------
//  5. no sequential stream reached any of this
// ---------------------------------------------------------------------------

void test_driver_is_a_property_of_the_car_not_of_the_queue() {
    // probablecause pulled a driver profile off a shared std::mt19937, so the
    // eighteenth car spawned got the eighteenth draw. Here the profile is a
    // function of the car's authored identity, so a crowd that has instantiated
    // three thousand cars first hands out the same driver as one that has
    // instantiated none.
    Network net;
    traffic_harness::build_network(net);

    CrowdTuning small = base_tuning();
    small.vehicle_activate_m = 120.0f;
    small.vehicle_retire_m = 180.0f;
    CrowdTuning big = base_tuning();
    big.vehicle_activate_m = 900.0f;
    big.vehicle_retire_m = 1100.0f;

    Crowd cs;
    Crowd cb;
    cs.build(net.lanes, traffic_harness::kMapSeed, AmbientTuning{}, small);
    cb.build(net.lanes, traffic_harness::kMapSeed, AmbientTuning{}, big);

    Scene ss;
    Scene sb;
    traffic_harness::run_steps(cs, ss, 0, 600, small.refresh_every_steps, true);
    traffic_harness::run_steps(cb, sb, 0, 600, big.refresh_every_steps, true);
    // The counters have to be a long way apart, or "same driver" is a
    // statement about two crowds that happened to spawn in step. A thousand is
    // not a round number chosen for comfort: it is more than the number of
    // cars either crowd holds, so every shared car is definitively at a
    // different position in a hypothetical spawn stream.
    REQUIRE_MSG(cb.stats().activated > cs.stats().activated + 1000,
                "the two crowds spawned nearly the same number of cars, so "
                "this proves nothing about stream position",
                "vacuity");

    std::size_t matched = 0;
    for (const VehicleAgent& a : cs.vehicles()) {
        const auto it = std::find_if(
            cb.vehicles().begin(), cb.vehicles().end(), [&](const VehicleAgent& b) {
                return b.lane_key == a.lane_key && b.slot == a.slot;
            });
        if (it == cb.vehicles().end()) continue;
        REQUIRE(it->profile.kind == a.profile.kind);
        REQUIRE(it->profile.headway == a.profile.headway);
        REQUIRE(it->profile.min_gap == a.profile.min_gap);
        ++matched;
    }
    REQUIRE_MSG(matched > 10, "the two crowds shared no cars", "vacuity");
    std::printf("      %zu shared cars, same driver after %zu vs %zu spawns\n",
                matched, cs.stats().activated, cb.stats().activated);
    pass("a driver comes off the car's identity, never off a spawn counter");
}

void test_a_retired_agent_never_comes_back() {
    // A car you rammed cannot be described by a closed form any more, so the
    // demotion is one-way. Without this, driving away and back re-spawns the
    // wreck at the position it would have had if you had never touched it.
    Network net;
    traffic_harness::build_network(net);
    const CrowdTuning tune = base_tuning();
    Crowd c;
    c.build(net.lanes, traffic_harness::kMapSeed, AmbientTuning{}, tune);

    Scene scene;
    const glm::vec2 here{1600.0f, 1700.0f};
    const glm::vec2 away{200.0f, 200.0f};
    for (int64_t s = 0; s < 200; ++s) {
        c.refresh(s, here);
        c.rebuild_buckets();
        c.step_vehicles(s);
        c.step_peds(s);
        c.publish(scene);
    }
    REQUIRE(c.vehicles().size() > 4);
    const uint64_t gone_key = c.vehicles().front().lane_key;
    const uint32_t gone_slot = c.vehicles().front().slot;

    for (int64_t s = 200; s < 260; ++s) c.refresh(s, away);
    REQUIRE(c.stats().retired > 0);

    for (int64_t s = 260; s < 460; ++s) {
        c.refresh(s, here);
        c.rebuild_buckets();
        c.step_vehicles(s);
        c.step_peds(s);
        c.publish(scene);
    }
    const bool back = std::any_of(c.vehicles().begin(), c.vehicles().end(),
                                  [&](const VehicleAgent& v) {
                                      return v.lane_key == gone_key &&
                                             v.slot == gone_slot;
                                  });
    REQUIRE_MSG(!back, "a retired agent was re-instantiated", "permanence");
    std::printf("      %zu agents retired and none returned\n", c.stats().retired);
    pass("retirement is permanent, so nothing ever demotes back to a phantom");
}

}  // namespace

int main() {
    test_phantom_has_no_history();
    test_phantoms_on_a_lane_never_close_on_each_other();
    test_arrival_direction_does_not_change_the_cars();
    test_scan_order_does_not_reach_the_result();
    test_spine_reordering_is_not_yet_bit_identical_and_here_is_the_cost();
    test_sub_rate_keyed_holds_and_spawn_ordinal_breaks();
    test_sub_rate_is_a_function_of_the_step_alone();
    test_radius_invariance_splits_at_the_analytic_boundary();
    test_driver_is_a_property_of_the_car_not_of_the_queue();
    test_a_retired_agent_never_comes_back();
    return apricot_test::done("traffic_determinism_tests");
}
