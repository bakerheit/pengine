// Punched-pedestrian reaction logic, headless.
//
// Pins the bucket boundaries, the reaction mapping and the waverer break, and
// then the part that is actually easy to get wrong: that the SPAWNER'S draw
// produces the split the constants claim.
//
// That last test is deliberately not the one it was lifted from. probablecause
// drew the nerve off a std::mt19937 and the test seeded one to match. Here the
// nerve is keyed — rng_at(seed, cell_x, cell_z, kChannelPedNerve) — so the test
// walks a lattice of spawn cells and rolls each one exactly the way the spawner
// will. It measures the real draw, and it also measures the property a stream
// could never give you: the SAME cell answers the same on a second pass, in a
// different order, with other cells' draws interleaved.

#include <cstdint>

#include "city/city_rng.h"
#include "city/pedestrian_reactions.h"
#include "test_assert.h"

using namespace apricot;
using ped_react::Disposition;
using ped_react::PunchReaction;

namespace {

// The nerve -> disposition buckets snap at the fraction boundaries. Boundaries
// are half-open on the low side (< COWARD_FRACTION is a coward), matching the
// spawner's uniform [0,1) draw.
void test_bucket_boundaries() {
    REQUIRE_MSG(ped_react::disposition_from_nerve(0.0f) == Disposition::Coward,
                "nerve 0.0 should be a coward", "lo");
    REQUIRE_MSG(ped_react::disposition_from_nerve(0.49f) == Disposition::Coward,
                "nerve just under the coward cut is a coward", "coward-hi");
    // At exactly COWARD_FRACTION we cross into waverer (strict < on the low edge).
    REQUIRE_MSG(ped_react::disposition_from_nerve(ped_react::COWARD_FRACTION)
                    == Disposition::Waverer,
                "nerve == coward fraction is a waverer", "coward/waverer edge");
    REQUIRE_MSG(ped_react::disposition_from_nerve(0.84f) == Disposition::Waverer,
                "nerve just under the waverer cut is a waverer", "waverer-hi");
    REQUIRE_MSG(ped_react::disposition_from_nerve(
                    ped_react::COWARD_FRACTION + ped_react::WAVERER_FRACTION)
                    == Disposition::DieHard,
                "nerve == coward+waverer fraction is a die-hard",
                "waverer/diehard edge");
    REQUIRE_MSG(ped_react::disposition_from_nerve(0.999f) == Disposition::DieHard,
                "nerve near 1.0 is a die-hard", "hi");
    apricot_test::pass("bucket boundaries are half-open on the low side");
}

// The real spawner draw: one keyed roll per spawn cell. A band, not an exact
// ratio — this catches an inverted comparison or a swapped bucket, and it also
// catches a channel that has quietly stopped decorrelating.
void test_keyed_draw_splits() {
    constexpr uint64_t kSeed = 0xA5A5'1234'C0FFEE01ull;
    constexpr int kSide = 450;  // 202,500 spawn cells
    int counts[3] = {0, 0, 0};
    for (int32_t z = 0; z < kSide; ++z) {
        for (int32_t x = 0; x < kSide; ++x) {
            const float nerve =
                city_unit_roll(kSeed, x, z, /*slot*/ 0u, kChannelPedNerve);
            REQUIRE(nerve >= 0.0f && nerve < 1.0f);
            switch (ped_react::disposition_from_nerve(nerve)) {
                case Disposition::Coward:  ++counts[0]; break;
                case Disposition::Waverer: ++counts[1]; break;
                case Disposition::DieHard: ++counts[2]; break;
            }
        }
    }
    const float n = static_cast<float>(kSide) * static_cast<float>(kSide);
    const float coward  = static_cast<float>(counts[0]) / n;
    const float waverer = static_cast<float>(counts[1]) / n;
    const float diehard = static_cast<float>(counts[2]) / n;
    // Each within 2 percentage points of its configured fraction.
    REQUIRE_MSG(coward  > ped_react::COWARD_FRACTION  - 0.02f &&
                coward  < ped_react::COWARD_FRACTION  + 0.02f,
                "coward fraction out of band", "dist-coward");
    REQUIRE_MSG(waverer > ped_react::WAVERER_FRACTION - 0.02f &&
                waverer < ped_react::WAVERER_FRACTION + 0.02f,
                "waverer fraction out of band", "dist-waverer");
    const float diehard_expected =
        1.0f - ped_react::COWARD_FRACTION - ped_react::WAVERER_FRACTION;
    REQUIRE_MSG(diehard > diehard_expected - 0.02f &&
                diehard < diehard_expected + 0.02f,
                "die-hard fraction out of band", "dist-diehard");
    apricot_test::pass("the keyed nerve roll splits at the configured fractions");
}

// THE REASON THE DRAW IS KEYED. Re-rolling one cell after thousands of
// unrelated cells have been rolled in between must give the identical nerve. A
// sequential stream fails this by construction, and it fails it as a desync an
// hour into a session rather than as a test.
void test_keyed_draw_is_order_independent() {
    constexpr uint64_t kSeed = 0xA5A5'1234'C0FFEE01ull;
    const float first = city_unit_roll(kSeed, 17, -42, 3u, kChannelPedNerve);

    volatile float sink = 0.0f;
    for (int32_t z = -30; z < 30; ++z)
        for (int32_t x = -30; x < 30; ++x)
            sink = sink + city_unit_roll(kSeed, x, z, 1u, kChannelPedNerve);

    const float again = city_unit_roll(kSeed, 17, -42, 3u, kChannelPedNerve);
    REQUIRE(first == again);

    // A different slot in the same cell is a different pedestrian, and a
    // different channel is a different decision about the same one. Neither may
    // come back with the first one's number.
    REQUIRE(city_unit_roll(kSeed, 17, -42, 4u, kChannelPedNerve) != first);
    REQUIRE(city_unit_roll(kSeed, 17, -42, 3u, kChannelPedSpace) != first);
    apricot_test::pass("a keyed roll does not care what was drawn in between");
}

// The reaction mapping the aggressive/coward split reads from.
void test_reaction_mapping() {
    REQUIRE_MSG(ped_react::punch_reaction(Disposition::Coward) == PunchReaction::Flee,
                "coward flees", "coward");
    REQUIRE_MSG(ped_react::punch_reaction(Disposition::Waverer) == PunchReaction::Fight,
                "waverer fights", "waverer");
    REQUIRE_MSG(ped_react::punch_reaction(Disposition::DieHard) == PunchReaction::Fight,
                "die-hard fights", "diehard");
    apricot_test::pass("only cowards flee a landed punch");
}

// A waverer breaks and flees once hurt to/below the threshold; above it, it
// keeps fighting. The caller only consults this for waverers, so "die-hards
// never break" is a caller invariant — asserted here by the reaction staying
// Fight regardless of health.
void test_fighter_break() {
    constexpr float MAX = 100.f;
    REQUIRE_MSG(!ped_react::waverer_should_break(100.f, MAX),
                "healthy waverer does not break", "healthy");
    REQUIRE_MSG(!ped_react::waverer_should_break(41.f, MAX),
                "waverer just above threshold does not break", "just-above");
    REQUIRE_MSG(ped_react::waverer_should_break(
                    MAX * ped_react::WAVERER_BREAK_HEALTH_FRAC, MAX),
                "waverer at exactly the threshold breaks", "at-threshold");
    REQUIRE_MSG(ped_react::waverer_should_break(1.f, MAX),
                "near-dead waverer breaks", "near-dead");
    REQUIRE_MSG(ped_react::punch_reaction(Disposition::DieHard) == PunchReaction::Fight,
                "die-hard always fights (never breaks)", "diehard-invariant");
    apricot_test::pass("the break threshold is inclusive, and die-hards ignore it");
}

// Fighter tunables are internally consistent (an inverted constant would make
// the fighter never land / never close). Cheap guards, not feel tuning.
void test_fighter_tunables() {
    // Closes to inside punch reach before it stops to swing.
    REQUIRE_MSG(ped_react::FIGHT_CLOSE_RANGE_M < ped_react::FIGHT_PUNCH_REACH_M,
                "close range is inside punch reach", "reach-order");
    // The contact window is a real sub-interval of the swing.
    REQUIRE_MSG(ped_react::FIGHT_CONTACT_LO_FRAC < ped_react::FIGHT_CONTACT_HI_FRAC,
                "contact lo < hi", "contact-window");
    REQUIRE_MSG(ped_react::FIGHT_CONTACT_HI_FRAC <= 1.0f,
                "contact window within the clip", "contact-bound");
    // Give-up range is well outside punch reach (you can step back without
    // instantly de-escalating).
    REQUIRE_MSG(ped_react::FIGHT_GIVE_UP_RANGE_M > ped_react::FIGHT_PUNCH_REACH_M,
                "give-up range beyond reach", "giveup-order");
    REQUIRE_MSG(ped_react::FIGHT_PUNCH_DAMAGE > 0.f &&
                    ped_react::FIGHT_PUNCH_PERIOD_S > 0.f,
                "positive damage + cadence", "positive");
    apricot_test::pass("fighter tunables are ordered the way the fight needs");
}

} // namespace

int main() {
    test_bucket_boundaries();
    test_keyed_draw_splits();
    test_keyed_draw_is_order_independent();
    test_reaction_mapping();
    test_fighter_break();
    test_fighter_tunables();
    return apricot_test::done("ped_reaction_tests");
}
