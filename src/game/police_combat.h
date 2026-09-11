#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include <glm/glm.hpp>

namespace apricot {

inline constexpr float kPoliceShootRangeM = 32.0f;
inline constexpr float kPoliceArmedStandOffM = 14.0f;
inline constexpr float kPoliceBulletDamage = 8.0f;
inline constexpr float kPoliceBulletHitRadiusM = 0.45f;
inline constexpr int64_t kPoliceShotPeriodSteps = 138;   // 1.15 s at 120 Hz
inline constexpr int64_t kPoliceShotReactionSteps = 42; // 0.35 s after aim

// Shooting AT a cop and KILLING one are different crimes, priced on the same
// scale as the rest (level 2 at 3 heat, level 3 at 6, level 5 at 12). A wound
// is a shade under an armed-threat report; killing an officer on top of that
// lands the average player straight into a three-star response, which is the
// point - a unit you take off the board costs you a heavier one.
inline constexpr float kOfficerWoundedHeat = 2.0f;
inline constexpr float kOfficerKilledHeat = 4.0f;

// Civilians, on the same scale and a tier below the uniform. Two rounds into a
// shopper plus the third that kills them comes to 4.5 heat - a two-star
// response, short of the three an officer's death buys.
//
// Killing somebody with the CAR is priced under killing them with the pistol,
// and deliberately so: at the moment it happens the city cannot tell a
// hit-and-run from a bad accident, and pricing the two identically turns every
// clipped kerb-stepper into a shooting.
inline constexpr float kCivilianWoundedHeat = 0.75f;
inline constexpr float kCivilianKilledHeat = 3.0f;
inline constexpr float kCivilianRunDownHeat = 2.0f;

// THE CITY NOTICES A KILLING WHETHER OR NOT ANYBODY WATCHED IT. That is the
// call app.cpp already makes for shooting an officer, stated once here so the
// two cannot drift: the witness cone in game/police_offenses.h gates the
// offences that are arguable - speeding, a red light, a drawn weapon - and a
// body in the road is not one of them.

struct PoliceShotEvent {
    uint64_t lane_key = 0;
    uint32_t slot = 0;
    uint32_t ordinal = 0;
    int64_t step = 0;
    glm::vec3 origin{0.0f};
    glm::vec3 end{0.0f};
};

inline uint64_t police_combat_mix(uint64_t x) {
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

inline int64_t police_first_shot_step(int64_t now, uint64_t lane_key,
                                      uint32_t slot) {
    const uint64_t key = police_combat_mix(
        lane_key ^ (static_cast<uint64_t>(slot) << 32) ^ 0x53484F54ull);
    return now + kPoliceShotReactionSteps + static_cast<int64_t>(key % 19u);
}

// Alpha-style distance spread, keyed only to stable officer identity and shot
// ordinal. Scan order and frame batching cannot change where the bullet goes.
inline PoliceShotEvent make_police_shot(uint64_t lane_key, uint32_t slot,
                                        uint32_t ordinal, int64_t step,
                                        glm::vec3 origin,
                                        glm::vec3 player_torso) {
    PoliceShotEvent shot{lane_key, slot, ordinal, step, origin, player_torso};
    glm::vec3 forward = player_torso - origin;
    const float distance = glm::length(forward);
    if (!(distance > 1e-4f) || !std::isfinite(distance)) return shot;
    forward /= distance;
    glm::vec3 right = glm::cross(forward, glm::vec3{0.0f, 1.0f, 0.0f});
    if (glm::length(right) < 1e-4f) right = {1.0f, 0.0f, 0.0f};
    else right = glm::normalize(right);
    const glm::vec3 up = glm::normalize(glm::cross(right, forward));
    const uint64_t seed = police_combat_mix(
        lane_key ^ (static_cast<uint64_t>(slot) << 32) ^
        (static_cast<uint64_t>(ordinal) * 0xD1B54A32D192ED03ull));
    const float a = static_cast<float>((seed >> 8) & 0xFFFFFFu) /
        static_cast<float>(0x1000000u);
    const float b = static_cast<float>((seed >> 32) & 0xFFFFFFu) /
        static_cast<float>(0x1000000u);
    const float radius = std::sqrt(a) *
        (0.08f + 1.80f * std::clamp(distance / kPoliceShootRangeM, 0.0f, 1.0f));
    const float angle = b * 6.283185307f;
    shot.end = player_torso + right * (std::cos(angle) * radius) +
        up * (std::sin(angle) * radius);
    return shot;
}

inline bool police_shot_hits_player(const PoliceShotEvent& shot,
                                    glm::vec3 player_torso,
                                    float radius = kPoliceBulletHitRadiusM) {
    const glm::vec3 segment = shot.end - shot.origin;
    const float length2 = glm::dot(segment, segment);
    if (!(length2 > 1e-8f) || !std::isfinite(length2)) return false;
    const float t = std::clamp(
        glm::dot(player_torso - shot.origin, segment) / length2, 0.0f, 1.0f);
    const glm::vec3 closest = shot.origin + segment * t;
    const glm::vec3 miss = player_torso - closest;
    return glm::dot(miss, miss) <= radius * radius;
}

}  // namespace apricot
