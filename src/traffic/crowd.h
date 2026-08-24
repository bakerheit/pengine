#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

#include "city/traffic_ai.h"
#include "road/lane_graph.h"
#include "scene/scene.h"
#include "traffic/ambient.h"

namespace apricot {

// THE ACTIVE SET — the bounded population that is actually stepped.
//
// Everything outside it is a phantom (traffic/ambient.h): defined, not
// simulated, costing nothing until somebody asks. This class owns the two
// transitions between those worlds and the per-step work of the ones that made
// it across.
//
// Four rules hold the whole thing up, and each of them is here because the
// obvious implementation of it is wrong:
//
// 1. AN AGENT'S IDENTITY IS (lane key, slot), NEVER AN INDEX. Lane keys come
//    off the authored spine, so an agent survives a rebuild and survives
//    reordering the spine table. Nothing about which car you meet depends on
//    how many cars were instantiated before it.
//
// 2. THE ACTIVE SET IS KEPT SORTED BY THAT IDENTITY. Not for lookup — for
//    determinism. Anything that sums over neighbours (ped_separation does)
//    sums floats, and float addition is not associative, so a neighbour list
//    gathered in arrival order gives a different answer to the same list
//    gathered in a different arrival order. Sorting makes the iteration order
//    a pure function of the SET rather than of its history.
//
// 3. EVERY CROSS-AGENT READ IS OF FROZEN DATA. The lane buckets are built from
//    the positions at the top of the step and are not touched again, so agent
//    A reading agent B's gap gets the same answer whether A or B updated
//    first. Without this, iteration order leaks into results even when the
//    order itself is deterministic — and it would then leak whenever the
//    activation radius changed the set.
//
// 4. A SUB-RATE PHASE IS KEYED, NEVER COUNTED. See SubRatePolicy.
//
// Nothing here reads a clock. `step` is the absolute sim step, passed in.

// Which steps an agent updates on, when the crowd runs below the sim rate.
//
// The rule sub-rate scheduling has to satisfy is narrow and absolute: the
// schedule must be a pure function of the step index and the agent's STABLE
// identity, and never of arrival order. Both policies below satisfy "a pure
// function of the step index". Only one of them satisfies the other half, and
// the difference is invisible until two runs instantiate the same agents in a
// different order — which is every run of a streamed city.
enum class SubRatePolicy : uint8_t {
    // phase = hash(map_seed, lane key, slot) % k. Correct, unconditionally:
    // the phase travels with the agent and nothing else can reach it.
    Keyed = 0,

    // phase = position in the active vector % k. This is the literal reading of
    // "update agent i when step % k == i % k", and in THIS class it happens to
    // be safe — but only because the active vector is sorted by identity after
    // every membership change, which makes the position identity-derived.
    // Remove that sort and this silently becomes SpawnOrdinal.
    //
    // It is shipped as a comparison, not as an option. The suite asserts that
    // it survives, so that if somebody ever drops the sort the failure lands
    // here with a name instead of turning up as a replay desync.
    ContainerIndex = 1,

    // phase = the ordinal this agent was instantiated at % k. THE BUG. How many
    // agents were made before this one is a fact about which way the player
    // drove in, so the same car gets a different phase in two runs of the same
    // tape. tests/traffic_determinism_tests.cpp requires it to DIVERGE; a
    // negative control that passes is not a control.
    SpawnOrdinal = 2,
};

// Has this agent left its closed form yet?
//
// Analytic agents recompute their position from an INTEGER phase every step —
// they are reproduced, not advanced — so their state at step t does not depend
// on the step they were instantiated at. Integrating agents accumulate, and
// from that moment their state is a function of their whole history. The
// transition is one-way for the same reason a retired agent never demotes back
// to a phantom: a car that braked cannot be described by a closed form any
// more, and pretending otherwise is the LOD promotion bug wearing a hat.
enum class AgentMode : uint8_t { Analytic = 0, Integrating = 1 };

struct VehicleAgent {
    // --- identity (stable, ordered on) ---
    uint64_t lane_key = 0;
    uint32_t slot = 0;

    // --- state ---
    LaneRef lane = kInvalidLane;
    float dist_along_m = 0.0f;
    float speed_mps = 0.0f;
    AgentMode mode = AgentMode::Analytic;

    // The previous step's distance. An analytic agent detects its schedule
    // wrapping by seeing this go backwards, which is the moment it stops being
    // reproducible and starts being a car.
    float last_dist_m = 0.0f;

    // The speed this agent would hold if nothing were in its way. Analytic
    // agents are AT it by definition; falling below it is what "perturbed"
    // means.
    float cruise_mps = 0.0f;

    uint32_t decisions = 0;  // the decision_index handed to choose_next()

    // How many agents this crowd had instantiated before this one. Carried for
    // exactly one reason: SubRatePolicy::SpawnOrdinal needs it in order to be
    // wrong. Nothing shipped may read it.
    uint32_t spawn_ordinal = 0;

    DriverProfile profile{};

    glm::vec3 pos{0.0f};
    glm::vec3 fwd{1.0f, 0.0f, 0.0f};
    NodeId node = kInvalidId;
};

struct PedAgent {
    uint64_t lane_key = 0;
    uint32_t slot = 0;

    LaneRef lane = kInvalidLane;
    float dist_along_m = 0.0f;
    float speed_mps = 0.0f;
    float base_lateral_m = 0.0f;  // which footway, from the slot ordinal
    float lateral_m = 0.0f;       // after separation
    float last_dist_m = 0.0f;
    uint32_t spawn_ordinal = 0;  // see VehicleAgent::spawn_ordinal
    AgentMode mode = AgentMode::Analytic;
    bool blocked = false;

    glm::vec3 pos{0.0f};
    glm::vec3 fwd{1.0f, 0.0f, 0.0f};
    NodeId node = kInvalidId;
};

struct CrowdTuning {
    // Radii, in metres. Activate < retire, always: one radius means a player
    // idling on the boundary thrashes the same agent in and out forever, which
    // is the identical hysteresis argument the terrain streamer already makes.
    float vehicle_activate_m = 220.0f;
    float vehicle_retire_m = 320.0f;
    float ped_activate_m = 110.0f;
    float ped_retire_m = 160.0f;

    // Population caps. A cap THAT ACTUALLY BINDS is scan-order dependent —
    // which agents survive it depends on which were reached first — so these
    // are a safety valve against a pathological map, not a design knob. If a
    // measurement ever runs into one, the measurement is wrong before the
    // engine is. tests/traffic_bench.cpp checks that it never binds.
    uint32_t max_vehicles = 65536;
    uint32_t max_peds = 65536;

    // Walk the candidate lanes backwards during refresh(). It exists for one
    // reason: the suite that proves the scan order cannot reach the result
    // needs a second scan order to compare against, on an OTHERWISE IDENTICAL
    // lane graph, so that a difference cannot be blamed on the road build.
    // There is no reason to set it in a game.
    bool reverse_scan_order = false;

    // Membership refresh cadence, in STEPS. Never in frames: a 144 Hz machine
    // would otherwise change the population at different moments to a 60 Hz
    // one, and the sim would depend on the display.
    int refresh_every_steps = 8;

    // k in "agent updates when step % k == phase". 1 is every agent every step.
    int vehicle_sub_rate = 1;
    int ped_sub_rate = 1;
    SubRatePolicy policy = SubRatePolicy::Keyed;

    // Car length, so a follow gap is bumper to bumper rather than centre to
    // centre. Feeds effective_min_gap()'s floor, which is what stops a steady
    // follow settling inside the leader.
    float car_length_m = 4.4f;

    // How far short of the junction a car holds for a red.
    float stop_line_m = 6.0f;

    // Signal cycle, in steps. A step count and not a duration, for the same
    // reason every other clock in this engine counts steps.
    int64_t signal_period_steps = 1440;  // 12 s at 120 Hz
};

// What one step cost and what it did. Counters only — no timings, because
// nothing below src/app/ may read a clock. The bench times the phases from
// outside, which is also why they are separate public calls.
struct CrowdStats {
    std::size_t vehicles = 0;
    std::size_t peds = 0;
    std::size_t vehicles_analytic = 0;
    std::size_t peds_analytic = 0;
    std::size_t vehicles_stepped = 0;  // this step, after sub-rate gating
    std::size_t peds_stepped = 0;
    std::size_t activated = 0;   // cumulative
    std::size_t retired = 0;     // cumulative
    std::size_t ped_neighbour_tests = 0;  // this step
    std::size_t lanes_scanned = 0;        // last refresh
};

class Crowd {
public:
    // Binds to a lane graph and builds the per-lane schedules and the lane
    // index. Both are pure functions of (graph, map_seed, tuning), so this is
    // the only place either is computed and neither is ever recomputed at
    // runtime. The graph must outlive the crowd.
    void build(const LaneGraph& graph, uint64_t map_seed,
               const AmbientTuning& ambient, const CrowdTuning& tuning);

    void clear();

    // --- the step, in phases -------------------------------------------------
    //
    // Split because the bench needs to know where the time goes and the only
    // legal place to hold a clock is outside the sim. It is also just a better
    // shape: each phase is separately testable and the ordering constraint
    // between them is written down rather than implied.
    //
    // Call in this order, once per sim step:
    //
    //     if (step % refresh_every_steps == 0) crowd.refresh(step, player_xz);
    //     crowd.rebuild_buckets();
    //     crowd.step_vehicles(step);
    //     crowd.step_peds(step);
    //     crowd.publish(scene);

    // Instantiate phantoms that came into range and retire agents that left.
    void refresh(int64_t step, glm::vec2 player_xz);

    // Freeze this step's cross-agent reads. MUST run after refresh() and
    // before either step_*(), because both read it and neither may see a
    // partially updated world.
    void rebuild_buckets();

    void step_vehicles(int64_t step);
    void step_peds(int64_t step);

    // Push transforms into the scene. Creates a node for an agent that does not
    // have one and removes the nodes of agents retired since the last call.
    void publish(Scene& scene);

    // --- inspection ----------------------------------------------------------
    const std::vector<VehicleAgent>& vehicles() const { return vehicles_; }
    const std::vector<PedAgent>& peds() const { return peds_; }
    const CrowdStats& stats() const { return stats_; }
    const std::vector<LaneSchedule>& vehicle_schedules() const { return veh_sched_; }
    const std::vector<LaneSchedule>& ped_schedules() const { return ped_sched_; }

    // A 64-bit digest of the whole active population — identity, mode and
    // state — for the determinism suites. Folded over the SORTED set, so it is
    // a property of the population and not of how it was assembled.
    uint64_t population_hash() const;

    // Identity-only digest. Answers "is the same SET active" separately from
    // "is it in the same state", which is the distinction the radius-invariance
    // result turns on.
    uint64_t membership_hash() const;

private:
    struct BucketEntry {
        float dist = 0.0f;
        uint32_t agent = 0;
    };

    void build_lane_index();
    void gather_lanes(glm::vec2 xz, float radius_m, std::vector<LaneRef>& out) const;
    int sub_rate_phase(uint64_t lane_key, uint32_t slot, uint32_t index,
                       uint32_t spawn_ordinal, int k) const;
    bool is_retired(uint64_t lane_key, uint32_t slot) const;
    void retire(uint64_t lane_key, uint32_t slot);

    // Retired identities, sorted, EXACT. A packed-and-hashed key would be
    // smaller and would occasionally suppress a car that was never retired,
    // once, somewhere, unreproducibly.
    struct RetiredId {
        uint64_t key;
        uint32_t slot;
    };

    const LaneGraph* graph_ = nullptr;
    uint64_t map_seed_ = 0;
    AmbientTuning ambient_{};
    CrowdTuning tuning_{};

    std::vector<LaneSchedule> veh_sched_;  // indexed by LaneRef
    std::vector<LaneSchedule> ped_sched_;

    // Uniform grid over lane geometry. Ordered containers only: a lane list
    // whose order came out of a hash map is a neighbour list whose float sum
    // depends on the standard library.
    std::vector<std::vector<LaneRef>> index_cells_;
    glm::vec2 index_min_{0.0f};
    int index_nx_ = 0;
    int index_nz_ = 0;
    float index_cell_m_ = 64.0f;

    std::vector<VehicleAgent> vehicles_;
    std::vector<PedAgent> peds_;

    // Permanent: a retired agent never returns, because the closed form that
    // would have described it stopped describing it the moment it was
    // simulated. This vector therefore only grows — see README.md, which says
    // what that costs and what has to happen about it before shipping.
    std::vector<RetiredId> retired_;

    // Frozen per-step reads.
    std::vector<std::vector<BucketEntry>> lane_buckets_;
    std::vector<LaneRef> touched_lanes_;
    std::vector<float> leader_gap_;  // per vehicle, metres, +inf when clear

    // Ped neighbour grid, cell == PED_SEPARATION_RADIUS so a ped scans its own
    // cell plus the eight around it.
    std::vector<std::vector<uint32_t>> ped_cells_;
    std::vector<int64_t> ped_cell_keys_;
    std::vector<glm::vec2> ped_pos_frozen_;
    std::vector<glm::vec2> ped_scratch_;

    std::vector<NodeId> dead_nodes_;
    std::vector<LaneRef> lane_scratch_;

    CrowdStats stats_{};
};

}  // namespace apricot
