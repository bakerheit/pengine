#pragma once

#include <algorithm>
#include <cstdint>
#include <cstddef>

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
// What a pedestrian does besides walk (traffic/crowd.h, PedActivity). Four
// channels and not one, because "do I stop here", "for how long", "where along
// this pavement" and "how long am I on the floor" are four unrelated
// questions: share a channel between them and a person who stops for a long
// time always stops in the same place, which reads as a pattern immediately.
inline constexpr uint32_t kChannelPedIdleChance    = 0x2500u;
inline constexpr uint32_t kChannelPedIdleLength    = 0x2600u;
inline constexpr uint32_t kChannelPedLoiterAt      = 0x2700u;
inline constexpr uint32_t kChannelPedKerbWait      = 0x2800u;
inline constexpr uint32_t kChannelPedDowned        = 0x2900u;
// Where a parked car sits along its kerb, and which way round it is.
inline constexpr uint32_t kChannelParkedAlong      = 0x2A00u;
inline constexpr uint32_t kChannelParkedFacing     = 0x2B00u;

// Presentation and collision choose the same legacy body from stable phantom
// identity. Keeping this recipe here prevents the renderer from showing a
// narrow sedan around a widest-model collision box (the visible air-gap bug)
// while preserving activation/order determinism.
enum class TrafficVehicleKind : uint8_t {
    Sedan = 0,
    BoxTruck = 1,
    Ambulance = 2,
    Firetruck = 3,
    HalcyonSix = 4,
    MontroseRegentEight = 5,
    VesperVx91 = 6,
    Police = 7,
    Snowplow = 8,
};

inline constexpr float kSnowplowBladeForwardM = 2.95f;
inline constexpr float kSnowplowBladeWidthM = 2.70f;
inline constexpr float kSnowplowWorkSpeedMps = 6.0f;
inline constexpr std::size_t kMaxSnowplowFleet = 4;

struct TrafficVehicleFootprint {
    float half_width_m = 1.0f;
    float half_length_m = 2.5f;
};

inline uint64_t traffic_vehicle_identity_hash(uint64_t lane_key,
                                              uint32_t slot) {
    return splitmix64_mix(lane_key ^
        (static_cast<uint64_t>(slot) << 32));
}

inline TrafficVehicleKind traffic_vehicle_kind(uint64_t lane_key,
                                               uint32_t slot) {
    const uint64_t identity = traffic_vehicle_identity_hash(lane_key, slot);
    const uint32_t roll = static_cast<uint32_t>(
        identity % 30u);
    const uint32_t legacy_roll = roll % 15u;
    if (legacy_roll < 10u) {
        const uint32_t sedan_variant = static_cast<uint32_t>(
            (identity / 30u) % 4u);
        if (sedan_variant == 0u) return TrafficVehicleKind::HalcyonSix;
        if (sedan_variant == 1u) {
            return TrafficVehicleKind::MontroseRegentEight;
        }
        if (sedan_variant == 2u) return TrafficVehicleKind::VesperVx91;
        return TrafficVehicleKind::Sedan;
    }
    if (legacy_roll < 14u) return TrafficVehicleKind::BoxTruck;
    return roll == 14u ? TrafficVehicleKind::Ambulance
                       : TrafficVehicleKind::Firetruck;
}

// Which body is parked at this kerb slot. The moving-traffic recipe, keyed on
// a different word of the identity so a parked car and the car that would have
// driven that slot are not the same model, and with the emergency bodies
// folded back to a sedan: an ambulance does not sit unattended at a kerb, and
// a police car standing there means something the police module has not said.
//
// THIS LIVES IN THE HEADER because two things have to agree about it: the
// resident builder that places the bodies, and the bay gate that decides
// whether a kerb has room for them. It used to be file-local to crowd.cpp, so
// the gate reasoned about a nominal 0.95 m body while this put a 1.15 m truck
// there — see widest_parked_half_width_m().
inline TrafficVehicleKind parked_vehicle_kind(uint64_t lane_key, uint32_t slot) {
    const TrafficVehicleKind kind =
        traffic_vehicle_kind(lane_key ^ 0x5041524B4544ull, slot);
    switch (kind) {
        case TrafficVehicleKind::Ambulance:
        case TrafficVehicleKind::Firetruck:
        case TrafficVehicleKind::Police:
            return TrafficVehicleKind::Sedan;
        default:
            return kind;
    }
}

inline TrafficVehicleFootprint traffic_vehicle_footprint(
    TrafficVehicleKind kind) {
    // Legacy bodies measured after make_traffic_visual_layout() 5 m fit;
    // the authored service truck keeps its full body and blade dimensions.
    switch (kind) {
        case TrafficVehicleKind::Sedan: return {0.943954f, 2.5f};
        case TrafficVehicleKind::BoxTruck: return {1.148594f, 2.5f};
        case TrafficVehicleKind::Ambulance: return {0.964955f, 2.5f};
        case TrafficVehicleKind::Firetruck: return {0.976563f, 2.5f};
        case TrafficVehicleKind::HalcyonSix: return {0.946970f, 2.5f};
        case TrafficVehicleKind::MontroseRegentEight:
            return {0.929577f, 2.5f};
        case TrafficVehicleKind::VesperVx91: return {0.970497f, 2.5f};
        case TrafficVehicleKind::Police: return {1.018182f, 2.5f};
        case TrafficVehicleKind::Snowplow: return {1.35f, 3.3f};
    }
    return {1.0f, 2.5f};
}

// THE WIDEST BODY THAT CAN ACTUALLY END UP AT A KERB.
//
// The bay gate below is deciding whether a road has room for the car that will
// really be parked there, and parked_vehicle_kind() can put a box truck in any
// slot. The gate used to reason about AmbientTuning::parked_half_width_m — a
// nominal 0.95 m — while the runtime hazard test used the real footprint, up
// to 1.15 m. The two disagreed by 0.20 m, which is how a bay could pass a
// 1.00 m clearance gate and still put a body 1.10 m from the lane centre.
//
// The set is the image of traffic_vehicle_kind() under parked_vehicle_kind()'s
// fold. tests/parked_density_tests.cpp brute-forces the identity space against
// this number, so it cannot drift away from either function.
inline float widest_parked_half_width_m() {
    float widest = 0.0f;
    for (TrafficVehicleKind k : {TrafficVehicleKind::Sedan,
                                 TrafficVehicleKind::BoxTruck,
                                 TrafficVehicleKind::HalcyonSix,
                                 TrafficVehicleKind::MontroseRegentEight,
                                 TrafficVehicleKind::VesperVx91})
        widest = std::max(widest, traffic_vehicle_footprint(k).half_width_m);
    return widest;
}

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

    // WHERE THE CROWD CLUMPS, and why it is junction arity and not something
    // that sounds more like "interest".
    //
    // A uniform ped density over every metre of pavement reads as a conveyor
    // belt: a cul-de-sac behind a warehouse carries exactly as many people as
    // the four-way in the middle of the shopping street. Corners are where the
    // shopfronts are, and how many roads meet at a corner is authored — it is
    // the map — so a lane running between two crossroads carries more people
    // than one that dead-ends.
    //
    // Lane::block_quality was the obvious candidate and it is the WRONG one:
    // city/roads.h authors it as ROADBLOCK STAGING QUALITY ("0 means never
    // stage here; 255 means this is what this road is for"), which describes a
    // long open road with clear sightlines. That is the opposite of a busy
    // shopfront street, so using it would have clumped the crowd onto exactly
    // the roads it should have thinned.
    //
    // The gain multiplies the district's authored ped_density. Keep the two
    // ends either side of 1.0 or this quietly becomes a global population
    // knob wearing a clumping name.
    // These two are CENTRED ON THE REAL ISLAND, not chosen to look tidy. The
    // first pass used 0.55 / 1.85, which reads as a symmetric spread about 1.0
    // and is not: most of Pinatty's junctions are three- or four-way, so the
    // length-weighted mean gain came out at 1.38 and the island's pedestrian
    // total moved by 28%. That is a density change wearing a clumping name,
    // and it would have quietly invalidated every measured cost beside it.
    // tests/ped_life_tests.cpp measures the mean and fails if it drifts again.
    float ped_hotspot_quiet = 0.40f;  // a lane whose both ends are stubs
    float ped_hotspot_busy = 1.35f;   // a lane between two four-way corners

    // --- kerbside parking ---------------------------------------------------
    //
    // Nose-to-tail spacing at density 1.0. The lane's authored parked_density
    // divides it, exactly the way traffic_density divides vehicle_spacing_m,
    // so a district authored full of parked cars is full because
    // city/districts.h says so.
    float parked_spacing_m = 11.0f;
    uint32_t max_parked_slots = 40;

    // THE NOMINAL SLOT, which PLACES a body and does not measure one. Together
    // these put the car centre at `carriageway_half - kerb_gap - half_width`
    // from the road centreline, the same station for every body, so a sedan
    // ends up 0.30 m off the kerb and a box truck 0.10 m — both on the
    // carriageway, neither floating out in the road. Sizing the slot to each
    // body instead was tried and is worse: a wide body pushed to its own kerb
    // gap reaches FURTHER into the lane, not less.
    //
    // It is not the number the clearance gate reads. That one asks what will
    // really be parked here and uses widest_parked_half_width_m().
    float parked_half_width_m = 0.95f;
    float parked_kerb_gap_m = 0.30f;

    // HOW MUCH ROOM A PARKED CAR MUST LEAVE THE TRAFFIC, measured from the
    // travel lane's centreline to the parked car's near side. This is the gate
    // that decides which roads get kerbside parking at all, and it is a gate
    // rather than a nudge for one reason: nothing here is in the moving cars'
    // obstacle set, so a parked car that overlaps a lane centre is a car the
    // AI drives straight through. Measured against the widest body that can
    // actually park (widest_parked_half_width_m), the authored class table
    // admits Streets (1.10 m of clearance on a 14 m carriageway) and excludes
    // Arterials (0.35 m), which is also where a city would paint the bays.
    //
    // 1.10 m is LESS than a box truck needs to pass at cruise, and that is not
    // an oversight: a 14 m street is genuinely about 10 cm too narrow for a
    // truck to pass a parked truck, so the driver goes round instead. What
    // this gate forbids is the case where going round is impossible too.
    float parked_lane_clearance_m = 1.00f;

    // No parking across a junction mouth. Measured from each end of the lane.
    float parked_junction_setback_m = 9.0f;

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

// Minimum centre-to-centre spacing for a free-running traffic schedule. Fast
// lanes need enough road to stop even when the nominal density asks for more
// cars; otherwise an analytic freeway platoon becomes a pileup the first time
// its leader encounters a junction queue.
float traffic_vehicle_spacing_m(float nominal_spacing_m, float density,
                                float speed_mps);

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

// How much busier than baseline this lane's pavements are, from the arity of
// the junctions at its two ends. Pure in (graph, lane) — it is a fact about
// the authored map, so it carries no seed and no step.
float ped_hotspot_gain(const LaneGraph& graph, LaneRef lane,
                       const AmbientTuning& t);

// The pedestrian schedule for one lane's pair of footways. `slots` counts BOTH
// sides: even slots ride the right-hand footway, odd slots the left.
//
// `density_gain` is ped_hotspot_gain()'s answer, threaded in rather than
// looked up, so this stays a pure function of one lane and the caller keeps
// the graph. It defaults to 1.0, which is a flat, unclumped city.
LaneSchedule ped_schedule(uint64_t map_seed, const Lane& lane,
                          const AmbientTuning& t, float density_gain = 1.0f);

// Slot k of `sched` at absolute sim step `step`. Pure. No state, no clock, no
// dependence on which slots have been asked about before.
PhantomState phantom_vehicle(uint64_t map_seed, const Lane& lane,
                             const LaneSchedule& sched, uint32_t slot,
                             int64_t step, const AmbientTuning& t);

PhantomState phantom_ped(uint64_t map_seed, const Lane& lane,
                         const LaneSchedule& sched, uint32_t slot,
                         int64_t step, const AmbientTuning& t);

// Which LAP of its schedule slot `slot` is on at absolute step `step`.
//
// A phantom slot is not one car. `phantom_*` takes the REMAINDER of
// (step - depart - slot*headway) over the period; this is the matching
// QUOTIENT, so `lap * period + phase` reconstructs the numerator exactly. Slot
// k re-departs the lane once per period, and each departure is a different car
// that no one has simulated.
//
// This is what separates the two identities retirement has to tell apart: the
// instance that was perturbed and left, which must never come back, and the
// next departure of the same slot, which was never simulated and is described
// by the closed form exactly. Pure in (step, schedule) — no clock, no RNG.
int64_t phantom_lap(const LaneSchedule& sched, uint32_t slot, int64_t step);

// ---------------------------------------------------------------------------
//  Kerbside parking
// ---------------------------------------------------------------------------
//
// PARKED CARS ARE NOT AGENTS, and the distinction is the whole reason they get
// their own three functions instead of a third phantom schedule.
//
// A phantom has a position that depends on the step. A parked car does not
// depend on the step at all: it is a pure function of (map_seed, lane, slot),
// so there is nothing to promote, nothing to integrate, and nothing to retire.
// It costs one hash to place and it is identical on every machine on every run
// forever.
//
// That also fixes the boundary. The day one of these becomes something a
// player can shunt, steal or blow up, it stops being describable by this
// function and has to become a real agent with permanent retirement — the
// identical argument traffic/README.md makes about promotion. It is cheap
// street density precisely BECAUSE it has no state, and the moment it has
// state it is not this any more.
struct ParkedLaneBay {
    // TWO SEPARATE ANSWERS, and keeping them apart matters more than it looks.
    //
    // `lateral_m` non-zero means THE ROAD HAS ROOM: it has a kerb, this is its
    // outermost lane, and a parked body there still leaves the traffic the
    // authored clearance. That is a fact about the road's geometry and the
    // class table, and no district can change it.
    //
    // `slots` is how many cars the DISTRICT chose to put in that room. It can
    // be zero on a road with plenty of room — Marrow authors 0.05 and its
    // kerbs are meant to be empty.
    //
    // Collapsing the two into "slots == 0" makes every measurement of authored
    // density secretly a measurement of how many arterials a district has,
    // which is the shape the first draft of tests/parked_density_tests.cpp
    // came out and the reason it read as non-monotonic.
    uint32_t slots = 0;
    // Signed lateral offset in the LANE's own frame: positive is to the right
    // of travel, which is the kerb side. Zero when the road has no room.
    float lateral_m = 0.0f;
    float first_m = 0.0f;   // where the bay starts along the lane
    float pitch_m = 0.0f;   // centre-to-centre spacing of the slots
    float usable_m = 0.0f;  // kerb left after the junction setbacks
};

// The bay one lane carries. Pure in (lane, tuning) — no seed, because how much
// room a road has and how busy its district is are both authored facts.
ParkedLaneBay parked_lane_bay(const Lane& lane, const AmbientTuning& t);

struct ParkedSlot {
    float dist_along_m = 0.0f;
    float lateral_m = 0.0f;
    // Parked facing back down the lane. Roughly a third of them, because a
    // street on which every car faces the same way reads as a car park.
    bool reversed = false;
};

ParkedSlot parked_slot(uint64_t map_seed, const Lane& lane,
                       const ParkedLaneBay& bay, uint32_t slot,
                       const AmbientTuning& t);

}  // namespace apricot
