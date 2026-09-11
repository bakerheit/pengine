#include "city/body_damage.h"

#include <limits>

#include "test_assert.h"

using namespace apricot;

namespace {

constexpr float kNaN = std::numeric_limits<float>::quiet_NaN();
constexpr float kInf = std::numeric_limits<float>::infinity();

// The killing-blow edge is the whole contract of apply_body_damage: every
// consumer charges heat, starts a death clip or counts a corpse on it, so it
// has to fire exactly once no matter how many rounds land afterwards.
void killing_blow_fires_once() {
    float health = kBodyHealth;
    REQUIRE(!apply_body_damage(health, kPistolBodyDamage));
    REQUIRE_NEAR(health, 66.0f, 1e-4);
    REQUIRE(!apply_body_damage(health, kPistolBodyDamage));
    REQUIRE_NEAR(health, 32.0f, 1e-4);
    REQUIRE(apply_body_damage(health, kPistolBodyDamage));
    REQUIRE_NEAR(health, 0.0f, 1e-6);
    // A body already at zero is not killed again, and not damaged further.
    REQUIRE(!apply_body_damage(health, kPistolBodyDamage));
    REQUIRE_NEAR(health, 0.0f, 1e-6);
    apricot_test::pass("three pistol rounds kill, and only the third reports it");
}

// Exactly the failure this refusal exists for: a degenerate contact hands over
// a NaN closing speed, and health must neither empty nor recover.
void garbage_damage_is_refused() {
    float health = 50.0f;
    REQUIRE(!apply_body_damage(health, kNaN));
    REQUIRE_NEAR(health, 50.0f, 1e-6);
    REQUIRE(!apply_body_damage(health, kInf - kInf));
    REQUIRE_NEAR(health, 50.0f, 1e-6);
    REQUIRE(!apply_body_damage(health, 0.0f));
    REQUIRE_NEAR(health, 50.0f, 1e-6);
    REQUIRE(!apply_body_damage(health, -25.0f));
    REQUIRE_NEAR(health, 50.0f, 1e-6);
    float broken = kNaN;
    REQUIRE(!apply_body_damage(broken, 10.0f));
    apricot_test::pass("NaN, zero and negative damage change nothing");
}

// The band between the knockdown floor and the lethal speed is where the whole
// feel of being run over lives. Its two ends are the load-bearing part: a
// contact at the floor must cost nothing, and thirty miles an hour must kill.
void vehicle_impact_spans_knockdown_to_lethal() {
    constexpr float knock = 2.5f;  // PedLifeTuning::knockdown_speed_mps
    REQUIRE_NEAR(vehicle_impact_damage(knock, knock), 0.0f, 1e-6);
    REQUIRE_NEAR(vehicle_impact_damage(0.0f, knock), 0.0f, 1e-6);
    REQUIRE_NEAR(vehicle_impact_damage(-4.0f, knock), 0.0f, 1e-6);
    REQUIRE(vehicle_impact_damage(kVehicleLethalSpeedMps, knock) >= kBodyHealth);
    REQUIRE(vehicle_impact_damage(40.0f, knock) <= kBodyHealth);

    // Survivable in the middle, and monotonic across the band: a faster car
    // must never hurt less than a slower one.
    const float mid = vehicle_impact_damage(8.0f, knock);
    REQUIRE(mid > 0.0f && mid < kBodyHealth);
    float previous = -1.0f;
    for (int i = 0; i <= 40; ++i) {
        const float speed = static_cast<float>(i) * 0.5f;
        const float damage = vehicle_impact_damage(speed, knock);
        REQUIRE_MSG(damage >= previous, "impact damage must not fall with speed",
                    "monotonic");
        previous = damage;
    }
    // Squared, not linear: half way up the band costs well under half a life,
    // which is what makes a slow bump and a fast one feel like different events.
    const float half = knock + 0.5f * (kVehicleLethalSpeedMps - knock);
    REQUIRE_NEAR(vehicle_impact_damage(half, knock), kBodyHealth * 0.25f, 1e-3);

    REQUIRE_NEAR(vehicle_impact_damage(kNaN, knock), 0.0f, 1e-6);
    REQUIRE_NEAR(vehicle_impact_damage(9.0f, kNaN), 0.0f, 1e-6);
    // A degenerate band (floor at or past lethal) still answers, and answers
    // lethally rather than dividing by nothing.
    REQUIRE_NEAR(vehicle_impact_damage(20.0f, 20.0f), kBodyHealth, 1e-6);
    REQUIRE_NEAR(vehicle_impact_damage(4.0f, 20.0f), 0.0f, 1e-6);
    apricot_test::pass("run-over damage spans the knockdown floor to thirty mph");
}

// A DRIVER IS NOT A PEDESTRIAN. Sharing the pedestrian curve was the tempting
// simplification and it is wrong in the direction that matters: thirty miles an
// hour into a wall would have killed the player outright, which is true of
// somebody standing in the road and emphatically not true of somebody sitting
// behind a crumple zone.
void crashes_are_survivable_far_past_a_pedestrian_impact() {
    REQUIRE_NEAR(crash_driver_damage(kCrashSafeSpeedMps), 0.0f, 1e-6);
    REQUIRE_NEAR(crash_driver_damage(4.0f), 0.0f, 1e-6);
    REQUIRE(crash_driver_damage(kCrashLethalSpeedMps) >= kBodyHealth);
    REQUIRE(crash_driver_damage(60.0f) <= kBodyHealth);
    REQUIRE_NEAR(crash_driver_damage(kNaN), 0.0f, 1e-6);

    // The load-bearing comparison: at the speed that kills a pedestrian
    // outright, the driver of the car doing the killing is hurt but alive.
    const float at_pedestrian_lethal = crash_driver_damage(kVehicleLethalSpeedMps);
    REQUIRE_MSG(at_pedestrian_lethal > 0.0f && at_pedestrian_lethal < kBodyHealth,
                "the speed that kills a pedestrian must hurt, not kill, the "
                "driver", "asymmetry");
    REQUIRE_MSG(crash_driver_damage(20.0f) <
                    vehicle_impact_damage(20.0f, 2.5f),
                "a crash costs the driver more than being run over costs the "
                "pedestrian", "asymmetry");

    float previous = -1.0f;
    for (int i = 0; i <= 80; ++i) {
        const float damage = crash_driver_damage(static_cast<float>(i) * 0.5f);
        REQUIRE_MSG(damage >= previous, "crash damage must not fall with speed",
                    "monotonic");
        previous = damage;
    }
    apricot_test::pass("a crash is survivable well past the speed that kills a pedestrian");
}

void falls_hurt_above_the_safe_drop() {
    REQUIRE_NEAR(fall_impact_damage(kFallSafeSpeedMps), 0.0f, 1e-6);
    REQUIRE_NEAR(fall_impact_damage(3.0f), 0.0f, 1e-6);
    REQUIRE(fall_impact_damage(kFallLethalSpeedMps) >= kBodyHealth);
    const float mid = fall_impact_damage(15.0f);
    REQUIRE(mid > 0.0f && mid < kBodyHealth);
    REQUIRE_NEAR(fall_impact_damage(kNaN), 0.0f, 1e-6);
    apricot_test::pass("a walking drop is free; a tower is not");
}

// Health is one scale, and a pistol reads the same on every body in the city.
// Three rounds, whoever is holding still for them.
void one_scale_for_every_body() {
    REQUIRE(kPistolBodyDamage * 3.0f >= kBodyHealth);
    REQUIRE(kPistolBodyDamage * 2.0f < kBodyHealth);
    REQUIRE(kPunchBodyDamage * 4.0f >= kBodyHealth);
    REQUIRE(kPunchBodyDamage < kPistolBodyDamage);
    apricot_test::pass("three rounds or four punches, on any body");
}

}  // namespace

int main() {
    killing_blow_fires_once();
    garbage_damage_is_refused();
    vehicle_impact_spans_knockdown_to_lethal();
    crashes_are_survivable_far_past_a_pedestrian_impact();
    falls_hurt_above_the_safe_drop();
    one_scale_for_every_body();
    return apricot_test::done("body_damage_tests");
}
