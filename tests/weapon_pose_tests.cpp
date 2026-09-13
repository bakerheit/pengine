#include <cstdio>
#include <filesystem>
#include <limits>

#include "app/weapon_pose.h"
#include "core/asset_root.h"
#include "test_assert.h"

using namespace apricot;

namespace {
glm::mat4 palm(const Skeleton& skeleton, const std::vector<glm::mat4>& local,
               const char* side) {
    std::vector<glm::mat4> skin;
    skeleton.compute_skin_matrices(local, skin);
    glm::mat4 socket{1};
    int hand = -1;
    REQUIRE(weapon_pose_detail::palm_socket(skeleton, side, hand, socket));
    return skin[static_cast<std::size_t>(hand)] *
        glm::inverse(skeleton.bone(hand).inverse_bind) * socket;
}

void sampled_clip_contract(const Skeleton& skeleton, const SkinnedEmesh& mesh,
                           float scale, const char* clip) {
    Animation animation;
    REQUIRE(animation.load(asset_path(std::string{
        "models/characters/psx_pack/animations/"} + clip + ".eanim"), skeleton));
    VehicleDriverPose scratch;
    for (float time : {0.0f, .23f, .65f}) {
        std::vector<glm::mat4> base;
        animation.sample(time, skeleton, base);
        strip_root_motion_xz(skeleton, base);
        auto untouched = base;
        REQUIRE(!apply_player_weapon_pose(skeleton, scale, {}, untouched, scratch));
        REQUIRE(untouched == base);
        PlayerWeaponPose state{WeaponId::Pistol, 1, 1, 0, 0, false, 0};
        auto aimed = base;
        REQUIRE(apply_player_weapon_pose(skeleton, scale, state, aimed, scratch));
        const glm::mat4 aim_palm = palm(skeleton, aimed, "Right");
        const glm::vec3 aim_direction = -glm::normalize(glm::vec3{aim_palm[2]});
        REQUIRE(glm::dot(aim_direction, glm::vec3{0, 0, 1}) > .999f);
        const float support_distance = glm::distance(glm::vec3{aim_palm[3]},
            glm::vec3{palm(skeleton, aimed, "Left")[3]}) * scale;
        REQUIRE(support_distance < .105f);
        REQUIRE(support_distance > .015f);
        std::vector<glm::mat4> grip_skin;
        skeleton.compute_skin_matrices(aimed, grip_skin);
        std::vector<glm::vec4> grip_real, grip_dual;
        skin_matrices_to_dual_quaternions(grip_skin, grip_real, grip_dual);
        for (const char* side : {"Right", "Left"}) {
            const int tip = skeleton.find_bone(
                std::string{"mixamorig:"} + side + "HandIndex3");
            REQUIRE(tip >= 0);
            const glm::mat4 tip_world = grip_skin[static_cast<std::size_t>(tip)] *
                glm::inverse(skeleton.bone(tip).inverse_bind);
            const glm::mat4 hand_palm = palm(skeleton, aimed, side);
            // A relaxed, extended mitten hung far below the pistol in game.
            // Both fingertip joints must stay around their own grip socket.
            REQUIRE(glm::distance(glm::vec3{tip_world[3]},
                                   glm::vec3{hand_palm[3]}) * scale < .095f);
            for (const auto& vertex : mesh.vertices) {
                bool distal = false;
                for (int influence = 0; influence < 4; ++influence)
                    distal = distal || (vertex.bone_idx[influence] == tip &&
                        vertex.bone_weight[influence] > .5f);
                if (!distal) continue;
                // The distal mesh extends beyond Index3 itself. Check the
                // rendered skin, which exposed the dangling fingertips.
                const auto point = skinned_vertex_position(vertex, grip_real, grip_dual);
                REQUIRE(glm::distance(point, glm::vec3{hand_palm[3]}) * scale < .115f);
            }
        }
        for (int bone = 0; bone < skeleton.bone_count(); ++bone) {
            const auto index = static_cast<std::size_t>(bone);
            const auto& name = skeleton.bone(bone).name;
            const bool arm = name.find("Arm") != std::string::npos ||
                name.find("Hand") != std::string::npos;
            if (!arm) REQUIRE(aimed[index] == base[index]);
            // No stretching a forearm or shifting a joint to reach the prop.
            REQUIRE(glm::distance(glm::vec3{aimed[index][3]},
                                   glm::vec3{base[index][3]}) < .001f);
            for (int axis = 0; axis < 3; ++axis)
                REQUIRE_NEAR(glm::length(glm::vec3{aimed[index][axis]}),
                             glm::length(glm::vec3{base[index][axis]}), .001f);
        }
        state.aim_blend = 0;
        auto lowered = base;
        REQUIRE(apply_player_weapon_pose(skeleton, scale, state, lowered, scratch));
        const auto low_palm = palm(skeleton, lowered, "Right");
        REQUIRE(glm::vec3{low_palm[3]}.y < glm::vec3{aim_palm[3]}.y - .12f / scale);
        REQUIRE((-glm::normalize(glm::vec3{low_palm[2]})).y < -.5f);
        state.aim_blend = 1;
        state.pitch = .55f;
        auto pitched = base;
        REQUIRE(apply_player_weapon_pose(skeleton, scale, state, pitched, scratch));
        REQUIRE((-glm::normalize(glm::vec3{palm(skeleton, pitched, "Right")[2]})).y > .50f);
        state.pitch = 0;
        state.recoil = 1;
        auto recoiled = base;
        REQUIRE(apply_player_weapon_pose(skeleton, scale, state, recoiled, scratch));
        const auto kick = palm(skeleton, recoiled, "Right");
        REQUIRE((-glm::normalize(glm::vec3{kick[2]})).y > .12f);
        REQUIRE(glm::vec3{kick[3]}.z < glm::vec3{aim_palm[3]}.z - .02f / scale);

        state = {WeaponId::Pistol, 1, 0, 0, 0, true, 0};
        glm::vec3 previous_hand{0};
        float hand_travel = 0;
        for (int frame = 0; frame <= 120; ++frame) {
            state.reload_progress = static_cast<float>(frame) / 120.0f;
            auto reload = base;
            REQUIRE(apply_player_weapon_pose(skeleton, scale, state, reload, scratch));
            const auto hand = glm::vec3{palm(skeleton, reload, "Left")[3]} * scale;
            if (frame > 0) {
                const float step = glm::distance(hand, previous_hand);
                REQUIRE(step < .035f);
                hand_travel += step;
            }
            previous_hand = hand;
            if (frame == 120) {
                REQUIRE(glm::distance(glm::vec3{palm(skeleton, reload, "Right")[3]},
                                      glm::vec3{low_palm[3]}) * scale < .001f);
            }
        }
        REQUIRE(hand_travel > .30f);
    }
    apricot_test::pass(clip);
}

// THE MOLOTOV POSES ONE ARM. It borrows the pistol's solver — same IK, same
// finger rebuild, same blend — and the whole of the difference is that the
// support hand is left where the locomotion clip put it. Posing it anyway
// leaves the left hand gripping air beside the bottle, which is the exact bug
// this pins; the right hand still has to move, or the bottle is parented to a
// hand that never came up.
void molotov_poses_the_throwing_arm_only(const Skeleton& skeleton, float scale,
                                         const char* clip) {
    Animation animation;
    REQUIRE(animation.load(asset_path(std::string{
        "models/characters/psx_pack/animations/"} + clip + ".eanim"), skeleton));
    VehicleDriverPose scratch;
    std::vector<glm::mat4> base;
    animation.sample(0.31f, skeleton, base);
    strip_root_motion_xz(skeleton, base);

    auto held = base;
    REQUIRE(apply_player_weapon_pose(skeleton, scale,
        {WeaponId::Molotov, 1, 0, 0, 0, false, 0}, held, scratch));
    const float right_moved = glm::distance(
        glm::vec3{palm(skeleton, held, "Right")[3]},
        glm::vec3{palm(skeleton, base, "Right")[3]}) * scale;
    const float left_moved = glm::distance(
        glm::vec3{palm(skeleton, held, "Left")[3]},
        glm::vec3{palm(skeleton, base, "Left")[3]}) * scale;
    REQUIRE_MSG(right_moved > .10f, "the throwing hand never came up", clip);
    REQUIRE_MSG(left_moved < .001f, "the support hand was posed too", clip);

    // Taking aim cocks the bottle BACK and UP, which is the wind-up: a
    // molotov that pushed forward on aim would be sighting down a bottle.
    auto aimed = base;
    REQUIRE(apply_player_weapon_pose(skeleton, scale,
        {WeaponId::Molotov, 1, 1, 0, 0, false, 0}, aimed, scratch));
    const glm::vec3 rest{palm(skeleton, held, "Right")[3]};
    const glm::vec3 cocked{palm(skeleton, aimed, "Right")[3]};
    REQUIRE_MSG(cocked.y > rest.y, "aiming did not raise the bottle", clip);
    REQUIRE_MSG(cocked.z < rest.z, "aiming did not draw the bottle back", clip);
    // And the neck stays UP through both: the prop is built along the palm
    // socket's +Y, so a frame that tipped past horizontal would pour the fuel
    // out of the bottle the player is about to throw.
    for (const auto& posed : {held, aimed}) {
        const glm::mat4 socket = palm(skeleton, posed, "Right");
        REQUIRE_MSG(glm::normalize(glm::vec3{socket[1]}).y > .55f,
                    "the bottle tipped over in the hand", clip);
    }
    apricot_test::pass("molotov holds one arm");
}
} // namespace

int main() {
    VehicleDriverPose scratch;
    std::vector<glm::mat4> empty;
    REQUIRE(!apply_player_weapon_pose(Skeleton{}, 1,
        {WeaponId::Pistol, 1, 1, 0, 0, false, 0}, empty, scratch));
    REQUIRE_NEAR(weapon_pose_detail::unit(std::numeric_limits<float>::quiet_NaN()), 0, 0);
    const std::string root = "models/characters/psx_pack/player_male_01/skin";
    if (!std::filesystem::is_regular_file(asset_path(root + ".eskel"))) {
        std::printf("SKIP supplied weapon rig checks (private assets not staged)\n");
        return apricot_test::done("weapon_pose_tests");
    }
    Skeleton skeleton;
    SkinnedEmesh mesh;
    REQUIRE(skeleton.load(asset_path(root + ".eskel")));
    REQUIRE(read_skinned_emesh(asset_path(root + ".emesh"), mesh));
    const float scale = 1.76f / mesh.bounds.size().y;
    sampled_clip_contract(skeleton, mesh, scale, "idle");
    sampled_clip_contract(skeleton, mesh, scale, "walk");
    sampled_clip_contract(skeleton, mesh, scale, "sprint");
    molotov_poses_the_throwing_arm_only(skeleton, scale, "idle");
    molotov_poses_the_throwing_arm_only(skeleton, scale, "walk");
    return apricot_test::done("weapon_pose_tests");
}
