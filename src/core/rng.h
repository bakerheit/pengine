#pragma once

#include <glm/glm.hpp>

#include <cmath>
#include <cstdint>

namespace apricot {

// Turns per revolution, as a float, once. Spelled out rather than pulled from
// glm so the distribution helpers below produce bit-identical results on every
// platform regardless of how a maths library rounds its own constant.
inline constexpr float kTwoPi = 6.28318530717958647692f;

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

// AUDIT NOTE — the zero fixed point was MOVED, not removed.
//
// splitmix64_mix(0) == 0, and hash_coord() adds a gamma offset to keep the
// common (seed=0, x=0, z=0) input off it. That fixes the case everybody
// actually hits, but the fixed point is still reachable: solving
// `seed + gamma == 0` gives exactly one seed for which the origin collapses.
//
//     hash_coord(0x61C8864680B583EB, 0, 0) == 0        (measured, pinned in
//     hash_coord3(0x61C8864680B583EB, 0, 0, 0) == 0     the test suite)
//
// It is left alone ON PURPOSE. Removing it means changing the mixing, and
// changing the mixing regenerates every golden value, every world seed and
// every replay tape — a real cost, paid to defend against one 64-bit seed out
// of 2^64 producing one degenerate lattice value at one coordinate. That trade
// is not worth making. It is pinned in tests/rng_determinism_tests.cpp so it
// is a known property rather than a surprise at 2am.
//
// The streams are NOT affected: splitmix64_next() adds gamma BEFORE mixing, so
// a zero key still produces a perfectly good sequence. Verified, also pinned.

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

    // Uniform in (0, 1]. NEVER ZERO, which is the entire point: next_float()
    // legitimately returns exactly 0.0f about once in 16.7 million draws, and
    // any consumer that takes a log of it, divides by it, or feeds it to a
    // reciprocal square root gets an infinity roughly once per twenty minutes
    // of play. That bug is unreproducible by construction. Use this one
    // wherever zero is not a legal input; the +1 makes it structurally
    // impossible rather than merely unlikely.
    constexpr float open_unit_float() {
        return static_cast<float>((next_u64() >> 40) + 1ull) *
               (1.0f / 16777216.0f);
    }

    // Uniform in [-1, 1). The signed unit interval, for jitter and offsets
    // that should be as likely to go one way as the other. Exact at every
    // step: 24 bits scaled by 2^-23 and shifted by one, with no rounding
    // anywhere, so -1 is reachable and +1 is not.
    constexpr float unit_float() {
        return static_cast<float>(next_u64() >> 40) * (2.0f / 16777216.0f) -
               1.0f;
    }

    // Uniform in [lo, hi).
    //
    // Computes the span as (hi - lo), so a span wider than FLT_MAX overflows to
    // infinity and the result is NaN. Every real caller asks for metres or
    // radians and is nowhere near that; if you genuinely need the full float
    // range, do the arithmetic in double.
    constexpr float range(float lo, float hi) {
        return lo + (hi - lo) * next_float();
    }

    // Uniform in [lo, hi] INCLUSIVE. Slightly biased for spans that are not a
    // power of two; that bias is ~2^-64 and irrelevant for scatter, but do not
    // reach for this to build a shuffle you intend to prove uniform.
    constexpr int32_t next_int(int32_t lo, int32_t hi) {
        // ONE draw, ALWAYS, including on a degenerate span.
        //
        // This used to early-return `lo` for hi <= lo without pulling on the
        // stream, and that was a determinism hazard rather than an
        // optimisation: it made how far the stream advances depend on the
        // VALUES passed in. Two chunks running the same code with slightly
        // different data would then diverge in stream position and generate
        // completely different worlds from there on — a desync whose cause is
        // several hundred draws upstream of where anyone notices it. Burning
        // one draw is free; a data-dependent stream position is not.
        const uint64_t bits = next_u64();
        if (hi <= lo) return lo;

        // Widened to int64 before subtracting: `hi - lo` on int32 overflows for
        // a full-range span, and the wrapped value makes the modulo produce
        // out-of-range results rather than anything obviously wrong.
        const uint64_t span =
            static_cast<uint64_t>(static_cast<int64_t>(hi) -
                                  static_cast<int64_t>(lo)) + 1ull;
        const int64_t v = static_cast<int64_t>(lo) +
                          static_cast<int64_t>(bits % span);
        return static_cast<int32_t>(v);
    }

    // next_float() is in [0,1) and probability >= 1 therefore always fires,
    // probability <= 0 never does. A NaN probability answers false, because
    // every comparison against NaN is false — which is the right answer for
    // "should this unspecified thing happen".
    constexpr bool chance(float probability) {
        return next_float() < probability;
    }

    // --- distributions -------------------------------------------------------
    //
    // Every one of these is CLOSED FORM and consumes a FIXED number of draws.
    // None of them is a rejection loop, and that is deliberate: a loop that
    // retries until a sample lands inside the shape consumes a number of draws
    // that depends on the values it drew, which is precisely the
    // data-dependent stream position that next_int() above exists to avoid. It
    // also has no worst case, which is a poor property for something on a
    // frame budget.

    // Normally distributed, via Box-Muller. Consumes exactly two draws and
    // discards the second of the pair rather than caching it — caching would
    // make the result depend on how many times the function had been called
    // before, and a stream you cannot reason about is not deterministic in any
    // useful sense.
    //
    // u1 comes from open_unit_float() precisely because std::log(0) is -inf,
    // which would come back as a NaN sample about once in 16.7 million calls.
    float normal(float mean = 0.0f, float stddev = 1.0f) {
        const float u1 = open_unit_float();  // (0, 1] -- log is always finite
        const float u2 = next_float();       // [0, 1)
        const float r = std::sqrt(-2.0f * std::log(u1));
        return mean + stddev * (r * std::cos(kTwoPi * u2));
    }

    // A point on the unit circle. One draw.
    glm::vec2 on_unit_circle() {
        const float a = kTwoPi * next_float();
        return glm::vec2{std::cos(a), std::sin(a)};
    }

    // Uniform over the AREA of the unit disc. Two draws.
    //
    // The radius is sqrt(u), not u. Using u directly is the classic mistake:
    // it is uniform in radius, which piles half the samples into the inner
    // quarter of the area and reads on screen as scatter clumping around every
    // spawn point.
    glm::vec2 in_unit_disc() {
        const float r = std::sqrt(next_float());
        return on_unit_circle() * r;
    }

    // Uniform over the SURFACE of the unit sphere. Two draws.
    //
    // Archimedes' hat-box: z uniform in [-1, 1) and the angle uniform gives an
    // exactly area-uniform distribution. Picking a latitude uniformly instead
    // would bunch the samples at the poles.
    glm::vec3 on_unit_sphere() {
        const float z = unit_float();                    // [-1, 1)
        const float r = std::sqrt(std::fmax(0.0f, 1.0f - z * z));
        const float a = kTwoPi * next_float();
        return glm::vec3{r * std::cos(a), r * std::sin(a), z};
    }

    // Uniform over the VOLUME of the unit sphere. Three draws.
    // The cube root is the three-dimensional counterpart of the disc's square
    // root, and skipping it clumps just as visibly.
    glm::vec3 in_unit_sphere() {
        const float r = std::cbrt(next_float());
        return on_unit_sphere() * r;
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
