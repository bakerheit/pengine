#include "traffic/ambient.h"

#include <algorithm>
#include <cmath>

#include "core/fixed_step.h"

namespace apricot {
namespace {

constexpr float kSimDtF = static_cast<float>(kSimDt);

// Steps a phantom takes to traverse `length_m` at `v`. At least one, because a
// zero-length run makes the period zero and the modulo below undefined. Lanes
// of zero length do not exist in a built graph, but a schedule that divides by
// a number it did not check is a crash waiting for the first degenerate spine
// somebody authors.
int64_t run_steps_for(float length_m, float v) {
    if (!(v > 0.0f) || !(length_m > 0.0f)) return 1;
    const double n = std::ceil(static_cast<double>(length_m) /
                               (static_cast<double>(v) * kSimDt));
    if (!(n > 0.0)) return 1;
    return static_cast<int64_t>(std::min(n, 1.0e9));
}

// Non-negative modulo. `step` is absolute and a tape can legitimately start
// before the schedule's epoch, so the C++ sign-of-the-dividend rule would put a
// phantom at a negative distance — which reads on screen as a car parked at the
// junction it should be leaving.
int64_t wrap(int64_t v, int64_t period) {
    const int64_t r = v % period;
    return r < 0 ? r + period : r;
}

LaneSchedule build_schedule(uint64_t map_seed, const Lane& lane, float gap_m,
                            uint32_t max_slots, float speed) {
    LaneSchedule s;
    s.length_m = lane.length_m;
    s.speed_mps = speed;

    if (!(gap_m > 0.0f) || !(speed > 0.0f) || !(lane.length_m > 0.0f)) return s;

    // THE LENGTH IS QUANTISED BEFORE IT REACHES A CEIL, AND THAT IS NOT
    // FUSSINESS.
    //
    // `run` is ceil(length / (v * dt)) — a STEP FUNCTION of a float. It feeds
    // the headway, which feeds the period, which feeds every phantom's phase,
    // so a sub-millimetre wobble in the measured arc length moves cars by
    // centimetres and flips whether one of them has to brake. Measured on this
    // tree: rebuilding the same district from a REORDERED spine table changes
    // 972 of 6,240 lane lengths, by up to 0.000244 m — geometrically nothing,
    // and enough to walk straight through the ceil and out the other side.
    //
    // Flooring to 1/16 m first means a length has to move across a 6.25 cm
    // tread before the schedule notices, which takes the exposure from about a
    // third of all lanes to well under one percent. It REDUCES the exposure; it
    // does not remove it. The actual fix is for the lane graph to be
    // bit-identical under a spine reorder, which is a road-module property and
    // not this module's to change.
    const float len_q = std::floor(lane.length_m * 16.0f) * (1.0f / 16.0f);
    const int64_t run = run_steps_for(len_q, speed);

    int64_t headway = static_cast<int64_t>(
        std::lround(static_cast<double>(gap_m) /
                    (static_cast<double>(speed) * kSimDt)));
    if (headway < 1) headway = 1;

    // Slots that fit on the lane at once. floor(), so slots * headway <= run
    // and therefore period <= run: every slot is always somewhere on its lane.
    int64_t slots = run / headway;
    if (slots < 0) slots = 0;
    if (slots > static_cast<int64_t>(max_slots))
        slots = static_cast<int64_t>(max_slots);
    if (slots == 0) return s;

    // Capping the slot count without re-deriving the headway would bunch every
    // capped lane at the near end. Re-spread instead, which keeps the constant
    // headway that makes a lane non-interacting.
    headway = run / slots;
    if (headway < 1) headway = 1;

    s.slots = static_cast<uint32_t>(slots);
    s.headway_steps = headway;
    s.period_steps = slots * headway;

    const uint64_t h = phantom_key(map_seed, lane.key, 0u, kChannelPhantomDepart);
    s.depart_step = static_cast<int64_t>(h % static_cast<uint64_t>(s.period_steps));
    return s;
}

// Per-slot speed spread, as a multiplier either side of the lane speed. Kept
// symmetric about 1.0 so turning per_slot_speed on changes the VARIANCE of the
// ambient population and not its mean — otherwise the two modes would also
// differ in how many cars a lane holds, and the measurement comparing them
// would be measuring two things at once.
constexpr float kSlotSpreadLo = 0.88f;
constexpr float kSlotSpreadHi = 1.12f;

float slot_speed(uint64_t map_seed, const Lane& lane, uint32_t slot,
                 float lane_speed, uint32_t channel, bool per_slot) {
    if (!per_slot) return lane_speed;
    Rng r{phantom_key(map_seed, lane.key, slot, channel)};
    return lane_speed * r.range(kSlotSpreadLo, kSlotSpreadHi);
}

}  // namespace

float traffic_vehicle_spacing_m(float nominal_spacing_m, float density,
                                float speed_mps) {
    const float authored = std::max(nominal_spacing_m, 1.0f) /
                           std::max(density, 0.01f);
    constexpr float kTrafficEmergencyBrakeMps2 = 8.0f;
    constexpr float kBodyAndMarginM = 6.0f;
    const float stopping = kBodyAndMarginM +
        std::max(0.0f, speed_mps) * std::max(0.0f, speed_mps) /
            (2.0f * kTrafficEmergencyBrakeMps2);
    return std::max(authored, stopping);
}

LaneSchedule vehicle_schedule(uint64_t map_seed, const Lane& lane,
                              const AmbientTuning& t) {
    Rng r{phantom_key(map_seed, lane.key, 0u, kChannelPhantomLaneSpeed)};
    const float v = lane.speed_limit_mps * r.range(t.speed_lo, t.speed_hi);
    const float spacing = traffic_vehicle_spacing_m(
        t.vehicle_spacing_m, lane.traffic_density, v);
    return build_schedule(map_seed, lane, spacing, t.max_vehicle_slots, v);
}

float ped_hotspot_gain(const LaneGraph& graph, LaneRef lane,
                       const AmbientTuning& t) {
    if (!graph.valid(lane)) return 1.0f;
    const Lane& l = graph.lane(lane);
    auto arms = [&](uint32_t j) {
        // Capped at four: a five-way is not two and a half times busier than a
        // crossroads, and one freak junction should not dominate the map.
        return j < graph.junction_count()
            ? std::min<uint32_t>(graph.junction(j).degree, 4u) : 1u;
    };
    // 1 is a stub at both ends, 4 is a crossroads at both.
    const float corners = 0.5f * (static_cast<float>(arms(l.junction_from)) +
                                  static_cast<float>(arms(l.junction_to)));
    const float u = std::clamp((corners - 1.0f) / 3.0f, 0.0f, 1.0f);
    return t.ped_hotspot_quiet + (t.ped_hotspot_busy - t.ped_hotspot_quiet) * u;
}

LaneSchedule ped_schedule(uint64_t map_seed, const Lane& lane,
                          const AmbientTuning& t, float density_gain) {
    Rng r{phantom_key(map_seed, lane.key, 0u, kChannelPhantomPedSpeed)};
    const float v = r.range(t.ped_speed_lo, t.ped_speed_hi);
    // Two footways, so the same along-lane spacing yields twice the slots.
    const float density =
        std::max(lane.ped_density * 2.0f * std::max(density_gain, 0.0f), 0.0f);
    const float spacing = density > 0.0f
        ? std::max(t.ped_spacing_m / density, 1.0f) : 0.0f;
    return build_schedule(map_seed, lane, spacing, t.max_ped_slots, v);
}

PhantomState phantom_vehicle(uint64_t map_seed, const Lane& lane,
                             const LaneSchedule& sched, uint32_t slot,
                             int64_t step, const AmbientTuning& t) {
    PhantomState p;
    if (sched.slots == 0 || slot >= sched.slots) return p;

    p.speed_mps = slot_speed(map_seed, lane, slot, sched.speed_mps,
                             kChannelPhantomSlotSpeed, t.per_slot_speed);

    // THE CLOSED FORM. One integer wrap and one multiply — no accumulation, so
    // evaluating it at step 10 and at step 10,000,000 costs the same and
    // neither answer depends on the other having been asked for.
    const int64_t phase = wrap(step - sched.depart_step -
                                   static_cast<int64_t>(slot) * sched.headway_steps,
                               sched.period_steps);
    p.dist_along_m =
        std::min(static_cast<float>(phase) * p.speed_mps * kSimDtF, sched.length_m);
    p.lateral_m = 0.0f;
    return p;
}

int64_t phantom_lap(const LaneSchedule& sched, uint32_t slot, int64_t step) {
    if (sched.period_steps <= 0) return 0;
    const int64_t v = step - sched.depart_step -
                      static_cast<int64_t>(slot) * sched.headway_steps;
    // Floor division, to match wrap()'s non-negative remainder. Truncation
    // would make the lap jump back to 0 across the schedule epoch and hand two
    // different departures the same identity.
    int64_t q = v / sched.period_steps;
    if (v % sched.period_steps < 0) --q;
    return q;
}

PhantomState phantom_ped(uint64_t map_seed, const Lane& lane,
                         const LaneSchedule& sched, uint32_t slot,
                         int64_t step, const AmbientTuning& t) {
    PhantomState p;
    if (sched.slots == 0 || slot >= sched.slots) return p;

    p.speed_mps = slot_speed(map_seed, lane, slot, sched.speed_mps,
                             kChannelPhantomPedSpeed, t.per_slot_speed);

    const int64_t phase = wrap(step - sched.depart_step -
                                   static_cast<int64_t>(slot) * sched.headway_steps,
                               sched.period_steps);
    p.dist_along_m =
        std::min(static_cast<float>(phase) * p.speed_mps * kSimDtF, sched.length_m);

    // Even slots take the right-hand footway, odd slots the left. Which side is
    // decided by the ORDINAL and not by a roll, so the two footways carry equal
    // traffic without a second hash and without a count that drifts.
    const float side = (slot & 1u) ? -1.0f : 1.0f;
    p.lateral_m = side * (lane.width_m * 0.5f + t.sidewalk_offset_m);
    return p;
}

ParkedLaneBay parked_lane_bay(const Lane& lane, const AmbientTuning& t) {
    ParkedLaneBay bay;
    // Kerbs first: a class with no sidewalk has no kerb to park against, which
    // takes out freeways, alleys and dirt roads without naming any of them.
    if (!road_class_def(lane.cls).sidewalks) return bay;
    // Then the OUTERMOST lane only. Lane 0 is nearest the centreline, so an
    // inner lane on a wide road would otherwise also see room at the kerb and
    // the road would come out double-parked.
    const uint32_t outermost = static_cast<uint32_t>(
        std::max<uint8_t>(1, std::max(lane.lanes_at_start, lane.lanes_at_end)));
    if (static_cast<uint32_t>(lane.index) + 1u != outermost) return bay;

    const float half = lane.width_m * 0.5f;
    const float centre_from_road =
        half - t.parked_kerb_gap_m - t.parked_half_width_m;
    // In the lane's own frame. lateral_offset_m is how far right of the road
    // centreline this lane sits, so subtracting it converts one to the other —
    // the identical conversion the footway offset makes.
    const float lateral = centre_from_road - lane.lateral_offset_m;
    // The gate: room between the traffic and the parked bodies.
    //
    // MEASURED AGAINST THE WIDEST BODY THAT CAN ACTUALLY PARK HERE, not
    // against the nominal slot width above. Those are two different jobs and
    // one constant used to do both: `parked_half_width_m` places the body, and
    // it is deliberately a nominal 0.95 m so a sedan sits 0.30 m off the kerb
    // and a truck 0.10 m — both on the carriageway, neither floating. But the
    // GATE is asking whether the road has room for the car that will really be
    // put there, and parked_vehicle_kind() can put a 1.15 m box truck in any
    // slot. Asking it about the nominal body let a bay pass a 1.00 m clearance
    // gate and then stand a body 1.10 m from the lane centre — which the
    // runtime hazard test, correctly using real footprints, then called an
    // obstruction. The gate and the hazard test now measure the same car.
    if (lateral - widest_parked_half_width_m() < t.parked_lane_clearance_m)
        return bay;

    const float usable =
        lane.length_m - 2.0f * t.parked_junction_setback_m;
    if (!(usable > 0.0f)) return bay;

    // The road has room. Everything from here decides how much of it the
    // DISTRICT fills, and `slots == 0` past this point means an empty kerb by
    // authorial choice rather than a road that could never have one.
    bay.lateral_m = lateral;
    bay.usable_m = usable;

    const float density = std::max(lane.parked_density, 0.0f);
    if (!(density > 0.0f)) return bay;
    const float pitch = std::max(t.parked_spacing_m, 1.0f) / density;
    const int64_t count = static_cast<int64_t>(std::floor(usable / pitch));
    if (count <= 0) return bay;

    bay.slots = static_cast<uint32_t>(
        std::min<int64_t>(count, static_cast<int64_t>(t.max_parked_slots)));
    // Re-spread over the usable run after the cap, for the same reason
    // build_schedule() re-derives its headway: capping without re-spreading
    // bunches every capped lane against the near junction.
    bay.pitch_m = usable / static_cast<float>(bay.slots);
    bay.first_m = t.parked_junction_setback_m + bay.pitch_m * 0.5f;
    return bay;
}

ParkedSlot parked_slot(uint64_t map_seed, const Lane& lane,
                       const ParkedLaneBay& bay, uint32_t slot,
                       const AmbientTuning& t) {
    ParkedSlot p;
    if (bay.slots == 0 || slot >= bay.slots) return p;
    p.lateral_m = bay.lateral_m;

    // Jitter inside the slot's own share of the kerb, never across it, so two
    // parked cars can never be placed on top of each other however the rolls
    // fall. Half the pitch minus a body length is what is genuinely free.
    Rng along{phantom_key(map_seed, lane.key, slot, kChannelParkedAlong)};
    const float free = std::max(0.0f, bay.pitch_m * 0.5f - 2.6f);
    const float centre = bay.first_m + bay.pitch_m * static_cast<float>(slot);
    p.dist_along_m = std::clamp(centre + along.range(-free, free),
                                t.parked_junction_setback_m,
                                std::max(t.parked_junction_setback_m,
                                         lane.length_m -
                                             t.parked_junction_setback_m));

    Rng facing{phantom_key(map_seed, lane.key, slot, kChannelParkedFacing)};
    p.reversed = facing.next_float() < 0.34f;
    return p;
}

}  // namespace apricot
