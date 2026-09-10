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
#include "city/pedestrian_reactions.h"
#include "city/police_officer.h"
#include "game/police_combat.h"
#include "physics/vehicle.h"
#include "physics/vehicle_damage.h"
#include "physics/vehicle_mechanical.h"
#include "road/lane_graph.h"
#include "scene/scene.h"
#include "traffic/ambient.h"
#include "traffic/pedestrian_paths.h"

namespace apricot {

struct VehicleState;
class TerrainCollider;

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

// Appended, never reordered: the kind is mixed into the population hash, so
// renumbering an existing one changes every recorded drive's hash for no
// behavioural reason.
enum class TrafficManeuverKind : uint8_t {
    None, PullAside, MergeBack, PoliceBypass, PoliceTurnaround, PoliceRoadside,
    // Deliberate contact. The ONE arc the player's own body does not veto —
    // hitting the player is the point of it — which is why it is a distinct
    // kind and not a PoliceBypass with a different offset.
    PoliceRam
};

// Deliberate steering is independent of impact displacement. The lane remains
// the route anchor while the body follows these short, clearance-tested arcs.
struct TrafficManeuver {
    TrafficManeuverKind kind = TrafficManeuverKind::None;
    std::array<TrafficTurnCurve, 3> curves{};
    uint8_t count = 0;
    float progress_m = 0.0f;
    float length_m = 0.0f;
    float speed_limit_mps = 4.0f;
    LaneRef destination = kInvalidLane;
    float end_station_m = 0.0f;
    float end_offset_m = 0.0f;
    bool active() const { return count > 0 && progress_m < length_m; }
};

TrafficTurnCurve traffic_steering_curve(const LanePose& from, const LanePose& to);
LanePose traffic_maneuver_pose(const TrafficManeuver& move, float distance_m);

// Do two identities name the SAME DEPARTURE?
//
// `(lane_key, slot)` alone does not answer this. A slot is a recurring
// schedule departure, so one agent can retire and the next lap's agent appear
// at the same pair with NO absent frame between two presentation updates —
// measured at 41 vehicle and 161 pedestrian swaps over 270 s on the authored
// city. Any consumer that keeps per-agent state across frames and matches on
// the pair alone will hand that state to a car or a person that never earned
// it: a get-up crossfade out of somebody else's sprawl, a horn cooldown from
// the car that just left. Presentation and audio reconciliation must ask THIS,
// not compare the pair.
inline bool same_departure(uint64_t a_key, uint32_t a_slot, int64_t a_generation,
                           uint64_t b_key, uint32_t b_slot, int64_t b_generation) {
    return a_key == b_key && a_slot == b_slot && a_generation == b_generation;
}

struct VehicleAgent {
    // --- identity (stable, ordered on) ---
    uint64_t lane_key = 0;
    uint32_t slot = 0;
    // The schedule lap this car departed on. Ordering does NOT include it:
    // only one instance of a (lane_key, slot) pair can be resident at a time,
    // so the sort is unchanged. It exists so retirement can ban THIS departure
    // without banning every future one.
    int64_t generation = 0;

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
    // Assigned only when a service truck is dispatched; never changes in view.
    bool snowplow_unit = false;
    bool police_unit = false;
    bool police_pursuit = false;
    std::vector<LaneRef> police_route;
    uint32_t police_route_index = 0;
    int64_t police_last_replan_step = -1;
    glm::vec2 police_last_target{0.0f};
    PoliceOfficerState officer{};
    // FREE-DRIVE PURSUIT. While `chase_active` this agent is NOT on the lane
    // graph: `chase` is stepped by the same vehicle physics the player uses,
    // against the same world, and pos/fwd/speed are read back out of it. The
    // lane fields are stale for the duration and are re-anchored on release.
    bool chase_active = false;
    VehicleState chase{};
    // Aiming at the target is all police_terminal_pursuit_cmd knows how to do,
    // so a free-driving cruiser WILL eventually pin itself against a wall with
    // the throttle open. These give it up and put the car back on the road,
    // then keep it there long enough not to drive straight back into the wall.
    float chase_stall_s = 0.0f;
    float chase_cooldown_s = 0.0f;
    // How long this cruiser has been backing up. Reversing is a three-point
    // turn, not a driving mode: past the cap the car gives the road back and
    // lets the lane router find a way round instead of grinding rearwards.
    float chase_reverse_s = 0.0f;
    float chase_turn_stall_s = 0.0f;
    float chase_turnaround_s = 0.0f;
    TrafficManeuver maneuver{};
    EmergencyYield emergency_yield = EmergencyYield::None;
    float emergency_resume_s = 0.0f;
    float roadside_offset_m = 0.0f;
    float maneuver_steer_rad = 0.0f;

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
    // Frustration builds while stuck and fades after traffic starts moving.
    // It changes comfort gaps and launch acceleration, never legal controls.
    float delay_seconds = 0.0f;

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
    if (agent.snowplow_unit) return TrafficVehicleKind::Snowplow;
    return agent.police_unit
        ? TrafficVehicleKind::Police
        : traffic_vehicle_kind(agent.lane_key, agent.slot);
}

inline glm::vec3 police_officer_eye_position(const VehicleAgent& agent) {
    if (police_officer_on_foot(agent.officer) || agent.officer.transition.active())
        return agent.officer.pos + glm::vec3{0.0f, 1.60f, 0.0f};
    return agent.pos + glm::vec3{0.0f, 1.15f, 0.0f};
}

inline glm::vec3 police_officer_forward(const VehicleAgent& agent) {
    if (police_officer_on_foot(agent.officer))
        return {std::sin(agent.officer.heading), 0.0f,
                -std::cos(agent.officer.heading)};
    return agent.fwd;
}

// ONE PARKED CAR near the player, and it is deliberately not an agent.
//
// It has no speed, no lane progress, no mode and no retirement, because it is
// a pure function of (map_seed, lane key, slot) — see traffic/ambient.h. That
// is what makes it the cheapest street density in the genre: the whole
// population costs one hash per car per membership refresh and nothing at all
// in between.
//
// It is also why the list is rebuilt wholesale rather than maintained. There
// is no history to lose. The day one of these becomes something a player can
// shunt or steal, it stops being describable by that function and has to
// become a real VehicleAgent with permanent retirement — the identical
// argument traffic/README.md makes about promotion, and the reason this type
// carries the same (lane_key, slot) identity a real agent would.
//
// SEPARATE FROM set_parked_vehicle_poses(). That setter is the APP's list of
// cars the PLAYER left somewhere, which drivers treat as hazards; this is the
// authored ambient population and never enters that list.
struct AmbientParkedCar {
    uint64_t lane_key = 0;
    uint32_t slot = 0;
    glm::vec3 pos{0.0f};
    glm::vec3 fwd{1.0f, 0.0f, 0.0f};
    TrafficVehicleKind kind = TrafficVehicleKind::Sedan;
};

// One actual player/cruiser contact per stable identity per solver call.
// Velocities are sampled at the contact point before the collision impulse.
struct PolicePlayerContact {
    uint64_t lane_key = 0;
    uint32_t slot = 0;
    glm::vec3 player_velocity{0.0f};
    glm::vec3 police_velocity{0.0f};
    glm::vec3 normal{0.0f};             // from police body toward player
};

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

// WHAT A PEDESTRIAN IS DOING. Plain data, and deliberately nothing else.
//
// This is the one field the presentation layer reads to choose what a person
// looks like, so it is a POD enum with explicit values and no presentation
// concept anywhere near it: no clip name, no blend weight, no animation timer.
// The sim decides the STATE; src/app/ decides what the state looks like. Put a
// clip in here and the two stop being separable, and the sim starts depending
// on which model happens to be loaded.
//
// The vocabulary is small on purpose. Each entry is a thing a person is
// visibly doing, distinguishable at fifty metres:
//
//   Walking  the default: making progress along the footway
//   Idling   stopped and loitering, of its own accord
//   Waiting  held at a kerb because crossing now would be stupid
//   Alarmed  has noticed a car bearing down and has not yet run
//   Fleeing  panic run, away from the kerb
//   Downed   knocked over, and not getting up this second
//   Rising   on the floor but getting up: stationary, and not panicking
//
// Alarmed and Fleeing are the two halves of city/traffic_ai.h's PanicPhase,
// which already exists, is already pure and is already pinned. They are not a
// second panic concept; see ped_panic_phase() in crowd.cpp.
enum class PedActivity : uint8_t {
    Walking = 0,
    Idling = 1,
    Waiting = 2,
    Alarmed = 3,
    Fleeing = 4,
    Downed = 5,
    Rising = 6,
};

const char* ped_activity_name(PedActivity a);

// The player car's collision half extents, mirroring
// VehicleTuning::car_collision_half_{width,length}. Two things in this header
// need them — the knockdown footprint and resolve_player_collision() — and a
// second literal is a second answer to "how big is the car".
inline constexpr float kPlayerHalfWidthM = 1.045f;
inline constexpr float kPlayerHalfLengthM = 2.445f;

struct PedAgent {
    uint64_t lane_key = 0;
    uint32_t slot = 0;
    // The schedule lap this walker departed on; see VehicleAgent::generation.
    int64_t generation = 0;

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

    // --- what this person is doing, and why -------------------------------
    //
    // `activity` is the readable state (above). The three fields under it are
    // the machine that drives it and are sim-side bookkeeping: read them if
    // you like, but `activity` is the contract.
    PedActivity activity = PedActivity::Walking;

    // The hidden nerve bucket, rolled ONCE at spawn from (map_seed, lane key,
    // slot) on kChannelPedNerve. city/pedestrian_reactions.h owns the meaning
    // and says explicitly that the roll belongs to the spawner and must be
    // keyed to spawn identity — so it is rolled in refresh(), never here and
    // never off a stream. It decides how easily this person panics, and it is
    // the same disposition the punch reaction uses. There is only one.
    ped_react::Disposition disposition = ped_react::Disposition::Coward;

    // Steps remaining in a timed activity — an idle, a kerb hesitation, the
    // time spent on the floor. STEPS, never seconds of wall time, for the same
    // reason every other clock in this engine counts steps.
    int64_t activity_steps = 0;

    // How many keyed activity rolls this person has made. It is an ORDINAL
    // OVER ITS OWN DECISIONS, not over the population, so it plays exactly the
    // role `walk_decisions` does: it varies the roll without letting the size
    // or the order of the active set reach it.
    uint32_t activity_decisions = 0;

    // panic_tick()'s remaining-panic timer, in seconds of SIM time. The kernel
    // guarantees it always decays, so a person cannot get stuck panicking.
    float panic_seconds = 0.0f;

    // --- the blow that put this person down --------------------------------
    //
    // Written once, on the step the knockdown lands, and valid for as long as
    // `activity` is Downed. `impact_dir_xz` is the incoming car or bullet's
    // direction: the way the body falls, NOT the way it was facing.
    // `impact_speed_mps` is the car closing speed or the shot's fall impulse.
    //
    // Recorded here rather than re-derived on the presentation side because by
    // the time anybody looks at a person on the floor the car has driven off,
    // and a fall that picks its direction from where the car is NOW throws the
    // body the wrong way down the street. A fall clip needs the direction to
    // choose front or back; a ragdoll needs the speed as well.
    glm::vec2 impact_dir_xz{0.0f};
    float impact_speed_mps = 0.0f;
    bool impact_from_bullet = false;

    // Where the body actually IS, as an offset in metres from the pose the
    // walk line would give it. Integrated as a point mass while Downed, then
    // walked off once this person is back on their feet.
    //
    // THE SIM HAS TO OWN THIS. A person struck at 14 m/s lands six metres down
    // the road, and where they land is sim state: it is where the get-up
    // happens, where the next car finds them, and where an officer sees them.
    // Leaving it to the presentation ragdoll — which runs on the render clock
    // — would make a pedestrian's position frame-rate dependent, and the
    // symptom before this existed was a body flying six metres and then
    // teleporting back to the kerb to stand up.
    glm::vec3 impact_offset{0.0f};
    glm::vec3 impact_velocity{0.0f};

    glm::vec3 pos{0.0f};
    glm::vec3 fwd{1.0f, 0.0f, 0.0f};
    NodeId node = kInvalidId;
};

// One shot, one victim. `officer` says which population the identity names: a
// police officer's (lane_key, slot) is his CRUISER's, and the two spaces are
// not disjoint, so a consumer that matches the pair alone would eventually
// attribute a cop's death to a civilian standing somewhere else.
struct PedShotHit {
    bool hit=false;
    glm::vec3 point{0.f};
    float distance=0.f;
    uint64_t lane_key=0;
    uint32_t slot=0;
    bool officer=false;
    // Set only on the round that put a police officer on the ground, so the
    // caller charges the heat for the kill and not for each round after it.
    bool officer_downed=false;
};

// True while this person is on their feet and making progress. The presentation
// layer wants "is this a walk cycle or something else" more often than it wants
// the specific state, and asking here keeps the answer in one place.
inline bool ped_is_moving(PedActivity a) {
    return a == PedActivity::Walking || a == PedActivity::Alarmed ||
           a == PedActivity::Fleeing;
}

// On the floor, either way: flat out or halfway up. Neither can walk, panic,
// cross a road or be thrown any further, and the three places that care all
// wanted the pair rather than one of them.
inline bool ped_is_floored(PedActivity a) {
    return a == PedActivity::Downed || a == PedActivity::Rising;
}

// The knobs behind PedActivity. Everything timed is in STEPS; everything the
// panic kernel needs is the kernel's own tuning, unmodified, because the panic
// kernel is shared with traffic and a second copy of its constants is a second
// answer to "what counts as reckless".
struct PedLifeTuning {
    // Loitering. A person rolls against this once when they reach a new stretch
    // of pavement, and once more at a keyed point part way along it, so idling
    // happens at shopfronts and corners rather than only at junctions.
    float idle_chance = 0.18f;
    int64_t idle_min_steps = 240;   // 2 s at 120 Hz
    int64_t idle_max_steps = 1200;  // 10 s

    // Kerb hesitation before stepping into a road, even when nothing is
    // coming. Without it a crossing reads as a person walking through traffic
    // with total confidence, which is the tell that nobody is deciding
    // anything.
    int64_t kerb_wait_min_steps = 30;   // 0.25 s
    int64_t kerb_wait_max_steps = 180;  // 1.5 s

    // How much road a person wants to see empty before stepping off the kerb.
    // Measured from the junction mouth along the carriageway being crossed.
    float crossing_clear_m = 14.0f;
    // ... and how slow an approaching car has to be to be ignored entirely,
    // so a stationary queue does not strand every pedestrian in the district.
    float crossing_ignore_speed_mps = 0.8f;

    // Reaction to a car. `threat` is the same bumper-path kernel traffic uses
    // to notice the player, evaluated the other way round: from the CAR's
    // heading, asking whether this person is in front of it. A cone drawn from
    // the pedestrian would never see a car coming up behind them, which is the
    // case that actually kills people.
    PlayerHazardTuning threat{22.0f, 2.4f, 2.0f, 2.4f, 6.0f};
    PanicTuning panic{};
    // Cowards spook at a lower closing speed, die-hards at a higher one. One
    // multiplier on the kernel's own gate, so there is still one definition of
    // "reckless" and this only says who agrees with it.
    float coward_closing_mul = 0.55f;
    float diehard_closing_mul = 1.55f;

    float alarm_speed_mul = 0.25f;  // noticed it; barely moving
    float flee_speed_mul = 2.1f;    // panic run
    // Pull-aside away from the kerb while fleeing, in metres. Clamped by
    // PED_SEPARATION_MAX_OFFSET on the way through ped_separation(), so this
    // can never walk somebody off the far side of the pavement.
    float flee_lateral_m = 0.85f;

    // Knockdown. A person whose body cylinder touches the player car's yaw
    // footprint, while it is moving this fast, goes down. It changes the
    // PERSON only — no impulse is applied to the car, because a pedestrian
    // that stops a vehicle is a worse bug than one that does not react at all.
    //
    // This used to be a 1.35 m circle around the car's CENTRE, and that is
    // most of a car length short of the bumper: the nose swept through people
    // without touching them and they went down when the door reached them,
    // half a second late and from the wrong direction. It is now
    // breakaway_contact() against the same half extents the car collides with,
    // which is the identical argument that function already makes about poles.
    float body_radius_m = 0.30f;   // shoulders plus a swinging arm
    float knockdown_speed_mps = 2.5f;
    int64_t downed_min_steps = 420;  // 3.5 s
    int64_t downed_max_steps = 900;  // 7.5 s

    // Getting back up, during which this person is STATIONARY.
    //
    // Without it the crowd put somebody straight back to walking the moment
    // the downed timer expired, while the presentation was still playing a
    // two-second get-up — so the body slid along the pavement and back toward
    // its lane in a pose that is on its hands and knees. It is the sim that
    // has to own the pause, because the sim owns the movement.
    //
    // 273 steps is 2.27 s, which is what the shipped get-up actually takes:
    // a 4.667 s clip at rate 1.35 with the first 1.60 s skipped. If that clip
    // or its rate changes, this is the number that has to follow it.
    int64_t rising_steps = 273;

    // The thrown body, as a point mass. This is not the ragdoll — the ragdoll
    // is presentation and follows this — it is the deterministic answer to
    // "where does this person end up", which the sim cannot delegate.
    //
    // `launch_forward` and `launch_lift` split the closing speed into travel
    // and arc; a bumper catches a person below the knee, so they go up as well
    // as along. `drag` bleeds airborne speed, `skid` is what the ground takes
    // per second once they are on it, and `bounce` keeps a fast body from
    // sticking to the first thing it touches.
    float launch_forward = 0.62f;
    float launch_lift = 0.30f;
    float launch_drag = 0.35f;
    float ground_skid = 4.6f;
    float ground_bounce = 0.22f;
    float gravity_mps2 = 17.5f;
    // How fast the offset is walked off once this person is upright again.
    // They came to rest in the road; they should return to the pavement on
    // their feet rather than slide back to it.
    float offset_recover_mps = 1.1f;
};

struct CrowdTuning {
    // Radii, in metres. Activate < retire, always: one radius means a player
    // idling on the boundary thrashes the same agent in and out forever, which
    // is the identical hysteresis argument the terrain streamer already makes.
    float vehicle_activate_m = 220.0f;
    float vehicle_retire_m = 320.0f;
    // RAISED FROM 110/160, and the number came off the bench rather than off
    // a feeling. tests/traffic_bench.cpp's ladder measures the shipping
    // vehicle radius (220 m) against a range of ped radii on one machine:
    // 110 m costs 0.153 ms of the 8.333 ms step (1.8%), 165 m costs 0.262 ms
    // (3.1%). A tenth of a millisecond is a cheap price for roughly twice the
    // pavement being populated, and 110 m is close enough that a person can
    // pop into existence inside the draw distance.
    float ped_activate_m = 165.0f;
    float ped_retire_m = 230.0f;
    // Kerbside parking has no hysteresis pair because it has no state to
    // thrash: a parked car that leaves the radius and comes back is bit-identical
    // to the one that left. One radius is the honest shape here.
    float parked_activate_m = 200.0f;

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
    float emergency_response_radius_m = 60.0f;

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

    // What people on the pavement do besides walk. See PedActivity.
    PedLifeTuning ped_life{};

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

DriverProfile traffic_driver_after_wait(const DriverProfile& profile,
                                        float delay_seconds);
float traffic_gap_margin_seconds(const DriverProfile& profile, float delay_seconds);
float traffic_travel_seconds(float distance_m, float speed_mps,
                             float acceleration_mps2, float speed_cap_mps);

struct TrafficApproachView {
    bool valid = false;
    bool committed = false;
    int priority = 0;
    float eta_seconds = std::numeric_limits<float>::infinity();
    int64_t arrival_step = -1;
    uint64_t lane_key = 0;
    uint32_t slot = 0;
    // Includes travel to the gate, the whole vehicle clearing, and the
    // driver's comfort margin. Infinity disables gap acceptance.
    float clearance_seconds = std::numeric_limits<float>::infinity();
};

// A committed car wins. Otherwise a lower-priority driver needs enough time
// to clear before priority traffic arrives; competing arrivals use a stable
// priority / arrival / identity order.
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

    // Pedestrians by activity, this step. Counted over the WHOLE active set
    // rather than over the sub-rate slice, so the numbers describe the street
    // and not the schedule.
    std::size_t peds_walking = 0;
    std::size_t peds_idling = 0;
    std::size_t peds_waiting = 0;
    std::size_t peds_alarmed = 0;
    std::size_t peds_fleeing = 0;
    std::size_t peds_downed = 0;

    // Ambient parked cars currently resident. Not agents; see AmbientParkedCar.
    std::size_t parked = 0;
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

    // App supplies accumulated snow demand, including after snowfall ends.
    // Disabling dispatch leaves existing trucks to finish their normal routes.
    void set_snowplow_service(bool needed) { snowplow_service_ = needed; }
    std::size_t snowplow_unit_count() const;

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
    // `player` optionally supplies car hazards; shoot_ped applies bullet
    // knockdowns separately. Passing nullptr is a street with no player car,
    // while existing downed people still advance through recovery.
    void step_peds(int64_t step, const VehicleState* player = nullptr);

    // Clip max_distance to the world raycast before calling. Both queries use
    // the same nearest standing person, and BOTH populations are standing
    // people: civilians on the pavement and police officers out of their
    // cruisers. Testing only one of them is how a pistol ends up firing
    // straight through the cop walking at you — the officer is not a
    // pedestrian agent, so nothing else in the crowd would ever see him.
    // One shot never passes through whoever it reaches first.
    PedShotHit raycast_ped(glm::vec3 origin,glm::vec3 unit_direction,
                          float max_distance) const;
    PedShotHit shoot_ped(glm::vec3 origin,glm::vec3 unit_direction,
                        float max_distance,int64_t step);

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

    void set_police_officer_context(bool target_on_foot, bool target_armed,
                                    glm::vec2 target_velocity,
                                    const TerrainCollider* world = nullptr);
    // Called only after an attributed player hit raises wanted heat. The
    // struck officer felt the contact, even when the player hit from behind.
    bool report_police_vehicle_hit(VisiblePoliceIdentity cruiser);
    void set_police_officer_vehicle_layout(const PoliceOfficerVehicleLayout& layout) {
        police_officer_layout_ = layout;
    }
    // The cruiser's own handling, so a free-driving pursuit corners and grips
    // like the car it is rather than a default. Weather belongs here too: the
    // host owns conditions, and a cop on ice must be on the same ice.
    void set_police_vehicle_tuning(const VehicleTuning& tuning) {
        police_vehicle_tuning_ = tuning;
    }
    const std::vector<PolicePlayerContact>& police_player_contacts() const {
        return police_player_contacts_;
    }
    const std::vector<PoliceShotEvent>& police_shots() const {
        return police_shots_;
    }

    // Resolve the player against active traffic bodies. Both bodies receive
    // separation and impulse; traffic carries its reaction as a deterministic
    // world-space offset over the lane pose, then drives forward to rejoin.
    bool resolve_player_collision(VehicleState& player,
                                  float player_half_width_m = kPlayerHalfWidthM,
                                  float player_half_length_m = kPlayerHalfLengthM,
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
    // Sorted by (lane key, slot), so it is a property of the population and
    // not of the lane scan that assembled it.
    const std::vector<AmbientParkedCar>& ambient_parked() const {
        return ambient_parked_;
    }
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

    // How many identities the retirement set holds. One entry per (lane, slot)
    // ever retired, NOT one per retirement — the bound this policy has to keep.
    std::size_t retired_identity_count() const { return retired_.size(); }
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
        float delay_seconds = 0.0f;
    };

    void dispatch_snowplows(glm::vec2 player_xz);
    void build_lane_index();
    void gather_lanes(glm::vec2 xz, float radius_m, std::vector<LaneRef>& out) const;
    int sub_rate_phase(uint64_t lane_key, uint32_t slot, uint32_t index,
                       uint32_t spawn_ordinal, int k) const;
    void update_police_response(int64_t step);
    void update_police_route(VehicleAgent& agent, LaneRef target_lane,
                             glm::vec2 route_target, int64_t step);
    bool police_has_line_of_sight(const VehicleAgent& agent) const;
    void step_police_officers(const VehicleState* player,
                              const OnFootTrafficHazard* on_foot_player,
                              int64_t step);
    void resolve_vehicle_collisions();
    void prepare_emergency_maneuvers(int64_t step, const VehicleState* player,
                                    const OnFootTrafficHazard* on_foot_player);
    bool step_emergency_maneuver(uint32_t index, float dt);
    bool emergency_path_clear(uint32_t index, const TrafficManeuver& move,
                              float start_m, float distance_m,
                              bool check_world, bool ignore_player = false) const;
    float emergency_obstacle_speed(uint32_t index) const;
    struct EmergencyBody {
        glm::vec3 pos{0}, fwd{1, 0, 0};
        float speed = 0, half_width = 1.15f, half_length = 2.5f;
        TrafficManeuver move{};
    };
    std::vector<EmergencyBody> emergency_frozen_;
    std::vector<EmergencyBody> emergency_obstacles_;
    // Where the player's car sits in emergency_obstacles_, or npos when there
    // is no player car this step. A ram arc has to be allowed to end inside
    // that body; every other obstacle still vetoes it.
    std::size_t emergency_player_obstacle_ = static_cast<std::size_t>(-1);
    std::vector<uint32_t> emergency_reservations_;
    float junction_clearance(uint32_t junction) const;
    float junction_turn_clearance(uint32_t junction) const;
    bool movements_conflict(LaneRef from, LaneRef to,
                            LaneRef other_from, LaneRef other_to) const;
    bool is_retired(uint64_t lane_key, uint32_t slot, int64_t lap) const;
    void retire(uint64_t lane_key, uint32_t slot, int64_t lap);

    // May this person step off the kerb at the end of `foot_lane` right now?
    // Reads the SIGNAL (the same shared clock the bulbs and the drivers use)
    // and the FROZEN lane buckets, never live agent state, so two crowds that
    // assembled the same population in different orders answer identically.
    bool ped_crossing_is_clear(LaneRef foot_lane, int64_t step) const;

    // Retired identities, sorted, EXACT. A packed-and-hashed key would be
    // smaller and would occasionally suppress a car that was never retired,
    // once, somewhere, unreproducibly.
    struct RetiredId {
        uint64_t key;
        uint32_t slot;
        // WHICH DEPARTURE, not just which slot. A slot re-departs once per
        // schedule period; banning the pair alone destroyed the recurring slot
        // rather than the one car that was simulated, and a stationary player
        // watched the whole neighbourhood drain. See phantom_lap().
        int64_t lap;
    };
    static bool retired_id_less(const RetiredId& a, const RetiredId& b);

    const LaneGraph* graph_ = nullptr;
    uint64_t map_seed_ = 0;
    AmbientTuning ambient_{};
    CrowdTuning tuning_{};

    std::vector<LaneSchedule> veh_sched_;  // indexed by LaneRef
    std::vector<LaneSchedule> ped_sched_;
    std::vector<ParkedLaneBay> parked_bays_;  // indexed by LaneRef
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
    std::vector<AmbientParkedCar> ambient_parked_;
    std::vector<glm::vec3> parked_vehicle_positions_;
    std::vector<PedAgent> peds_;

    bool snowplow_service_ = false;
    bool snowplow_envelope_active_ = false;
    uint32_t snowplow_dispatch_serial_ = 0;
    int police_wanted_level_ = 0;
    glm::vec2 police_target_xz_{0.0f};
    std::vector<VisiblePoliceIdentity> visible_police_;
    bool police_response_due_ = false;
    int64_t police_next_response_step_ = 0;
    bool police_officers_enabled_ = false;
    bool police_target_on_foot_ = false;
    bool police_target_armed_ = false;
    glm::vec2 police_target_velocity_{0.0f};
    float police_target_speed_mps_ = 0.0f;
    const TerrainCollider* police_officer_world_ = nullptr;
    PoliceOfficerVehicleLayout police_officer_layout_{};
    VehicleTuning police_vehicle_tuning_{};
    // How far the suspect is from the nearest lane centreline. When he is off
    // the road the lane path CANNOT reach him, so this widens the free-drive
    // hand-off instead of leaving cruisers parked on the tarmac.
    float police_target_offroad_m_ = 0.0f;
    // Returns true when this agent drove itself this step and the lane path
    // must be skipped entirely.
    bool step_police_free_chase(VehicleAgent& agent, float dt);
    std::vector<PolicePlayerContact> police_player_contacts_;
    std::vector<PoliceShotEvent> police_shots_;

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
    std::vector<float> snowplow_junction_clearance_m_;
    std::vector<float> snowplow_junction_turn_clearance_m_;
    std::set<std::array<LaneRef, 4>> snowplow_movement_conflicts_;

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
