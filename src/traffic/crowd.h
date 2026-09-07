#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <set>
#include <vector>
#include <utility>

#include <glm/glm.hpp>

#include "city/traffic_ai.h"
#include "physics/vehicle_damage.h"
#include "physics/vehicle_mechanical.h"
#include "road/lane_graph.h"
#include "scene/scene.h"
#include "traffic/ambient.h"
#include "traffic/pedestrian_paths.h"

namespace apricot {

struct VehicleState;

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

// One smooth path through a junction. Lanes end and begin at the junction
// centre, so swapping their tangents directly makes a car rotate in one sim
// step. This cubic joins a point before the centre to a point after it and the
// small arc-length table lets a vehicle travel it at an even road speed.
struct TrafficTurnCurve {
    glm::vec3 p0{0.0f};
    glm::vec3 p1{0.0f};
    glm::vec3 p2{0.0f};
    glm::vec3 p3{0.0f};
    std::array<float, 17> arc_m{};
    float length_m = 0.0f;
};

TrafficTurnCurve traffic_turn_curve(const LaneGraph& graph, LaneRef incoming,
                                    LaneRef outgoing, float entry_m,
                                    float exit_m);
LanePose traffic_turn_pose(const TrafficTurnCurve& curve,
                           float distance_along_m);

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

    // Police is an agent role, not a renderer guess. Ambient patrol livery is
    // assigned from stable identity at promotion; pursuit is the temporary
    // wanted-response state layered over it. Routes are lane refs from the
    // current lane through the target lane and are refreshed on a fixed-step
    // cadence, never from frame time.
    bool police_unit = false;
    bool police_pursuit = false;
    std::vector<LaneRef> police_route;
    uint32_t police_route_index = 0;
    int64_t police_last_replan_step = -1;
    glm::vec2 police_last_target{0.0f};

    // Junction memory. Stop signs need a real arrival and dwell instead of the
    // old "speed dipped under 0.4, good enough" rolling stop. A car that has
    // entered a junction also keeps its claim briefly on the outgoing lane so
    // perpendicular traffic does not launch into its flank.
    uint32_t stop_junction = 0xFFFFFFFFu;
    int64_t stop_wait_steps = 0;
    int64_t stop_arrival_step = -1;
    bool stop_completed = false;
    uint32_t committed_junction = 0xFFFFFFFFu;
    LaneRef committed_approach_lane = kInvalidLane;
    LaneRef committed_exit_lane = kInvalidLane;

    // While this is valid, `lane` is already the outgoing lane for traffic
    // ordering, but the physical pose follows a cubic from the incoming stop
    // line to the outgoing clearance line. That keeps route bookkeeping cheap
    // without snapping the body or front wheels to the exit heading.
    LaneRef turn_from_lane = kInvalidLane;
    TurnKind active_turn_kind = TurnKind::Straight;
    float turn_progress_m = 0.0f;
    float turn_length_m = 0.0f;
    float turn_entry_m = 0.0f;
    float turn_exit_m = 0.0f;
    float turn_steer_rad = 0.0f;

    // Time spent at the stop line because the chosen exit lane cannot hold a
    // whole car beyond the junction. This is deliberately separate from red
    // light / stop-sign waiting: only a blocked destination should make a
    // driver look for a different turn.
    int64_t blocked_exit_steps = 0;
    int64_t intersection_stall_steps = 0;

    // World-space reaction layered over the lane pose. Traffic normally stays
    // cheap and lane-constrained, but a player impact gives it linear and
    // angular momentum so it can be shoved onto a shoulder instead of acting
    // like an immovable concrete prop. The driver then slows and steers a
    // forward arc back into flow; it never translates back to an old pose.
    glm::vec2 collision_offset_xz{0.0f};
    glm::vec2 collision_velocity_xz{0.0f};
    float collision_yaw_rad = 0.0f;
    float collision_yaw_velocity = 0.0f;
    float collision_recovery_seconds = 0.0f;
    // Actual +Y front-wheel rotation; positive turns a native -Z nose to -X.
    // Render directly as +Y rotation, not with the UI/right-steer sign flip.
    float collision_steer_rad = 0.0f;
    VehicleDamageState body_damage{};
    VehicleMechanicalState mechanical{};

    // How many agents this crowd had instantiated before this one. Carried for
    // exactly one reason: SubRatePolicy::SpawnOrdinal needs it in order to be
    // wrong. Nothing shipped may read it.
    uint32_t spawn_ordinal = 0;

    DriverProfile profile{};

    glm::vec3 pos{0.0f};
    glm::vec3 fwd{1.0f, 0.0f, 0.0f};
    NodeId node = kInvalidId;
};

// One canonical model decision for every production consumer. A converted
// responder changes body and livery immediately; ordinary traffic keeps the
// existing identity-derived model recipe.
inline TrafficVehicleKind traffic_vehicle_kind(const VehicleAgent& agent) {
    return agent.police_unit
        ? TrafficVehicleKind::Police
        : traffic_vehicle_kind(agent.lane_key, agent.slot);
}

// Stable identity of one police vehicle whose ray to the wanted target is
// unobstructed in the production world. World collision owns that raycast;
// Crowd owns the range/FOV/contact rules applied after it.
struct VisiblePoliceIdentity {
    uint64_t lane_key = 0;
    uint32_t slot = 0;

    bool operator<(const VisiblePoliceIdentity& other) const {
        return lane_key != other.lane_key ? lane_key < other.lane_key
                                          : slot < other.slot;
    }
    bool operator==(const VisiblePoliceIdentity& other) const {
        return lane_key == other.lane_key && slot == other.slot;
    }
};

struct PedAgent {
    uint64_t lane_key = 0;
    uint32_t slot = 0;

    LaneRef lane = kInvalidLane;
    float dist_along_m = 0.0f;  // metres on the active sidewalk/link, not car lane
    float speed_mps = 0.0f;
    float base_lateral_m = 0.0f;  // footway centre offset relative to current lane
    float lateral_m = 0.0f;       // after separation
    float last_dist_m = 0.0f;
    uint32_t spawn_ordinal = 0;  // see VehicleAgent::spawn_ordinal
    AgentMode mode = AgentMode::Analytic;
    bool blocked = false;

    uint32_t walk_path = PedestrianPaths::invalid;
    uint32_t walk_link = PedestrianPaths::invalid;  // otherwise link of walk_path
    uint32_t walk_decisions = 0;
    float walk_offset_m = 0.0f;

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

    // Shared wanted-response knobs. Wanted heat and the crowd must read the
    // same values or a cop can lose contact on one side while holding it on
    // the other.
    PoliceTuning police{};

    // Car length, so a follow gap is bumper to bumper rather than centre to
    // centre. Feeds effective_min_gap()'s floor, which is what stops a steady
    // follow settling inside the leader.
    float car_length_m = 5.0f;

    // Minimum distance from the junction centre at which a car holds. The
    // runtime expands this from the widest connected carriageway plus half a
    // vehicle, so an arterial's stop line is not buried inside its own box.
    float stop_line_m = 6.0f;

    // Signal cycle, in steps. A step count and not a duration, for the same
    // reason every other clock in this engine counts steps.
    int64_t signal_period_steps = 1440;  // 12 s at 120 Hz
    int64_t signal_yellow_steps = 180;   // final 1.5 s of each green phase

    // Smarter local behavior. All clocks are sim steps and all scans are over
    // the frozen active set, so these remain deterministic under streaming.
    int64_t stop_dwell_steps = 90;       // 0.75 s at 120 Hz
    float stop_capture_m = 0.45f;
    float junction_lookahead_m = 18.0f;
    float junction_clear_m = 8.0f;
    float junction_eta_tie_s = 0.30f;
    float spawn_junction_exclusion_m = 12.0f;
    float junction_storage_lookahead_m = 32.0f;
    float junction_storage_margin_m = 1.0f;
    int64_t blocked_exit_replan_every_steps = 120;
    uint32_t blocked_exit_replan_probes = 32;
    PlayerHazardTuning player_hazard{};

    // A person is much narrower and slower than the player's car. This is the
    // tight bumper-path cone from the lifted on-foot traffic rules.
    PlayerHazardTuning player_hazard_on_foot{
        18.0f, 1.5f, 2.0f, 1.5f, 6.0f,
    };

    // Traffic-body response to the player. The base lane sim remains scalar;
    // these tune the short world-space impulse and its return to the lane.
    float traffic_mass_kg = 1450.0f;
    float collision_restitution = 0.18f;
    float collision_max_speed_mps = 14.0f;
    float collision_linear_drag = 1.8f;
    float collision_angular_drag = 2.4f;
    float collision_recovery_delay_s = 0.55f;
    float collision_recovery_lookahead_m = 11.0f;
    float collision_recovery_yaw_rate = 1.35f;
    float collision_recovery_max_yaw = 0.52f;
    float collision_recovery_speed_mul = 0.68f;
    float collision_recovery_speed_cap_mps = 6.0f;
    float collision_recovery_max_steer_rad = 0.55f;

    // Once a driver owns a junction it finishes the movement. Contacts inside
    // the box use a short reaction pause and fast lane re-alignment; a car
    // that still falls below walking speed escalates to a clearance throttle
    // instead of becoming a permanent signal-box obstacle.
    float intersection_min_clear_speed_mps = 4.5f;
    float intersection_escape_speed_mps = 7.0f;
    float intersection_escape_accel_mps2 = 9.0f;
    float intersection_stall_speed_mps = 1.0f;
    int64_t intersection_escape_after_steps = 120;
    float intersection_collision_recovery_delay_s = 0.08f;
    float intersection_recovery_lookahead_m = 5.5f;
    float intersection_recovery_yaw_rate = 3.8f;
    float intersection_recovery_max_yaw = 0.38f;
    float intersection_low_speed_yaw_threshold_mps = 2.0f;

    // Conservative maximum envelope used for junction clearance and the
    // broadphase. Narrow-phase contact uses traffic_vehicle_footprint() for
    // the deterministic visible model, so sedans do not stop at truck width.
    float traffic_half_width_m = 1.15f;
    float traffic_half_length_m = 2.50f;
    float traffic_collision_cell_m = 6.0f;
};

struct TrafficBodyCollision {
    bool collided = false;
    float penetration_m = 0.0f;
    float closing_speed_mps = 0.0f;
    glm::vec2 normal_xz{0.0f};
};

// Optional second player hazard used while the player is out of the car. It
// stays a tiny traffic-owned view so traffic does not depend on the game's
// character controller or any host type.
struct OnFootTrafficHazard {
    glm::vec2 position{0.0f};
    glm::vec2 velocity{0.0f};
    float height_m = std::numeric_limits<float>::quiet_NaN();
};

// Deterministic planar OBB contact for two lane agents. Both receive
// separation and impulse, then use the same driver-recovery state as a player
// impact. Used directly by the crowd broadphase and by headless contracts.
TrafficBodyCollision resolve_traffic_body_collision(
    VehicleAgent& a, VehicleAgent& b, const CrowdTuning& tuning);

// Convert a world-XZ offset from an AI car centre into the same physics body
// frame used by VehicleDamageState: -X left, +X right, -Z front, +Z rear.
// Public so the corner mapping is pinned independently of collision response.
glm::vec3 traffic_body_local_contact(glm::vec2 forward_xz,
                                     glm::vec2 contact_offset_xz);

// A driver may enter only when its destination lane can hold its centre past
// the junction clear zone while preserving a bumper gap to the queue ahead.
// Infinity means the destination lane is empty.
bool traffic_exit_has_storage(float nearest_vehicle_center_m,
                              float junction_clear_m, float car_length_m,
                              float min_follow_gap_m, float margin_m);

bool traffic_same_committed_movement(
    LaneRef approach, LaneRef exit, LaneRef committed_approach,
    LaneRef committed_exit);

// Centre-to-centre junction footprint used by both entry and exit. A stopped
// car keeps its nose outside the widest crossing carriageway; a committed car
// retains ownership until its rear has passed the same edge.
float traffic_junction_clearance(float widest_carriageway_m,
                                 float vehicle_half_length_m, float margin_m,
                                 float minimum_m);
float traffic_junction_clearance(const LaneGraph& graph, uint32_t junction,
                                 const CrowdTuning& tuning);

enum class TrafficSignalPhase : uint8_t { Red = 0, Yellow, Green };

// One shared signal clock for drivers and visible bulbs. If these are separate
// implementations, a car eventually stops under a green light or drives a red.
TrafficSignalPhase traffic_signal_phase(const LaneGraph& graph,
                                        uint32_t junction, LaneRef incoming,
                                        int64_t step,
                                        const CrowdTuning& tuning);

struct TrafficStopDecision {
    bool hold = true;
    bool completed = false;
    int64_t wait_steps = 0;
};

// One deterministic stop-sign tick. Cars must reach the line, settle, and
// remain there for the dwell before becoming eligible for right of way.
TrafficStopDecision traffic_stop_decision(float slack_to_line_m, float speed_mps,
                                          int64_t wait_steps,
                                          int elapsed_steps,
                                          int64_t required_steps,
                                          float capture_m);

struct TrafficApproachView {
    bool valid = false;
    bool committed = false;
    int priority = 0;
    float eta_seconds = std::numeric_limits<float>::infinity();
    int64_t arrival_step = -1;
    uint64_t lane_key = 0;
    uint32_t slot = 0;
};

// Strict, antisymmetric right-of-way pick. A committed car wins first, then
// movement priority, then arrival/ETA, then stable agent identity.
bool traffic_approach_yields(const TrafficApproachView& mine,
                             const TrafficApproachView& other,
                             bool all_way_stop, float eta_tie_seconds);

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
    std::size_t player_hazards = 0;       // this step
    std::size_t junction_yields = 0;      // this step
    std::size_t stop_holds = 0;           // this step
    std::size_t junction_box_holds = 0;   // blocked destination this step
    std::size_t jam_reroutes = 0;         // alternate exits selected this step
    std::size_t stalled_vehicles = 0;     // blocked-exit cars below 0.35 m/s
    std::size_t intersection_escapes = 0; // stalled committed cars clearing
    std::size_t ai_collision_pairs = 0;   // broadphase candidates this step
    std::size_t ai_collisions = 0;        // contacts resolved this step
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

    void step_vehicles(int64_t step, const VehicleState* player = nullptr,
                       const OnFootTrafficHazard* on_foot_player = nullptr);
    void step_peds(int64_t step);

    // Wanted response context for the next fixed step. Only identities in
    // `visible_police` passed their own production-world LOS raycast. An empty
    // list fails closed: no cruiser can witness or maintain contact through a
    // building.
    void set_police_context(int wanted_level, glm::vec2 target_xz,
                            const std::vector<VisiblePoliceIdentity>&
                                visible_police = {});
    void set_police_context(int wanted_level, glm::vec3 target,
                            const std::vector<VisiblePoliceIdentity>&
                                visible_police = {}) {
        set_police_context(wanted_level, glm::vec2{target.x, target.z},
                           visible_police);
    }

    // Resolve the player against active traffic bodies. Both bodies receive
    // separation and impulse; traffic carries its reaction as a deterministic
    // world-space offset over the lane pose, then drives forward to rejoin.
    bool resolve_player_collision(VehicleState& player,
                                  float player_half_width_m = 1.045f,
                                  float player_half_length_m = 2.445f,
                                  float player_mass_kg = 1250.0f,
                                  float player_body_damage_gain = 1.0f);

    // Push transforms into the scene. Creates a node for an agent that does not
    // have one and removes the nodes of agents retired since the last call.
    void publish(Scene& scene);

    // --- inspection ----------------------------------------------------------
    const std::vector<VehicleAgent>& vehicles() const { return vehicles_; }
    // Fixed-step ownership transfer. Retire the exact identity so refresh
    // cannot respawn a second copy of a car now owned by the player.
    bool take_vehicle(uint64_t lane_key, uint32_t slot, VehicleAgent& out);
    void set_parked_vehicle_positions(std::vector<glm::vec2> positions) {
        parked_vehicle_positions_.clear();
        for (glm::vec2 p : positions)
            parked_vehicle_positions_.push_back({p.x,std::numeric_limits<float>::quiet_NaN(),p.y});
    }
    void set_parked_vehicle_poses(std::vector<glm::vec3> positions) {
        parked_vehicle_positions_=std::move(positions);
    }
    const std::vector<PedAgent>& peds() const { return peds_; }
    const CrowdStats& stats() const { return stats_; }
    const std::vector<LaneSchedule>& vehicle_schedules() const { return veh_sched_; }
    const std::vector<LaneSchedule>& ped_schedules() const { return ped_sched_; }
    const PoliceTuning& police_tuning() const { return tuning_.police; }
    std::size_t police_unit_count() const;
    std::size_t police_pursuit_count() const;
    bool player_in_police_view() const;
    const VehicleAgent* nearest_police_pursuer() const;

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

    struct JunctionSnapshot {
        LaneRef lane = kInvalidLane;
        float dist_along_m = 0.0f;
        float speed_mps = 0.0f;
        uint32_t decisions = 0;
        uint32_t stop_junction = 0xFFFFFFFFu;
        int64_t stop_wait_steps = 0;
        int64_t stop_arrival_step = -1;
        bool stop_completed = false;
        uint32_t committed_junction = 0xFFFFFFFFu;
        LaneRef committed_approach_lane = kInvalidLane;
        LaneRef committed_exit_lane = kInvalidLane;
        LaneRef planned_exit_lane = kInvalidLane;
        bool active_turn = false;
        bool engine_failed = false;
    };

    void build_lane_index();
    void gather_lanes(glm::vec2 xz, float radius_m, std::vector<LaneRef>& out) const;
    int sub_rate_phase(uint64_t lane_key, uint32_t slot, uint32_t index,
                       uint32_t spawn_ordinal, int k) const;
    void update_police_response(int64_t step);
    void update_police_route(VehicleAgent& agent, LaneRef target_lane,
                             int64_t step);
    bool police_has_line_of_sight(const VehicleAgent& agent) const;
    void resolve_vehicle_collisions();
    float junction_clearance(uint32_t junction) const;
    bool movements_conflict(LaneRef from, LaneRef to,
                            LaneRef other_from, LaneRef other_to) const;
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
    PedestrianPaths ped_paths_;

    // Uniform grid over lane geometry. Ordered containers only: a lane list
    // whose order came out of a hash map is a neighbour list whose float sum
    // depends on the standard library.
    std::vector<std::vector<LaneRef>> index_cells_;
    glm::vec2 index_min_{0.0f};
    int index_nx_ = 0;
    int index_nz_ = 0;
    float index_cell_m_ = 64.0f;

    std::vector<VehicleAgent> vehicles_;
    std::vector<glm::vec3> parked_vehicle_positions_;
    std::vector<PedAgent> peds_;

    int police_wanted_level_ = 0;
    glm::vec2 police_target_xz_{0.0f};
    std::vector<VisiblePoliceIdentity> visible_police_;
    bool police_response_due_ = false;
    int64_t police_next_response_step_ = 0;

    // Permanent: a retired agent never returns, because the closed form that
    // would have described it stopped describing it the moment it was
    // simulated. This vector therefore only grows — see README.md, which says
    // what that costs and what has to happen about it before shipping.
    std::vector<RetiredId> retired_;

    // Frozen per-step reads.
    std::vector<std::vector<BucketEntry>> lane_buckets_;
    std::vector<LaneRef> touched_lanes_;
    std::vector<float> leader_gap_;  // per vehicle, metres, +inf when clear
    std::vector<float> leader_speed_;  // matching leader speed, +inf when clear
    std::vector<JunctionSnapshot> junction_frozen_;
    std::vector<std::vector<uint32_t>> junction_heads_;
    std::vector<float> junction_clearance_m_;
    std::vector<float> junction_turn_clearance_m_;
    std::set<std::array<LaneRef, 4>> movement_conflicts_;

    // Ped neighbour grid, cell == PED_SEPARATION_RADIUS so a ped scans its own
    // cell plus the eight around it.
    //
    // A COUNTING SORT INTO FLAT ARRAYS, not a vector of buckets. The obvious
    // shape — std::vector<std::vector<uint32_t>>, cleared each step — frees and
    // re-allocates every inner vector on every step, and at a hundred thousand
    // pedestrians that is the single most expensive thing in the frame. This
    // form clears with one fill of an integer array and allocates nothing after
    // the first step.
    //
    // The table is sized to the POPULATION, not to the area: a dense grid over
    // the active box is fine at a 160 m radius and is megabytes of memset per
    // step at a kilometre. Two cells can therefore share a bucket, which is why
    // the real cell coordinates are compared on the way out.
    std::vector<uint32_t> ped_bucket_start_;  // size table + 1
    std::vector<uint32_t> ped_bucket_items_;  // size peds_, ped indices
    std::vector<uint32_t> ped_bucket_of_;     // size peds_, bucket per ped
    std::vector<int64_t> ped_cell_keys_;
    std::vector<glm::vec2> ped_pos_frozen_;
    std::vector<glm::vec2> ped_scratch_;
    std::size_t ped_table_ = 0;

    std::vector<NodeId> dead_nodes_;
    std::vector<LaneRef> lane_scratch_;

    CrowdStats stats_{};
};

}  // namespace apricot
