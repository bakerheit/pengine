#pragma once

#include <algorithm>
#include <cmath>

namespace apricot {

// ONE SCALE FOR EVERY PERSON IN PINATTY. The player, the crowd on the pavement
// and the officers out of their cruisers all carry the same hundred points and
// spend them at the same rate, because the alternative — a pistol that is
// lethal to a civilian and a nuisance to a cop, tuned in two headers that do
// not know about each other — is a balance argument nobody can win. A round is
// worth what it is worth; who it lands in decides what happens next, not how
// much it hurt.
//
// The thing that kept expiring before this header existed: everybody had a
// knockdown and nobody had a death. A bullet floored a pedestrian for seven
// seconds and they got up; three rounds put an officer on the road for twelve
// and his health came back with him. The knockdown was standing in for damage,
// so the only number the player could move was a timer.
inline constexpr float kBodyHealth = 100.0f;

// Three centre-mass pistol rounds. The number is shared rather than restated
// because city/police_officer.h already had its own copy and the two were free
// to drift; a pistol that takes three rounds to kill a cop and four to kill a
// shopper is a bug you find by accident, months later.
inline constexpr float kPistolBodyDamage = 34.0f;

// A punch is a punch: four connected blows floor somebody who has not been
// shot, and fewer than that if they have. The second half is the property that
// makes health visible in a fist fight — a wounded man goes down to one jab —
// and it is why the fist spends from the same hundred points as everything
// else rather than owning a knockdown counter of its own.
inline constexpr float kPunchBodyDamage = 25.0f;

// Falling. Below the free drop nothing happens; above it the damage tracks the
// landing speed, because that is the quantity that hurts. Terminal-ish speeds
// off a tower kill outright.
inline constexpr float kFallSafeSpeedMps = 9.0f;    // ~4 m drop
inline constexpr float kFallLethalSpeedMps = 22.0f; // ~25 m drop

// Being run over. `knockdown_speed_mps` is the crowd's own floor for a contact
// counting at all, passed in rather than duplicated: this curve must start
// exactly where the knockdown starts or there is a band of speeds that floors
// a person without ever touching their health.
//
// Lethal at 13.4 m/s — thirty miles an hour, the speed at which being hit by a
// car stops being an injury in the real world too. The ramp is SQUARED across
// that band because the energy in the impact is, and the feel of it is the
// reason to keep it: a kerb-speed nudge floors somebody without hurting them,
// a twenty-five mile an hour hit does most of a person's health, and thirty is
// a death. A linear ramp made every speed above walking pace feel the same.
inline constexpr float kVehicleLethalSpeedMps = 13.4f;

inline float vehicle_impact_damage(float closing_speed_mps,
                                   float knockdown_speed_mps) {
    if (!std::isfinite(closing_speed_mps) || !std::isfinite(knockdown_speed_mps))
        return 0.0f;
    const float floor_mps = std::max(0.0f, knockdown_speed_mps);
    const float span = kVehicleLethalSpeedMps - floor_mps;
    if (!(span > 1e-3f)) return closing_speed_mps >= floor_mps ? kBodyHealth : 0.0f;
    const float t = std::clamp((closing_speed_mps - floor_mps) / span, 0.0f, 1.0f);
    return kBodyHealth * t * t;
}

inline float fall_impact_damage(float landing_speed_mps) {
    if (!std::isfinite(landing_speed_mps)) return 0.0f;
    constexpr float span = kFallLethalSpeedMps - kFallSafeSpeedMps;
    const float t = std::clamp(
        (landing_speed_mps - kFallSafeSpeedMps) / span, 0.0f, 1.0f);
    return kBodyHealth * t * t;
}

// Crashing the car you are driving. A shell, a belt and a crumple zone are
// worth a great deal, so this is NOT the pedestrian curve: it starts where a
// shunt stops being a scrape and runs out to a speed nobody walks away from.
//
// Twenty-five miles an hour is the floor because below it the body panels take
// everything — physics/vehicle_damage.h is already modelling that, and a
// driver who loses health every time they clip a bollard turns the whole
// system into an annoyance. Seventy-four is the ceiling, which is a head-on
// at motorway speed.
inline constexpr float kCrashSafeSpeedMps = 11.0f;
inline constexpr float kCrashLethalSpeedMps = 33.0f;

inline float crash_driver_damage(float impact_speed_mps) {
    if (!std::isfinite(impact_speed_mps)) return 0.0f;
    constexpr float span = kCrashLethalSpeedMps - kCrashSafeSpeedMps;
    const float t = std::clamp(
        (impact_speed_mps - kCrashSafeSpeedMps) / span, 0.0f, 1.0f);
    return kBodyHealth * t * t;
}

// Spend health, and report ONLY the blow that emptied it. Every consumer wants
// that edge and not the level — heat is charged for a killing, a death clip is
// started once, a corpse is counted once — and asking each of them to compare
// health before and after is how one of them eventually charges the player for
// a murder twice by shooting a body already on the road.
//
// Damage that is not finite or not positive is refused rather than clamped: a
// NaN closing speed arriving from a degenerate contact must not silently zero
// somebody's health, and it must not heal them either.
inline bool apply_body_damage(float& health, float damage) {
    if (!std::isfinite(health) || health <= 0.0f) return false;
    if (!std::isfinite(damage) || damage <= 0.0f) return false;
    health = std::max(0.0f, health - damage);
    return health <= 0.0f;
}

}  // namespace apricot
