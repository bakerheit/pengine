#include <filesystem>

#include "app/ped_impact_pose.h"
#include "test_assert.h"

using namespace apricot;

int main() {
    const auto car = pedestrian_impact_input(true, false, true);
    REQUIRE(car.downed && !car.dead);
    const auto bullet = pedestrian_impact_input(true, true, true);
    REQUIRE(!bullet.downed && bullet.dead && bullet.impact_from_front);
    const auto rising = pedestrian_impact_input(false, true, false);
    REQUIRE(!rising.downed && !rising.dead);
    CharacterAnimSample sample;
    sample.root = ClipRoot::AnchorStart;
    REQUIRE(pedestrian_impact_sample(sample, false).root == ClipRoot::AnchorStart);
    REQUIRE(pedestrian_impact_sample(sample, true).root == ClipRoot::Strip);
    apricot_test::pass("bullet falls select authored pose; car knockdowns retain ragdoll signal");

    for (const char* model : {"player_male_01", "civilian_female_03"}) {
        const std::string root = std::string{"models/characters/psx_pack/"} + model + "/skin";
        REQUIRE(std::filesystem::is_regular_file(asset_path(root + ".eskel")));
        Skeleton skeleton;
        SkinnedEmesh mesh;
        CharacterClipSet clips;
        REQUIRE(skeleton.load(asset_path(root + ".eskel")));
        REQUIRE(read_skinned_emesh(asset_path(root + ".emesh"), mesh));
        REQUIRE(clips.load(skeleton));
        const float scale = 1.76f / mesh.bounds.size().y;
        std::vector<glm::mat4> bind;
        for (int bone = 0; bone < skeleton.bone_count(); ++bone)
            bind.push_back(skeleton.bone(bone).bind_local);
        const glm::vec2 bind_root = root_translation_xz(skeleton, bind);
        for (bool from_front : {false, true}) {
            CharacterAnimator animator;
            CharacterAnimInput idle;
            animator.advance(clips, idle, .1f);
            auto impact = pedestrian_impact_input(true, true, from_front);
            std::vector<BonePose> a, b;
            std::vector<glm::mat4> local, skin;
            std::vector<glm::vec4> real, dual;
            float largest_height = 0;
            float final_height = 0;
            for (int frame = 0; frame < 300; ++frame) {
                animator.advance(clips, impact, 1.0f / 60.0f);
                const auto fall = pedestrian_impact_sample(animator.sample(), true);
                REQUIRE(fall.clip == (from_front ? CharacterClip::DieBackward : CharacterClip::DieForward));
                evaluate_character_pose(clips, skeleton, fall, a, b, local);
                const auto root_xz = root_translation_xz(skeleton, local);
                REQUIRE(glm::length(root_xz - bind_root) < .0001f);
                skeleton.compute_skin_matrices(local, skin);
                skin_matrices_to_dual_quaternions(skin, real, dual);
                final_height = 0;
                for (const auto& vertex : mesh.vertices) {
                    const float height = (skinned_vertex_position(vertex, real, dual).y - mesh.bounds.min.y) * scale;
                    largest_height = std::max(largest_height, height);
                    final_height = std::max(final_height, height);
                }
            }
            REQUIRE(largest_height < 2.15f);
            REQUIRE(final_height < .85f);
            REQUIRE(animator.state() == CharacterAnimState::Downed);
            animator.advance(clips, rising, 1.0f / 60.0f);
            REQUIRE(animator.state() == CharacterAnimState::GetUp);
            REQUIRE(pedestrian_impact_sample(animator.sample(), true).root == ClipRoot::Strip);
            std::printf("      %s %s: peak %.3fm, down %.3fm\n", model,
                from_front ? "backward" : "forward", static_cast<double>(largest_height),
                static_cast<double>(final_height));
        }
    }
    apricot_test::pass("real skinned bullet falls remain bounded, settle low and enter recovery");
    return apricot_test::done("ped_impact_pose_tests");
}
