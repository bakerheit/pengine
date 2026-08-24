#pragma once

#include <cstdint>

#include "core/rng.h"

namespace apricot {

// HOW PINATTY DRAWS ITS RANDOMNESS, and why it does not look like the code it
// came from.
//
// probablecause seeded its city from sequential streams: std::mt19937 for
// driver profiles and lane picks, a hand-rolled xorshift32 for weather and
// lightning. Those are correct only while every pull happens in the same order
// on every machine and every run — and a streamed open world is exactly the
// place that stops being true. A car spawned two frames earlier because a
// chunk arrived two frames earlier pulls a different profile, and from there
// the two runs are different worlds. The bug does not present as "the RNG
// changed"; it presents as "replays desync after a minute", several hundred
// draws downstream of the pull that actually diverged.
//
// So everything spatial or per-entity here is keyed: the entropy for a thing
// is a pure function of the run seed, the thing's identity, and a CHANNEL that
// keeps two unrelated decisions about the same thing uncorrelated. Draw order
// stops mattering because there is no order.
//
// The channels are listed together, in one place, for one reason: two
// decisions that share a channel share their entropy, and correlated "random"
// choices are the kind of bug that reads as "why do all the impatient drivers
// spawn on the same street". Add a channel rather than reusing one.
//
// terrain/noise.h owns 0x0100..0x0FFF. The city takes 0x1000 up.

inline constexpr uint32_t kChannelDriverProfile = 0x1000u;
inline constexpr uint32_t kChannelPedNerve      = 0x1100u;
inline constexpr uint32_t kChannelPedPreferred  = 0x1200u;
inline constexpr uint32_t kChannelPedSpace      = 0x1300u;
inline constexpr uint32_t kChannelWeather       = 0x1400u;
inline constexpr uint32_t kChannelLightning     = 0x1500u;

// THE ONE PLACE A STREAM IS STILL RIGHT, and the carve-out it lives under.
//
// core/rng.h permits Rng for "genuinely order-local work". The weather and
// lightning schedulers are that: one instance, one consumer, advanced by one
// update() per sim step. There is no approach order to lose, so a stream is not
// only safe, it is the honest shape — "what is the weather doing next" genuinely
// depends on what it did last.
//
// What they may NOT do is carry their own generator. probablecause gave each of
// them a hand-rolled xorshift32, which meant two more algorithms in a library
// whose entire determinism story rests on there being one. Both now take an
// apricot Rng, seeded through hash_coord() off the run seed, on separate
// channels so a change to one does not reshuffle the other.
constexpr Rng city_system_rng(uint64_t seed, uint32_t channel) {
    return rng_at(seed, 0, 0, channel);
}

// Entropy for one entity that lives at a world cell — a car about to be given
// a driver, a pedestrian about to be given a nerve. `cell_x` / `cell_z` are the
// spawn cell, and `slot` disambiguates several entities in the same cell (an
// index within the spawn batch, a lane ordinal — anything the caller can
// reproduce). Two entities in the same cell with the same slot ARE the same
// entity as far as this is concerned, which is the caller's problem to avoid.
constexpr uint64_t city_entity_key(uint64_t seed, int32_t cell_x, int32_t cell_z,
                                   uint32_t slot, uint32_t channel) {
    // The slot is folded into the channel rather than into a coordinate: the
    // coordinates carry spatial structure that hash_coord's two odd multipliers
    // are chosen to separate, and burying an arbitrary index in them throws
    // that away.
    return hash_coord3(seed, cell_x, cell_z,
                       channel ^ (slot * 0x9E3779B9u));
}

// The standard stream for one entity's decisions: keyed, so it does not care
// when it was made, and short-lived, so it never becomes shared state.
constexpr Rng city_entity_rng(uint64_t seed, int32_t cell_x, int32_t cell_z,
                              uint32_t slot, uint32_t channel) {
    return Rng{city_entity_key(seed, cell_x, cell_z, slot, channel)};
}

// A [0, 1) roll for one keyed decision, with no stream left over. Use this
// where a caller wants exactly one number and holding a stream would invite
// somebody to pull a second one from it later — which is how a keyed draw
// quietly turns back into a sequential one.
inline float city_unit_roll(uint64_t seed, int32_t cell_x, int32_t cell_z,
                            uint32_t slot, uint32_t channel) {
    Rng r{city_entity_key(seed, cell_x, cell_z, slot, channel)};
    return r.next_float();
}

}  // namespace apricot
