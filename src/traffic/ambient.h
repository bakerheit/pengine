#pragma once

#include <cstdint>

#include <glm/glm.hpp>

#include "core/rng.h"
#include "road/lane_graph.h"

namespace apricot {

// THE AMBIENT POPULATION IS DEFINED, NOT SIMULATED.
//
// Every directed lane carries a fixed number of phantom slots. Slot k on lane L
// has a nominal speed and a departure step derived from
// hash_coord3(map_seed, low32(L.key), high32(L.key), channel ^ slot), and its
// position at sim step t is a CLOSED FORM in t. Nothing integrates, so nothing
// has history, so there is nothing an approach direction can take away.
//
// That is the whole trick, and it is aimed at one specific failure. "Simulate
// only what is near the player" is fine on its own — the player's position is a
// pure function of seed and inputs. What breaks is PROMOTION: a car advanced
// cheaply and then handed to the full simulation is not in the state it would
// have been in had it been simulated all along, and no amount of care makes an
// approximation match an integrated truth. Here there is no approximation to
// promote from. A phantom instantiates AT its closed form, exactly, at whatever
// step the player happens to arrive.
//
// A phantom's identity is (lane key, slot), never an index into anything. Lane
// keys come off the authored spine, so they survive a rebuild and survive
// reordering the spine table; the slot is an ordinal within the lane. Which car
// you meet therefore does not depend on how many cars were spawned before it,
// which is the property a sequential stream destroys and the reason
// choose_next() takes (seed, decision_index) rather than a generator.

// Channel allocation. terrain/noise.h owns 0x0100..0x0FFF, city_rng.h took
// 0x1000 up. Traffic takes 0x2000 up. Two decisions that share a channel share
// their entropy, and correlated "random" choices read as "why is every car on
// this street the same colour" — add a channel rather than reusing one.
inline constexpr uint32_t kChannelPhantomLaneSpeed = 0x2000u;
inline constexpr uint32_t kChannelPhantomDepart    = 0x2100u;
inline constexpr uint32_t kChannelPhantomSlotSpeed = 0x2200u;
inline constexpr uint32_t kChannelPhantomPedSpeed  = 0x2300u;
inline constexpr uint32_t kChannelPhantomPedSide   = 0x2400u;

// How dense the ambient population is and how fast it moves. Everything here
// scales the SCHEDULE, so changing any of it moves every phantom in the world —
// it is a world parameter, not a per-agent one.
struct AmbientTuning {
    // Nominal along-lane gap between consecutive vehicle slots at density 1.0.
    // The lane's authored traffic_density divides it: a district authored busy
    // is busy because it was authored busy, not because a spawner landed there.
    float vehicle_spacing_m = 34.0f;
    float ped_spacing_m     = 11.0f;

    // Ceiling per lane per direction. A short alley with a huge density scalar
    // must not become a car park.
    uint32_t max_vehicle_slots = 32;
    uint32_t max_ped_slots     = 48;

    // Vehicle cruise as a fraction of the lane's speed limit.
    float speed_lo = 0.82f;
    float speed_hi = 1.04f;

    // Pedestrian walking speed, absolute (m/s).
    float ped_speed_lo = 1.10f;
    float ped_speed_hi = 1.70f;

    // Where the footway sits, measured out from the carriageway centreline:
    // half the road width plus this. Peds ride the vehicle lane's arc because
    // there IS no pedestrian lane network yet (src/road/README.md says so
    // explicitly, and says why). This is a stand-in for its GEOMETRY. It is not
    // a stand-in for its COST: the per-ped work below — one pose(), one
    // project-free advance, one separation solve against a gathered neighbour
    // list — is the work the real network will also demand.
    float sidewalk_offset_m = 2.2f;

    // THE EXPERIMENT, AND THE REASON IT IS A FLAG.
    //
    // false: every slot on a lane shares one lane speed, so consecutive slots
    //        hold a constant headway and NEVER close on each other. The lane is
    //        non-interacting by construction and its phantoms stay on their
    //        closed form indefinitely.
    // true:  each slot draws its own speed, which looks better and means a fast
    //        slot eventually catches a slow one — at which point somebody has to
    //        brake, and a car that brakes has history.
    //
    // Both are shipped because the difference between them is measurable and it
    // is the difference between "distant traffic is free" and "distant traffic
    // is free until it is not". tests/traffic_bench.cpp reports the rate.
    bool per_slot_speed = false;
};

// Where a phantom is at step t. Positions are LANE-LOCAL: the caller turns
// (lane, dist_along_m, lateral_m) into a world pose with LaneGraph::pose(),
// which is the one place the sign convention for `lateral_m` lives.
struct PhantomState {
    float dist_along_m = 0.0f;
    float speed_mps    = 0.0f;
    float lateral_m    = 0.0f;
};

// The per-lane schedule. Derived once per lane and cached by the caller,
// because it is a pure function of the lane and the tuning and recomputing it
// per step per slot is the kind of waste that only shows up at city scale.
//
// `period_steps` is the loop: slot k is at phase ((t - depart) mod period), and
// the geometry guarantees period <= run_steps, so every slot is on its lane at
// every step. A schedule that let slots blink in and out would need a presence
// test in the hot loop and would make the population count a function of t.
struct LaneSchedule {
    uint32_t slots        = 0;    // phantoms resident on this lane, always
    int64_t  headway_steps = 1;   // steps between consecutive slots
    int64_t  period_steps  = 1;   // slots * headway_steps
    int64_t  depart_step   = 0;   // phase offset of slot 0, in [0, period)
    float    speed_mps     = 0.0f;  // lane speed (per_slot_speed == false)
    float    length_m      = 0.0f;
};

// Keyed entropy for one phantom. The recipe is the one lane_graph.h prescribes:
// the lane's STABLE key split across the two coordinate axes, the slot folded
// into the channel. The slot goes in the channel and not in a coordinate
// because the coordinates carry spatial structure that hash_coord's two odd
// multipliers exist to separate, and burying an ordinal in them throws that
// away.
constexpr uint64_t phantom_key(uint64_t map_seed, uint64_t lane_key,
                               uint32_t slot, uint32_t channel) {
    return hash_coord3(map_seed,
                       static_cast<int32_t>(static_cast<uint32_t>(lane_key)),
                       static_cast<int32_t>(static_cast<uint32_t>(lane_key >> 32)),
                       channel ^ (slot * 0x9E3779B9u));
}

// The vehicle schedule for one lane. Deterministic in (map_seed, lane, tuning)
// and in nothing else.
LaneSchedule vehicle_schedule(uint64_t map_seed, const Lane& lane,
                              const AmbientTuning& t);

// The pedestrian schedule for one lane's pair of footways. `slots` counts BOTH
// sides: even slots ride the right-hand footway, odd slots the left.
LaneSchedule ped_schedule(uint64_t map_seed, const Lane& lane,
                          const AmbientTuning& t);

// Slot k of `sched` at absolute sim step `step`. Pure. No state, no clock, no
// dependence on which slots have been asked about before.
PhantomState phantom_vehicle(uint64_t map_seed, const Lane& lane,
                             const LaneSchedule& sched, uint32_t slot,
                             int64_t step, const AmbientTuning& t);

PhantomState phantom_ped(uint64_t map_seed, const Lane& lane,
                         const LaneSchedule& sched, uint32_t slot,
                         int64_t step, const AmbientTuning& t);

}  // namespace apricot
