#pragma once

#include "app/vehicle_driver_pose.h"
#include "game/weapon.h"

namespace apricot {

struct PlayerWeaponPose {
    WeaponId weapon = WeaponId::Unarmed;
    float equip_blend = 0.0f;
    float aim_blend = 0.0f;
    float recoil = 0.0f;
    float reload_progress = 0.0f;
    bool reloading = false;
    float pitch = 0.0f; // radians, positive up
};

namespace weapon_pose_detail {
inline float unit(float value) {
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}
inline float ease(float value) {
    const float t = unit(value);
    return t * t * (3.0f - 2.0f * t);
}
inline float phase(float progress, float begin, float end) {
    return ease((progress - begin) / (end - begin));
}

// This is also the render attachment: the authored wrist-to-knuckle vector
// defines the grip's forward axis. Keep one construction for rig and prop.
inline bool palm_socket(const Skeleton& skeleton, const char* side,
                         int& hand, glm::mat4& socket) {
    const std::string prefix = std::string{"mixamorig:"} + side;
    hand = skeleton.find_bone(prefix + "Hand");
    const int index = skeleton.find_bone(prefix + "HandIndex1");
    if (hand < 0 || index < 0) return false;
    const glm::mat4 bind = glm::inverse(skeleton.bone(hand).inverse_bind);
    const glm::vec3 wrist{bind[3]};
    const glm::vec3 knuckle{
        glm::inverse(skeleton.bone(index).inverse_bind)[3]};
    const glm::vec3 forward = glm::normalize(knuckle - wrist);
    const glm::vec3 right = glm::normalize(
        glm::cross(forward, glm::vec3{0, 0, 1}));
    glm::mat4 frame{1.0f};
    frame[0] = glm::vec4{right, 0};
    frame[1] = glm::vec4{glm::cross(right, forward), 0};
    frame[2] = glm::vec4{-forward, 0};
    frame[3] = glm::vec4{glm::mix(wrist, knuckle, .70f), 1};
    socket = skeleton.bone(hand).inverse_bind * frame;
    return driver_pose_detail::finite(socket);
}
} // namespace weapon_pose_detail

// Apply after the locomotion sample. Only the arm chains change: foot plants,
// hip travel, jump pose and the existing clip crossfade remain intact. The
// supplied animations face +Z in model space (bind pose alone faces +X).
// Reuse the seated-pose analytic solver to preserve measured limb lengths.
inline bool apply_player_weapon_pose(const Skeleton& skeleton,
                                     float metres_per_unit,
                                     const PlayerWeaponPose& state,
                                     std::vector<glm::mat4>& local,
                                     VehicleDriverPose& scratch) {
    using namespace weapon_pose_detail;
    const float weight = ease(state.equip_blend);
    if (state.weapon != WeaponId::Pistol || weight <= 0.0f) return false;
    if (!std::isfinite(metres_per_unit) || metres_per_unit <= 0.0f ||
        local.size() != static_cast<std::size_t>(skeleton.bone_count())) return false;
    int chains[2][3]{};
    int fingers[2][3]{};
    glm::mat4 sockets[2]{};
    const char* sides[]{"Right", "Left"};
    for (int side = 0; side < 2; ++side) {
        const std::string prefix = std::string{"mixamorig:"} + sides[side];
        chains[side][0] = skeleton.find_bone(prefix + "Arm");
        chains[side][1] = skeleton.find_bone(prefix + "ForeArm");
        if (chains[side][0] < 0 || chains[side][1] < 0 ||
            !palm_socket(skeleton, sides[side], chains[side][2], sockets[side])) return false;
        for (int joint = 0; joint < 3; ++joint)
            fingers[side][joint] = skeleton.find_bone(
                prefix + "HandIndex" + std::to_string(joint + 1));
    }
    scratch.local = local;
    driver_pose_detail::globals(skeleton, scratch);
    if (scratch.joints.size() != local.size()) return false;
    const glm::vec3 shoulder = .5f * (
        glm::vec3{scratch.joints[static_cast<std::size_t>(chains[0][0])][3]} +
        glm::vec3{scratch.joints[static_cast<std::size_t>(chains[1][0])][3]});
    const glm::vec3 right{-1, 0, 0}, up{0, 1, 0}, forward{0, 0, 1};
    const auto target = [&](glm::vec3 point) {
        return shoulder + (right * point.x + up * point.y + forward * point.z) /
                              metres_per_unit;
    };
    const float recoil = unit(state.recoil);
    // A quick shot from low ready still presents the sights toward the shot.
    // Hold the raised pose through the kick, then ease back during recovery.
    const float aim = std::max(ease(state.aim_blend), ease(recoil * 4.0f));
    const float progress = unit(state.reload_progress);
    const float reload = state.reloading
        ? phase(progress, 0.0f, .16f) * (1.0f - phase(progress, .80f, 1.0f)) : 0.0f;
    const float pitch = std::isfinite(state.pitch)
        ? std::clamp(state.pitch, -.95f, .95f) : 0.0f;

    glm::vec3 grip = glm::mix(glm::vec3{.105f, -.29f, .27f},
                            glm::vec3{.025f, -.065f, .48f}, aim);
    // Raise around the shoulder so high/low aim changes reach as well as the
    // muzzle angle. The IK clamps unreachable endpoints without stretching.
    const glm::quat aim_pitch = glm::angleAxis(-pitch * aim, glm::vec3{1, 0, 0});
    grip = aim_pitch * grip;
    grip = glm::mix(grip, glm::vec3{.115f, -.22f, .25f}, reload);
    grip.y += recoil * .018f;
    grip.z -= recoil * .045f;
    const float muzzle_pitch = glm::mix(-.62f, pitch, aim) + recoil * .15f;
    const glm::vec3 direction = glm::normalize(
        forward * std::cos(muzzle_pitch) + up * std::sin(muzzle_pitch));
    glm::mat4 frame{1.0f};
    frame[0] = glm::vec4{right, 0};
    frame[1] = glm::vec4{glm::cross(right, direction), 0};
    frame[2] = glm::vec4{-direction, 0};
    glm::quat grip_rotation = glm::quat_cast(glm::mat3{frame});
    // Cant the pistol inward while the support hand works under the mag well.
    grip_rotation = glm::normalize(
        glm::angleAxis(-.42f * reload, direction) * grip_rotation);

    glm::vec3 palms[2]{grip, grip + glm::vec3{-.062f, -.025f, -.018f}};
    if (state.reloading) {
        const glm::vec3 fetch{-.18f, -.54f, .06f};
        const glm::vec3 magazine = grip + glm::vec3{-.035f, -.12f, .005f};
        const glm::vec3 under_grip = grip + glm::vec3{-.035f, -.045f, .005f};
        glm::vec3 support = glm::mix(palms[1], fetch, phase(progress, .05f, .27f));
        support = glm::mix(support, magazine, phase(progress, .30f, .53f));
        support = glm::mix(support, under_grip, phase(progress, .53f, .65f));
        palms[1] = glm::mix(support, palms[1], phase(progress, .76f, .96f));
    }
    for (int side = 0; side < 2; ++side) {
        const auto& chain = chains[side];
        const glm::quat hand_rotation = glm::normalize(grip_rotation * glm::inverse(
            glm::quat_cast(glm::mat3{sockets[side]})));
        const glm::vec3 wrist = target(palms[side]) -
            hand_rotation * glm::vec3{sockets[side][3]};
        const glm::vec3 elbow = target({side == 0 ? .34f : -.34f, -.32f, .10f});
        driver_pose_detail::limb(skeleton, scratch, chain[0], chain[1],
                                chain[2], wrist, elbow);
        driver_pose_detail::set_rotation(skeleton, scratch, chain[2], hand_rotation);

        // The pack has a three-joint mitten chain, not separate fingers.
        // Relaxed walk-track rotations point it away from a rotated wrist;
        // rebuild from the authored hand rest frame before curling around
        // the grip. Never reuse a previous frame's already-curled pose.
        for (int finger : fingers[side]) {
            if (finger < 0) continue;
            const auto index = static_cast<std::size_t>(finger);
            BonePose rest = decompose_bone_pose(scratch.local[index]);
            rest.rotation = decompose_bone_pose(skeleton.bone(finger).bind_local).rotation;
            scratch.local[index] = bone_pose_matrix(rest);
        }
        driver_pose_detail::globals(skeleton, scratch);
        const glm::vec3 curl_axis = grip_rotation * glm::vec3{-1, 0, 0};
        const float curls[]{.80f, 1.65f, 1.20f};
        for (int joint = 0; joint < 3; ++joint) {
            const int finger = fingers[side][joint];
            if (finger < 0) continue;
            const auto rotation = glm::quat_cast(glm::mat3{
                scratch.joints[static_cast<std::size_t>(finger)]});
            driver_pose_detail::set_rotation(skeleton, scratch, finger,
                glm::angleAxis(curls[joint], curl_axis) * rotation);
        }
    }
    const auto blend_joint = [&](int bone) {
        if (bone < 0) return;
        const auto index = static_cast<std::size_t>(bone);
        const BonePose original = decompose_bone_pose(local[index]);
        const BonePose posed = decompose_bone_pose(scratch.local[index]);
        BonePose result = original;
        result.rotation = glm::normalize(glm::slerp(original.rotation, posed.rotation, weight));
        local[index] = bone_pose_matrix(result);
    };
    for (const auto& chain : chains) for (int bone : chain) blend_joint(bone);
    for (const auto& chain : fingers) for (int bone : chain) blend_joint(bone);
    return true;
}

} // namespace apricot
