#pragma once

#include <cstdint>

namespace apricot {

// Deterministic randomness. The whole world is a pure function of one 64-bit
// run seed, so this header has hard rules attached to it:
//
//   * NO std::rand(), NO std::random_device, NO global generator state.
//   * NO time-seeding ANYWHERE in the engine. A world that differs between two
//     runs of the same seed cannot be replayed, cannot be reported as a bug,
//     and cannot be raced against a ghost.
//   * Terrain, props and scatter derive from hash_coord(), never from a
//     sequential stream. A streamed chunk must generate identically whether
//     the player arrived from the north or the south, and a stream's value
//     depends on how many times it has been pulled — which is exactly the
//     approach order you cannot control.
//
// Use Rng (a stream) only for genuinely order-local work: shuffling a list you
// already hold, jittering within a single chunk you have already keyed.

// splitmix64's finaliser. A bijection on uint64_t with excellent avalanche —
// one flipped input bit changes about half the output bits.
constexpr uint64_t splitmix64_mix(uint64_t z) {
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Advance a splitmix64 state and return the next value.
constexpr uint64_t splitmix64_next(uint64_t& state) {
    state += 0x9E3779B97F4A7C15ull;  // 2^64 / golden ratio
    return splitmix64_mix(state);
}

// PURE spatial hash: the same (seed, x, z) yields the same value on every
// platform, every run, forever. This is the entry point for all procedural
// generation — chunk heights, prop scatter, surface variation.
//
// Signed coords are cast through uint32_t so negative coordinates wrap
// well-definedly rather than sign-extending into a near-identical hash for
// x and -x. Constexpr so callers can pin known values at compile time.
constexpr uint64_t hash_coord(uint64_t seed, int32_t x, int32_t z) {
    const uint64_t ux = static_cast<uint64_t>(static_cast<uint32_t>(x));
    const uint64_t uz = static_cast<uint64_t>(static_cast<uint32_t>(z));

    // The gamma offset is NOT decoration. splitmix64_mix() is a bijection with
    // ZERO AS A FIXED POINT: mix(0) == 0. Without this offset,
    // hash_coord(0, 0, 0) returns 0, so a default seed at the world origin
    // produces a degenerate lattice value and terrain that is provably wrong
    // in exactly the spot every test and every fresh run starts at. Adding the
    // constant moves the input off the fixed point.
    uint64_t h = seed + 0x9E3779B97F4A7C15ull;

    // Two distinct odd multipliers, so hash_coord(s, a, b) and
    // hash_coord(s, b, a) do not collide — a diagonally symmetric world is a
    // very visible bug.
    h = splitmix64_mix(h ^ (ux * 0xD6E8FEB86659FD93ull));
    h = splitmix64_mix(h ^ (uz * 0xA0761D6478BD642Full));
    return h;
}

// Three-axis variant, for when one coordinate pair needs several independent
// values (height vs. moisture vs. scatter at the same spot). `channel` keeps
// them uncorrelated without burning a second seed.
constexpr uint64_t hash_coord3(uint64_t seed, int32_t x, int32_t z,
                               uint32_t channel) {
    return splitmix64_mix(hash_coord(seed, x, z) ^
                          (static_cast<uint64_t>(channel) * 0x9E3779B97F4A7C15ull));
}

// A small deterministic stream. Cheap to construct — make one per chunk, per
// prop, per whatever, seeded from hash_coord(), and let it die with the scope.
// Copyable on purpose: copying it forks the stream reproducibly.
struct Rng {
    uint64_t state = 0u;

    constexpr uint64_t next_u64() { return splitmix64_next(state); }

    // Uniform in [0, 1). Takes the top 24 bits so every result is exactly
    // representable as a float — the low bits of a naive
    // `next_u64() / 2^64` conversion are rounding noise, not entropy.
    constexpr float next_float() {
        return static_cast<float>(next_u64() >> 40) * (1.0f / 16777216.0f);
    }

    // Uniform in [lo, hi).
    constexpr float range(float lo, float hi) {
        return lo + (hi - lo) * next_float();
    }

    // Uniform in [lo, hi] INCLUSIVE. Slightly biased for spans that are not a
    // power of two; that bias is ~2^-64 and irrelevant for scatter, but do not
    // reach for this to build a shuffle you intend to prove uniform.
    constexpr int32_t next_int(int32_t lo, int32_t hi) {
        if (hi <= lo) return lo;
        // Widened to int64 before subtracting: `hi - lo` on int32 overflows for
        // a full-range span, and the wrapped value makes the modulo produce
        // out-of-range results rather than anything obviously wrong.
        const uint64_t span =
            static_cast<uint64_t>(static_cast<int64_t>(hi) -
                                  static_cast<int64_t>(lo)) + 1ull;
        const int64_t v = static_cast<int64_t>(lo) +
                          static_cast<int64_t>(next_u64() % span);
        return static_cast<int32_t>(v);
    }

    constexpr bool chance(float probability) {
        return next_float() < probability;
    }
};

// The standard way to get a stream: key it to a world coordinate so it stays
// order-independent.
constexpr Rng rng_at(uint64_t seed, int32_t x, int32_t z) {
    return Rng{hash_coord(seed, x, z)};
}

constexpr Rng rng_at(uint64_t seed, int32_t x, int32_t z, uint32_t channel) {
    return Rng{hash_coord3(seed, x, z, channel)};
}

}  // namespace apricot
