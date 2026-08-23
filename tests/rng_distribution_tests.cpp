// RNG distributions, degenerate cases, and reproducibility across a FRESH
// PROCESS.
//
// tests/rng_determinism_tests.cpp pins the hash. This one pins the shapes the
// hash gets poured into, and the properties that make them safe to build a
// world out of:
//
//   * every distribution is a pure function of the stream state;
//   * every one of them consumes a FIXED number of draws, so stream position
//     never depends on the values drawn;
//   * the shapes are actually the shapes they claim to be — a disc sampler
//     that clumps at the centre still passes "is inside the disc";
//   * the residual degenerate cases are named and pinned rather than
//     discovered later.
//
// The fresh-process case re-executes this binary and compares its output byte
// for byte. An in-process check cannot see a value that depends on address
// layout, on static initialisation order, or on anything else that differs
// between two runs — which is exactly the class of bug that turns into "it
// generated differently on my machine".

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

#include "core/rng.h"
#include "test_assert.h"

using namespace apricot;

namespace {

// --- degenerate cases --------------------------------------------------------

// The zero fixed point was MOVED, not removed. splitmix64_mix(0) == 0, and
// hash_coord's gamma offset keeps the common (0, 0, 0) input off it — but
// solving seed + gamma == 0 gives exactly one seed that lands back on it.
//
// This is pinned rather than fixed. Fixing it means changing the mixing, and
// changing the mixing regenerates every golden value, every world seed and
// every replay tape, to defend against one seed in 2^64 producing one
// degenerate lattice value at one coordinate. If this test ever fails, either
// the hash changed (regenerate the goldens and accept the cost) or somebody
// has already paid it.
void the_one_remaining_zero_is_known_and_pinned() {
    constexpr uint64_t kZeroSeed = 0x61C8864680B583EBull;  // == -gamma

    REQUIRE_MSG(hash_coord(kZeroSeed, 0, 0) == 0ull,
                "the documented zero fixed point moved; core/rng.h is stale",
                "known degenerate seed");
    REQUIRE_MSG(hash_coord3(kZeroSeed, 0, 0, 0) == 0ull,
                "hash_coord3 no longer inherits the documented zero",
                "known degenerate seed");

    // It is exactly ONE point, not a region. The neighbours must be healthy.
    REQUIRE(hash_coord(kZeroSeed, 1, 0) != 0ull);
    REQUIRE(hash_coord(kZeroSeed, 0, 1) != 0ull);
    REQUIRE(hash_coord(kZeroSeed, -1, 0) != 0ull);
    REQUIRE(hash_coord3(kZeroSeed, 0, 0, 1) != 0ull);

    // And the STREAMS are immune, because splitmix64_next adds gamma before
    // mixing. A zero key is a perfectly good stream key, which is why this
    // stayed a documented curiosity instead of a stop-the-line bug.
    Rng from_zero{0};
    const uint64_t a = from_zero.next_u64();
    const uint64_t b = from_zero.next_u64();
    REQUIRE_MSG(a != 0ull, "a zero-keyed stream produced zero", "zero key");
    REQUIRE_MSG(a != b, "a zero-keyed stream is stuck", "zero key");

    Rng at_zero_coord = rng_at(kZeroSeed, 0, 0);
    REQUIRE(at_zero_coord.next_u64() != 0ull);

    apricot_test::pass("the one remaining zero is named, pinned and harmless");
}

// next_int used to skip its draw on a degenerate span, which made how far the
// stream advances depend on the VALUES passed in. Two chunks running the same
// code with slightly different data would diverge in stream position and
// generate different worlds from there on — a desync whose cause sits hundreds
// of draws upstream of where anyone notices it.
void every_helper_consumes_a_fixed_number_of_draws() {
    constexpr uint64_t kKey = 0xC0FFEE1234ull;

    struct Case {
        const char* name;
        int draws;
        void (*pull)(Rng&);
    };

    const Case cases[] = {
        {"next_u64", 1, [](Rng& r) { (void)r.next_u64(); }},
        {"next_float", 1, [](Rng& r) { (void)r.next_float(); }},
        {"unit_float", 1, [](Rng& r) { (void)r.unit_float(); }},
        {"open_unit_float", 1, [](Rng& r) { (void)r.open_unit_float(); }},
        {"range", 1, [](Rng& r) { (void)r.range(-2.0f, 5.0f); }},
        {"chance", 1, [](Rng& r) { (void)r.chance(0.5f); }},
        {"next_int", 1, [](Rng& r) { (void)r.next_int(0, 9); }},
        // THE FIX. A collapsed span must cost the same as any other.
        {"next_int (degenerate span)", 1, [](Rng& r) { (void)r.next_int(4, 4); }},
        {"next_int (inverted span)", 1, [](Rng& r) { (void)r.next_int(9, 1); }},
        {"normal", 2, [](Rng& r) { (void)r.normal(); }},
        {"on_unit_circle", 1, [](Rng& r) { (void)r.on_unit_circle(); }},
        {"in_unit_disc", 2, [](Rng& r) { (void)r.in_unit_disc(); }},
        {"on_unit_sphere", 2, [](Rng& r) { (void)r.on_unit_sphere(); }},
        {"in_unit_sphere", 3, [](Rng& r) { (void)r.in_unit_sphere(); }},
    };

    for (const Case& c : cases) {
        Rng actual{kKey};
        Rng reference{kKey};
        c.pull(actual);
        for (int i = 0; i < c.draws; ++i) (void)reference.next_u64();
        REQUIRE_MSG(actual.state == reference.state,
                    "the helper did not advance the stream by its stated "
                    "number of draws",
                    c.name);
    }

    // The degenerate span still returns the documented VALUE; only the stream
    // accounting changed.
    Rng d{7};
    REQUIRE(d.next_int(4, 4) == 4);
    REQUIRE(d.next_int(9, 1) == 9);

    apricot_test::pass("every helper costs a fixed, data-independent number of draws");
}

// next_float() legitimately returns exactly 0.0f, and open_unit_float() exists
// so that log(), 1/x and friends cannot be handed it. Rather than argue from
// probability — one draw in 16.7 million, which no test run would reliably
// reach — this pins a WITNESS: a stream state whose first draw really is zero.
void open_unit_float_is_never_zero() {
    constexpr uint64_t kZeroDrawState = 0x000000000055BB00ull;

    Rng witness{kZeroDrawState};
    REQUIRE_MSG(witness.next_float() == 0.0f,
                "the pinned zero witness no longer draws zero; the stream "
                "algorithm changed",
                "witness");

    Rng same{kZeroDrawState};
    const float open = same.open_unit_float();
    REQUIRE_MSG(open > 0.0f, "open_unit_float returned zero on the one input "
                             "that makes next_float return zero",
                "witness");
    REQUIRE_MSG(open <= 1.0f, "open_unit_float left (0, 1]", "witness");

    // unit_float reaches exactly -1 on the same witness, and never reaches +1.
    Rng also{kZeroDrawState};
    REQUIRE_NEAR(also.unit_float(), -1.0, 0.0);

    // Bounds over a long run.
    Rng r = rng_at(0xBEEF, 3, -9);
    float max_unit = -2.0f;
    for (int i = 0; i < 200000; ++i) {
        const float u = r.unit_float();
        REQUIRE_MSG(u >= -1.0f && u < 1.0f, "unit_float left [-1, 1)", "bounds");
        if (u > max_unit) max_unit = u;

        const float o = r.open_unit_float();
        REQUIRE_MSG(o > 0.0f && o <= 1.0f, "open_unit_float left (0, 1]",
                    "bounds");
    }
    REQUIRE_MSG(max_unit > 0.99f, "unit_float never got near the top of its "
                                  "range; it is not covering it",
                "bounds");

    apricot_test::pass("open_unit_float is nonzero on the exact input that breaks next_float");
}

// --- shapes ------------------------------------------------------------------

void normal_is_actually_normal() {
    Rng r = rng_at(0x5EED, 11, 22);

    constexpr int kN = 200000;
    double sum = 0.0;
    double sum_sq = 0.0;
    int within_1 = 0;
    int within_2 = 0;

    for (int i = 0; i < kN; ++i) {
        const float v = r.normal(0.0f, 1.0f);
        REQUIRE_MSG(v == v, "normal() produced NaN", "finite");
        REQUIRE_MSG(v > -40.0f && v < 40.0f, "normal() produced an infinity",
                    "finite");
        const double d = static_cast<double>(v);
        sum += d;
        sum_sq += d * d;
        if (d > -1.0 && d < 1.0) ++within_1;
        if (d > -2.0 && d < 2.0) ++within_2;
    }

    const double n = static_cast<double>(kN);
    const double mean = sum / n;
    const double variance = sum_sq / n - mean * mean;

    REQUIRE_NEAR(mean, 0.0, 0.02);
    REQUIRE_NEAR(variance, 1.0, 0.02);

    // The 68-95 rule. A uniform distribution dressed up as a normal one passes
    // a mean check and fails this badly, which is the point of including it.
    REQUIRE_NEAR(static_cast<double>(within_1) / n, 0.6827, 0.01);
    REQUIRE_NEAR(static_cast<double>(within_2) / n, 0.9545, 0.01);

    // Mean and deviation are respected, not ignored.
    Rng s = rng_at(0x5EED, 11, 22);
    double shifted = 0.0;
    for (int i = 0; i < kN; ++i) shifted += static_cast<double>(s.normal(5.0f, 3.0f));
    REQUIRE_NEAR(shifted / n, 5.0, 0.06);

    apricot_test::pass("normal() has the right mean, variance and tails");
}

// A disc sampler that uses r = u instead of r = sqrt(u) still returns points
// inside the disc — it just piles half of them into the inner quarter of the
// area, which on screen is scatter clumping around every spawn point. The
// area-fraction check below is what separates the two: correct gives 0.25
// inside half-radius, the clumping version gives 0.50.
void disc_and_circle_are_area_uniform() {
    Rng r = rng_at(0xD15C, 5, 5);

    constexpr int kN = 200000;
    int inside_half = 0;
    double sum_x = 0.0;
    double sum_y = 0.0;

    for (int i = 0; i < kN; ++i) {
        const glm::vec2 p = r.in_unit_disc();
        const float len = glm::length(p);
        REQUIRE_MSG(len <= 1.0f + 1e-5f, "in_unit_disc left the unit disc",
                    "bounds");
        if (len <= 0.5f) ++inside_half;
        sum_x += static_cast<double>(p.x);
        sum_y += static_cast<double>(p.y);
    }

    const double n = static_cast<double>(kN);
    REQUIRE_MSG(std::fabs(static_cast<double>(inside_half) / n - 0.25) < 0.01,
                "the disc is not area-uniform (r = u instead of sqrt(u)?)",
                "area uniform");
    REQUIRE_NEAR(sum_x / n, 0.0, 0.01);
    REQUIRE_NEAR(sum_y / n, 0.0, 0.01);

    // The circle sampler stays exactly on the rim.
    Rng c = rng_at(0xC177, 1, 1);
    for (int i = 0; i < 20000; ++i) {
        const glm::vec2 p = c.on_unit_circle();
        REQUIRE_NEAR(glm::length(p), 1.0, 1e-5);
    }

    apricot_test::pass("the disc is area-uniform and the circle is on the rim");
}

void sphere_is_uniform_on_surface_and_in_volume() {
    Rng r = rng_at(0x50B3ull, 8, -8);

    constexpr int kN = 200000;
    int upper = 0;
    int outer_band = 0;
    double sum_z = 0.0;

    for (int i = 0; i < kN; ++i) {
        const glm::vec3 p = r.on_unit_sphere();
        REQUIRE_NEAR(glm::length(p), 1.0, 1e-4);
        if (p.z > 0.0f) ++upper;
        if (std::fabs(p.z) > 0.5f) ++outer_band;
        sum_z += static_cast<double>(p.z);
    }

    const double n = static_cast<double>(kN);
    REQUIRE_NEAR(static_cast<double>(upper) / n, 0.5, 0.01);
    REQUIRE_NEAR(sum_z / n, 0.0, 0.01);

    // Archimedes' hat-box: z is uniform on [-1, 1], so exactly half the
    // surface lies outside |z| = 0.5. Sampling a uniform LATITUDE instead —
    // the usual mistake — gives 2/3 here and visibly bunches at the poles.
    REQUIRE_MSG(std::fabs(static_cast<double>(outer_band) / n - 0.5) < 0.01,
                "the sphere bunches at the poles (uniform latitude?)",
                "hat-box");

    // Volume: the fraction within half the radius is 1/8, not 1/2.
    Rng v = rng_at(0x501D, 2, 2);
    int inside_half = 0;
    for (int i = 0; i < kN; ++i) {
        const glm::vec3 p = v.in_unit_sphere();
        REQUIRE_MSG(glm::length(p) <= 1.0f + 1e-5f,
                    "in_unit_sphere left the unit ball", "bounds");
        if (glm::length(p) <= 0.5f) ++inside_half;
    }
    REQUIRE_MSG(std::fabs(static_cast<double>(inside_half) / n - 0.125) < 0.01,
                "the ball is not volume-uniform (missing the cube root?)",
                "volume uniform");

    apricot_test::pass("the sphere is uniform on its surface and in its volume");
}

// --- reproducibility ---------------------------------------------------------

// Every value this suite cares about, as a text digest of exact BIT PATTERNS.
// Bit patterns rather than printed decimals: a %.9g round-trip can hide a
// one-ulp difference, and one ulp is all it takes to desync a replay.
std::string digest() {
    std::string out;
    char line[128];

    Rng r = rng_at(0x9E3779B9ull, -13, 71);

    auto push_u64 = [&](uint64_t v) {
        std::snprintf(line, sizeof(line), "%016llx\n",
                      static_cast<unsigned long long>(v));
        out += line;
    };
    auto push_f = [&](float v) {
        uint32_t bits = 0;
        std::memcpy(&bits, &v, sizeof(bits));
        std::snprintf(line, sizeof(line), "%08x\n",
                      static_cast<unsigned int>(bits));
        out += line;
    };

    for (int i = 0; i < 8; ++i) push_u64(r.next_u64());
    for (int i = 0; i < 8; ++i) push_f(r.next_float());
    for (int i = 0; i < 8; ++i) push_f(r.unit_float());
    for (int i = 0; i < 8; ++i) push_f(r.open_unit_float());
    for (int i = 0; i < 8; ++i) push_f(r.normal(1.5f, 0.25f));
    for (int i = 0; i < 8; ++i) {
        const glm::vec2 p = r.in_unit_disc();
        push_f(p.x);
        push_f(p.y);
    }
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 p = r.on_unit_sphere();
        push_f(p.x);
        push_f(p.y);
        push_f(p.z);
    }
    for (int i = 0; i < 8; ++i) {
        const glm::vec3 p = r.in_unit_sphere();
        push_f(p.x);
        push_f(p.y);
        push_f(p.z);
    }
    for (int32_t z = -2; z <= 2; ++z) {
        for (int32_t x = -2; x <= 2; ++x) push_u64(hash_coord(0xABCDEFull, x, z));
    }
    return out;
}

void same_process_repeats_itself() {
    REQUIRE_MSG(digest() == digest(), "two calls in one process disagreed",
                "in-process");
    apricot_test::pass("the digest repeats within one process");
}

// THE FRESH-PROCESS CASE.
//
// Re-executes this binary with --dump and compares its stdout to the digest
// computed here. An in-process check cannot see anything that varies between
// runs — address layout, static initialisation order, a lazily-cached global
// that got seeded from something it should not have. Those all reproduce
// perfectly within a process and not at all between two.
void a_fresh_process_produces_the_same_values(const char* self) {
    const std::string command = std::string("\"") + self + "\" --dump";

    std::FILE* child = popen(command.c_str(), "r");
    REQUIRE_MSG(child != nullptr, "could not re-execute the test binary",
                "fresh process");

    std::string child_output;
    char buffer[4096];
    while (std::fgets(buffer, sizeof(buffer), child) != nullptr) {
        child_output += buffer;
    }
    const int status = pclose(child);

    REQUIRE_MSG(status == 0, "the re-executed test binary failed",
                "fresh process");
    REQUIRE_MSG(!child_output.empty(), "the re-executed binary printed nothing",
                "fresh process");
    REQUIRE_MSG(child_output == digest(),
                "a FRESH PROCESS produced different values from this one",
                "fresh process");

    std::printf("      (%zu bytes of digest matched across a process boundary)\n",
                child_output.size());
    apricot_test::pass("a fresh process produces byte-identical values");
}

}  // namespace

int main(int argc, char** argv) {
    // The child half of the fresh-process check. Prints the digest and exits,
    // with no test output of its own so the comparison is exact.
    if (argc > 1 && std::strcmp(argv[1], "--dump") == 0) {
        const std::string d = digest();
        std::fwrite(d.data(), 1, d.size(), stdout);
        return 0;
    }

    std::printf("rng_distribution_tests\n");
    the_one_remaining_zero_is_known_and_pinned();
    every_helper_consumes_a_fixed_number_of_draws();
    open_unit_float_is_never_zero();
    normal_is_actually_normal();
    disc_and_circle_are_area_uniform();
    sphere_is_uniform_on_surface_and_in_volume();
    same_process_repeats_itself();
    a_fresh_process_produces_the_same_values(argv[0]);
    return apricot_test::done("rng_distribution_tests");
}
