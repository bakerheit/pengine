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

LaneSchedule build_schedule(uint64_t map_seed, const Lane& lane, float density,
                            float spacing_m, uint32_t max_slots, float speed) {
    LaneSchedule s;
    s.length_m = lane.length_m;
    s.speed_mps = speed;

    const float d = std::max(density, 0.0f);
    if (!(d > 0.0f) || !(speed > 0.0f) || !(lane.length_m > 0.0f)) return s;

    // Denser lane, shorter gap. The authored scalar divides the nominal gap
    // rather than multiplying a count, so doubling the density halves the
    // headway rather than doing something that depends on the lane's length.
    const float gap_m = std::max(spacing_m / d, 1.0f);

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

LaneSchedule vehicle_schedule(uint64_t map_seed, const Lane& lane,
                              const AmbientTuning& t) {
    Rng r{phantom_key(map_seed, lane.key, 0u, kChannelPhantomLaneSpeed)};
    const float v = lane.speed_limit_mps * r.range(t.speed_lo, t.speed_hi);
    return build_schedule(map_seed, lane, lane.traffic_density,
                          t.vehicle_spacing_m, t.max_vehicle_slots, v);
}

LaneSchedule ped_schedule(uint64_t map_seed, const Lane& lane,
                          const AmbientTuning& t) {
    Rng r{phantom_key(map_seed, lane.key, 0u, kChannelPhantomPedSpeed)};
    const float v = r.range(t.ped_speed_lo, t.ped_speed_hi);
    // Two footways, so the same along-lane spacing yields twice the slots.
    return build_schedule(map_seed, lane, lane.ped_density * 2.0f,
                          t.ped_spacing_m, t.max_ped_slots, v);
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

}  // namespace apricot
