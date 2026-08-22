// hash_coord() determinism.
//
// Everything procedural in apricot is a pure function of (seed, coordinate).
// If this suite fails, the world is no longer reproducible: replays desync,
// a save file no longer names the world it was recorded in, and "it generated
// differently on my machine" becomes a legitimate bug report. Treat a failure
// here as a stop-the-line event, not as a test to update.

#include <cstdint>
#include <set>
#include <vector>

#include "core/rng.h"
#include "test_assert.h"

using namespace apricot;

namespace {

// --- pinned values -----------------------------------------------------------
// GOLDEN VALUES. These are the actual outputs of the current algorithm, pinned
// so that ANY change to hash_coord's mixing is caught here rather than
// discovered as a silently different world three tickets later.
//
// If you change the algorithm deliberately, these must be regenerated AND
// every existing replay tape and save seed is invalidated. That is the cost;
// this test exists to make sure you pay it on purpose.
void golden_values() {
    REQUIRE(hash_coord(0, 0, 0) == 0x48218226FF3CD4BFull);
    REQUIRE(hash_coord(1, 2, 3) == 0xA67126CFDACE5C8Cull);
    REQUIRE(hash_coord(1, 3, 2) == 0x6572FF0885368B3Full);
    REQUIRE(hash_coord(0xDEADBEEFull, -7, 42) == 0xAA0504533E345F6Aull);
    apricot_test::pass("golden values unchanged");
}

// The regression pin for a real bug: splitmix64's finaliser is a bijection
// with zero as a FIXED POINT, so an unoffset hash_coord(0, 0, 0) returns 0.
// Seed 0 at the world origin is the single most likely combination in any
// test, any default-constructed state and any fresh run, and a zero there
// makes the terrain lattice degenerate exactly where everybody starts.
void origin_is_not_degenerate() {
    REQUIRE(hash_coord(0, 0, 0) != 0ull);
    REQUIRE(hash_coord(0, 0, 0) != hash_coord(0, 1, 0));

    // Nothing else on the axes may collapse either.
    for (int32_t i = -4; i <= 4; ++i) {
        REQUIRE_MSG(hash_coord(0, i, 0) != 0ull, "axis x hashed to zero", "x");
        REQUIRE_MSG(hash_coord(0, 0, i) != 0ull, "axis z hashed to zero", "z");
    }
    apricot_test::pass("origin and axes are non-degenerate");
}

// --- purity ------------------------------------------------------------------
// The same question always gets the same answer, no matter how many times or
// in what order it is asked. This is the property streaming depends on: a
// chunk approached from the north must generate identically to one approached
// from the south.
void repeatable_and_order_independent() {
    constexpr uint64_t kSeed = 0x1234'5678'9ABC'DEF0ull;

    for (int32_t z = -3; z <= 3; ++z) {
        for (int32_t x = -3; x <= 3; ++x) {
            const uint64_t a = hash_coord(kSeed, x, z);
            const uint64_t b = hash_coord(kSeed, x, z);
            REQUIRE_MSG(a == b, "repeated call differed", "immediate repeat");
        }
    }

    // Walk the same grid in the opposite order and compare against a forward
    // walk. Any hidden state would show up as a mismatch here.
    std::vector<uint64_t> forward;
    for (int32_t z = -3; z <= 3; ++z)
        for (int32_t x = -3; x <= 3; ++x) forward.push_back(hash_coord(kSeed, x, z));

    std::size_t i = forward.size();
    for (int32_t z = 3; z >= -3; --z) {
        for (int32_t x = 3; x >= -3; --x) {
            --i;
            REQUIRE_MSG(hash_coord(kSeed, x, z) == forward[i],
                        "reverse traversal differed", "order independence");
        }
    }
    apricot_test::pass("repeatable and order-independent");
}

// --- separation --------------------------------------------------------------
void distinct_inputs_give_distinct_values() {
    constexpr uint64_t kSeed = 42;

    // Transposing the coordinates must not give the same value, or the world
    // is mirrored about its diagonal — subtle in a screenshot, obvious once
    // you drive it.
    REQUIRE(hash_coord(kSeed, 2, 7) != hash_coord(kSeed, 7, 2));

    // Sign must matter. Casting through uint32 keeps negatives well defined;
    // sign-extension bugs make x and -x hash near-identically.
    REQUIRE(hash_coord(kSeed, -3, 5) != hash_coord(kSeed, 3, 5));
    REQUIRE(hash_coord(kSeed, 5, -3) != hash_coord(kSeed, 5, 3));

    // Changing the seed must change the world everywhere, not just somewhere.
    for (int32_t z = -2; z <= 2; ++z) {
        for (int32_t x = -2; x <= 2; ++x) {
            REQUIRE_MSG(hash_coord(1, x, z) != hash_coord(2, x, z),
                        "two seeds agreed at a coordinate", "seed separation");
        }
    }

    // No collisions across a decent patch. 81x81 distinct coordinates into 64
    // bits should be collision-free by an enormous margin; any collision at
    // this scale means the mixing is broken, not unlucky.
    std::set<uint64_t> seen;
    for (int32_t z = -40; z <= 40; ++z)
        for (int32_t x = -40; x <= 40; ++x) seen.insert(hash_coord(kSeed, x, z));
    REQUIRE(seen.size() == 81u * 81u);

    apricot_test::pass("distinct inputs stay distinct");
}

// Channels must decorrelate, or height and moisture are the same field.
void channels_are_independent() {
    constexpr uint64_t kSeed = 7;
    REQUIRE(hash_coord3(kSeed, 4, 4, 0) != hash_coord3(kSeed, 4, 4, 1));
    REQUIRE(hash_coord3(kSeed, 4, 4, 1) != hash_coord3(kSeed, 4, 4, 2));

    std::set<uint64_t> seen;
    for (uint32_t c = 0; c < 64; ++c) seen.insert(hash_coord3(kSeed, 4, 4, c));
    REQUIRE(seen.size() == 64u);
    apricot_test::pass("channels are independent");
}

// --- streams -----------------------------------------------------------------
void streams_are_deterministic_and_isolated() {
    Rng a = rng_at(99, 5, -5);
    Rng b = rng_at(99, 5, -5);

    // Two streams from the same key must agree step for step.
    for (int i = 0; i < 64; ++i) {
        REQUIRE_MSG(a.next_u64() == b.next_u64(), "same-key streams diverged",
                    "stream reproducibility");
    }

    // Pinned so a change to the stream algorithm is caught too.
    Rng g = rng_at(99, 5, -5);
    REQUIRE(g.next_u64() == 0x5D9EA6ABEE955721ull);
    REQUIRE(g.next_u64() == 0x08138FECF3C53E16ull);
    REQUIRE(g.next_u64() == 0xF0EE1BE0C739E7B2ull);

    // Pulling on one stream must not perturb another: no shared global state.
    Rng x = rng_at(5, 1, 1);
    Rng y = rng_at(5, 1, 1);
    for (int i = 0; i < 10; ++i) (void)x.next_u64();
    Rng fresh = rng_at(5, 1, 1);
    REQUIRE(y.next_u64() == fresh.next_u64());

    apricot_test::pass("streams reproducible and isolated");
}

void float_helpers_stay_in_range() {
    Rng r = rng_at(0xABCDEFull, 3, 9);
    for (int i = 0; i < 20000; ++i) {
        const float f = r.next_float();
        REQUIRE_MSG(f >= 0.0f && f < 1.0f, "next_float left [0,1)", "range");
    }

    Rng q = rng_at(0xABCDEFull, 3, 9);
    for (int i = 0; i < 5000; ++i) {
        const float v = q.range(-3.5f, 7.25f);
        REQUIRE_MSG(v >= -3.5f && v < 7.25f, "range() left its bounds", "range");
    }

    // Inclusive both ends, and every value in a small span must be reachable.
    Rng n = rng_at(1, 1, 1);
    std::set<int32_t> hit;
    for (int i = 0; i < 5000; ++i) {
        const int32_t v = n.next_int(-2, 3);
        REQUIRE_MSG(v >= -2 && v <= 3, "next_int left its bounds", "next_int");
        hit.insert(v);
    }
    REQUIRE(hit.size() == 6u);

    // Degenerate span must not divide by zero or spin.
    Rng d = rng_at(2, 2, 2);
    REQUIRE(d.next_int(4, 4) == 4);
    REQUIRE(d.next_int(9, 1) == 9);

    apricot_test::pass("float and int helpers respect their ranges");
}

// hash_coord must be usable at compile time; several generators want to pin
// constants without paying for them at runtime.
void usable_at_compile_time() {
    constexpr uint64_t k = hash_coord(1, 2, 3);
    static_assert(k == 0xA67126CFDACE5C8Cull,
                  "constexpr hash_coord disagrees with the runtime value");
    static_assert(hash_coord(0, 0, 0) != 0ull, "origin is degenerate");
    REQUIRE(k == hash_coord(1, 2, 3));
    apricot_test::pass("constexpr evaluation matches runtime");
}

}  // namespace

int main() {
    std::printf("rng_determinism_tests\n");
    golden_values();
    origin_is_not_degenerate();
    repeatable_and_order_independent();
    distinct_inputs_give_distinct_values();
    channels_are_independent();
    streams_are_deterministic_and_isolated();
    float_helpers_stay_in_range();
    usable_at_compile_time();
    return apricot_test::done("rng_determinism_tests");
}
