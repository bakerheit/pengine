#pragma once

#include <algorithm>
#include <cmath>

#include <glm/glm.hpp>

namespace apricot {

// The reticle is nailed to screen centre, so where the camera sits decides
// what you can shoot at. A rig centred behind the head puts the player's own
// back under the crosshair and you cannot see what you are pointing at, which
// is why idle carries a shoulder offset of its own rather than starting at
// zero. Raising the gun swings it further out and pulls the eye in.
inline constexpr float kWeaponAimIdleShoulderM = .68f;
inline constexpr float kWeaponAimShoulderM = .72f;
inline constexpr float kWeaponAimPitchLimit = 1.5533431f;

struct WeaponAimCamera {
    glm::vec3 target{0};
    glm::vec3 eye{0};
    glm::vec3 pivot{0};
    glm::vec3 direction{0, 0, -1};
    float fov_y = glm::radians(60.0f);
};

struct WeaponAimAngles {
    float yaw = 0.0f;
    float pitch = 0.0f;
    glm::vec3 direction{0, 0, -1};
    bool reachable = false;
};

namespace weapon_aim_detail {
inline bool finite(glm::vec3 value) {
    return std::isfinite(value.x) && std::isfinite(value.y) &&
           std::isfinite(value.z);
}
inline float blend(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}
inline glm::vec3 forward(float yaw, float pitch) {
    const float cp = std::cos(pitch);
    return {cp * std::sin(yaw), std::sin(pitch), -cp * std::cos(yaw)};
}
} // namespace weapon_aim_detail

// Lateral offset of the camera axis from the player's centre line, in metres.
// One producer, because weapon_aim_camera() and weapon_aim_toward() invert
// each other: if they disagree by a centimetre the sight ray stops landing
// where the crosshair is drawn.
inline float weapon_aim_shoulder(float aim_blend) {
    return glm::mix(kWeaponAimIdleShoulderM, kWeaponAimShoulderM,
                    weapon_aim_detail::blend(aim_blend));
}

// One geometry producer for both the rendered shoulder camera and its sight
// ray. Collision may shorten the eye-to-target distance, but must preserve
// the resulting view ray rather than silently aim from an older frame.
inline WeaponAimCamera weapon_aim_camera(glm::vec3 feet, float yaw, float pitch,
                                         float aim_blend) {
    constexpr float two_pi = 6.2831853071795864769f;
    const float aim = weapon_aim_detail::blend(aim_blend);
    if (!weapon_aim_detail::finite(feet)) feet = glm::vec3{0};
    yaw = std::isfinite(yaw) ? std::remainder(yaw, two_pi) : 0.0f;
    pitch = std::isfinite(pitch)
        ? std::clamp(pitch, -kWeaponAimPitchLimit, kWeaponAimPitchLimit) : 0.0f;
    WeaponAimCamera out;
    out.pivot = feet + glm::vec3{0, glm::mix(1.38f, 1.48f, aim), 0};
    out.direction = weapon_aim_detail::forward(yaw, pitch);
    const glm::vec3 right{std::cos(yaw), 0, std::sin(yaw)};
    out.target = out.pivot + right * weapon_aim_shoulder(aim);
    out.eye = out.target - out.direction * glm::mix(4.8f, 2.25f, aim);
    out.fov_y = glm::radians(glm::mix(60.0f, 48.0f, aim));
    return out;
}

// Solve the offset sight line, not just the bearing from the player's feet.
// At yaw y, a target must lie exactly shoulder metres to the right of the
// central forward plane. The forward component then determines pitch.
// Points inside the shoulder orbit (or beyond the pitch limit) are physically
// unreachable with this fixed camera layout; return finite angles and say so.
inline WeaponAimAngles weapon_aim_toward(glm::vec3 feet, glm::vec3 target,
                                         float aim_blend = 1.0f) {
    WeaponAimAngles out;
    if (!weapon_aim_detail::finite(feet) || !weapon_aim_detail::finite(target)) return out;
    const float aim = weapon_aim_detail::blend(aim_blend);
    const auto camera = weapon_aim_camera(feet, 0, 0, aim);
    const double dx = static_cast<double>(target.x) - static_cast<double>(camera.pivot.x);
    const double dy = static_cast<double>(target.y) - static_cast<double>(camera.pivot.y);
    const double dz = static_cast<double>(target.z) - static_cast<double>(camera.pivot.z);
    const double radius = std::hypot(dx, dz);
    const double shoulder = static_cast<double>(weapon_aim_shoulder(aim));
    const double bearing = radius > 0.0 ? std::atan2(dx, -dz) : 0.0;
    const double yaw = bearing - (radius > 0.0
        ? std::asin(std::clamp(shoulder / radius, 0.0, 1.0)) : 0.0);
    const double along = std::sqrt(std::max(0.0, (radius - shoulder) * (radius + shoulder)));
    const double pitch = (along > 0.0 || dy != 0.0) ? std::atan2(dy, along) : 0.0;
    constexpr double two_pi = 6.2831853071795864769;
    out.yaw = static_cast<float>(std::remainder(yaw, two_pi));
    out.pitch = static_cast<float>(std::clamp(pitch,
        -static_cast<double>(kWeaponAimPitchLimit), static_cast<double>(kWeaponAimPitchLimit)));
    out.direction = weapon_aim_detail::forward(out.yaw, out.pitch);
    out.reachable = radius >= shoulder &&
        std::fabs(pitch) <= static_cast<double>(kWeaponAimPitchLimit);
    return out;
}

} // namespace apricot
