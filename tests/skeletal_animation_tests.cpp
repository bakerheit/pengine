#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "core/skeletal_animation.h"
#include "test_assert.h"

using namespace apricot;

namespace {

float matrix_delta(const glm::mat4& a, const glm::mat4& b) {
    float largest = 0.0f;
    for (int column = 0; column < 4; ++column) {
        for (int row = 0; row < 4; ++row) {
            largest = std::max(largest,
                std::fabs(a[column][row] - b[column][row]));
        }
    }
    return largest;
}

void supplied_rigs_match_the_runtime_contract() {
    static constexpr std::array<const char*, 19> kNames = {
        "player_male_01", "civilian_male_03", "civilian_male_05",
        "civilian_male_07", "civilian_male_09", "civilian_female_03",
        "civilian_female_05", "civilian_female_07", "civilian_female_09",
        "civilian_male_06", "civilian_male_08", "civilian_male_10",
        "civilian_male_11", "civilian_male_13", "civilian_male_14",
        "civilian_male_15", "civilian_male_17_police",
        "civilian_female_04", "civilian_female_14",
    };
    for (const char* name : kNames) {
        const std::string root = std::string(
            "models/characters/psx_pack/") + name + "/skin";
        SkinnedEmesh mesh;
        Skeleton skeleton;
        Animation idle;
        Animation walk;
        Animation sprint;
        REQUIRE_MSG(read_skinned_emesh(asset_path(root + ".emesh"), mesh),
                    "rigged emesh loads", name);
        REQUIRE_MSG(skeleton.load(asset_path(root + ".eskel")),
                    "skeleton loads", name);
        REQUIRE_MSG(skeleton.accepts(mesh), "bone indices fit skeleton", name);
        REQUIRE_MSG(idle.load(asset_path(
                        "models/characters/psx_pack/animations/idle.eanim"),
                    skeleton), "idle clip binds", name);
        REQUIRE_MSG(walk.load(asset_path(
                        "models/characters/psx_pack/animations/walk.eanim"),
                    skeleton), "walk clip binds", name);
        REQUIRE_MSG(sprint.load(asset_path(
                        "models/characters/psx_pack/animations/sprint.eanim"),
                    skeleton), "sprint clip binds", name);
        REQUIRE_MSG(idle.unresolved_channels() == 0,
                    "idle has no missing bones", name);
        REQUIRE_MSG(walk.unresolved_channels() == 0,
                    "walk has no missing bones", name);
        REQUIRE_MSG(sprint.unresolved_channels() == 0,
                    "sprint has no missing bones", name);
        REQUIRE(idle.duration() > 1.0f);
        REQUIRE(walk.duration() > 0.5f);
        REQUIRE(sprint.duration() > 0.25f);
        REQUIRE(skeleton.bone_count() > 0);
        REQUIRE(skeleton.bone_count() <= kMaxSkinBones);

        std::vector<glm::mat4> bind;
        for (int bone = 0; bone < skeleton.bone_count(); ++bone) {
            bind.push_back(skeleton.bone(bone).bind_local);
        }
        std::vector<glm::mat4> skin;
        skeleton.compute_skin_matrices(bind, skin);
        REQUIRE(skin.size() == bind.size());
        for (const glm::mat4& matrix : skin) {
            REQUIRE_MSG(matrix_delta(matrix, glm::mat4{1.0f}) < 0.002f,
                        "bind pose produces identity skin matrices", name);
        }
        std::printf("      %s: %zu vertices, %d bones\n", name,
                    mesh.vertices.size(), skeleton.bone_count());
    }
    apricot_test::pass("all supplied pack rigs satisfy the skinned format");
}

void animation_sampling_is_continuous_and_root_motion_free() {
    const std::string root =
        "models/characters/psx_pack/player_male_01/skin";
    SkinnedEmesh mesh;
    Skeleton skeleton;
    Animation walk;
    REQUIRE(read_skinned_emesh(asset_path(root + ".emesh"), mesh));
    REQUIRE(skeleton.load(asset_path(root + ".eskel")));
    REQUIRE(walk.load(asset_path(
        "models/characters/psx_pack/animations/walk.eanim"), skeleton));
    REQUIRE(walk.duration() > 0.9f && walk.duration() < 1.1f);

    std::vector<glm::mat4> pose_a;
    std::vector<glm::mat4> pose_b;
    std::vector<glm::mat4> skin_a;
    std::vector<glm::mat4> skin_b;
    walk.sample(0.3100f, skeleton, pose_a);
    walk.sample(0.3101f, skeleton, pose_b);
    strip_root_motion_xz(skeleton, pose_a);
    strip_root_motion_xz(skeleton, pose_b);
    skeleton.compute_skin_matrices(pose_a, skin_a);
    skeleton.compute_skin_matrices(pose_b, skin_b);
    REQUIRE(skin_a.size() == skin_b.size());

    std::vector<glm::vec4> real;
    std::vector<glm::vec4> dual;
    std::vector<glm::vec4> real_b;
    std::vector<glm::vec4> dual_b;
    skin_matrices_to_dual_quaternions(skin_a, real, dual);
    skin_matrices_to_dual_quaternions(skin_b, real_b, dual_b);

    const float scale = 1.76f / mesh.bounds.size().y;
    float largest_step_m = 0.0f;
    for (const EmeshSkinnedVertex& vertex : mesh.vertices) {
        const glm::vec3 a = skinned_vertex_position(vertex, real, dual);
        const glm::vec3 b = skinned_vertex_position(vertex, real_b, dual_b);
        largest_step_m = std::max(largest_step_m, glm::length(b - a) * scale);
    }
    REQUIRE(largest_step_m > 0.0f);
    REQUIRE(largest_step_m < 0.002f);

    for (int bone = 0; bone < skeleton.bone_count(); ++bone) {
        if (skeleton.bone(bone).parent >= 0) continue;
        const glm::vec3 bind_root{skeleton.bone(bone).bind_local[3]};
        const glm::vec3 sampled_root{pose_a[static_cast<std::size_t>(bone)][3]};
        REQUIRE_NEAR(sampled_root.x, bind_root.x, 1e-5f);
        REQUIRE_NEAR(sampled_root.z, bind_root.z, 1e-5f);
    }

    REQUIRE(real.size() == static_cast<std::size_t>(skeleton.bone_count()));
    REQUIRE(dual.size() == real.size());
    for (std::size_t i = 0; i < real.size(); ++i) {
        REQUIRE(std::isfinite(glm::length(real[i])));
        REQUIRE_NEAR(glm::length(real[i]), 1.0f, 2e-4f);
        REQUIRE(std::isfinite(glm::length(dual[i])));
    }

    const float plant = locomotion_plant_offset(
        mesh, skeleton, walk, scale);
    REQUIRE(std::isfinite(plant));
    REQUIRE(plant >= 0.0f);
    REQUIRE(plant < 0.20f);
    std::printf("      max 0.1 ms vertex delta %.4f mm, plant lift %.4f m\n",
                static_cast<double>(largest_step_m * 1000.0f),
                static_cast<double>(plant));
    apricot_test::pass(
        "walk tracks interpolate continuously with horizontal root motion stripped");
}

}  // namespace

int main() {
    std::printf("skeletal_animation_tests\n");
    if (!std::filesystem::is_regular_file(asset_path(
            "models/characters/psx_pack/player_male_01/skin.emesh"))) {
        std::printf("SKIP skeletal_animation_tests (private character assets "
                    "not staged)\n");
        return 0;
    }
    supplied_rigs_match_the_runtime_contract();
    animation_sampling_is_continuous_and_root_motion_free();
    return apricot_test::done("skeletal_animation_tests");
}
