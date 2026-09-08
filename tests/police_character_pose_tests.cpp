#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "app/traffic_visual_layout.h"
#include "app/vehicle_transition_pose.h"
#include "core/asset_root.h"
#include "game/character.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr PlayerCarId kCruiser = PlayerCarId::MunicipalCruiser91C;

void check_uniform_pose(const char* name, const Transform& body,
                        float ground_y) {
    const std::string root = std::string{"models/characters/psx_pack/"} +
                             name + "/";
    SkinnedEmesh mesh;
    Skeleton skeleton;
    REQUIRE(read_skinned_emesh(asset_path(root + "skin.emesh"), mesh));
    REQUIRE(skeleton.load(asset_path(root + "skin.eskel")));
    REQUIRE(skeleton.accepts(mesh));
    REQUIRE(std::filesystem::is_regular_file(asset_path(root + "body.png")));
    std::array<Animation, 3> clips;
    const char* clip_names[]{"idle", "walk", "sprint"};
    for (std::size_t i = 0; i < clips.size(); ++i) {
        REQUIRE(clips[i].load(asset_path(std::string{
            "models/characters/psx_pack/animations/"} + clip_names[i] +
            ".eanim"), skeleton));
        REQUIRE(clips[i].unresolved_channels() == 0);
        std::vector<glm::mat4> local;
        std::vector<glm::mat4> skin;
        for (int frame = 0; frame < 60; ++frame) {
            clips[i].sample(clips[i].duration() * static_cast<float>(frame) /
                                60.0f, skeleton, local);
            strip_root_motion_xz(skeleton, local);
            skeleton.compute_skin_matrices(local, skin);
            for (const auto& matrix : skin)
                REQUIRE(driver_pose_detail::finite(matrix));
        }
    }

    VehicleDriverPose seated;
    REQUIRE(make_vehicle_driver_pose(kCruiser, skeleton, mesh.bounds, body,
                                     seated));
    REQUIRE_NEAR(seated.world.scale.y * mesh.bounds.size().y, 1.76f, 1e-5f);
    const auto& layout = vehicle_driver_layout(kCruiser);
    const auto side = body.rotate({1, 0, 0});
    auto door_pos = body.transform_point({layout.approach_x, 0, layout.hip.z}) +
                    side * 0.8f;
    door_pos.y = ground_y;
    float largest_step = 0.0f;
    for (const auto direction : {VehicleTransitionDirection::Exit,
                                 VehicleTransitionDirection::Enter}) {
        VehicleDriverPose standing;
        clips[0].sample(0.0f, skeleton, standing.local);
        strip_root_motion_xz(skeleton, standing.local);
        const auto facing = direction == VehicleTransitionDirection::Exit ?
                            side : -side;
        Transform root_transform;
        root_transform.position = door_pos;
        root_transform.rotation = character_root_rotation(
            std::atan2(facing.x, -facing.z));
        Transform model_transform;
        model_transform.scale = seated.world.scale;
        model_transform.rotation = glm::angleAxis(glm::pi<float>(),
                                                   glm::vec3{0, 1, 0});
        model_transform.position = {
            -mesh.bounds.center().x * model_transform.scale.x,
            -mesh.bounds.min.y * model_transform.scale.y,
            -mesh.bounds.center().z * model_transform.scale.z,
        };
        standing.world = root_transform * model_transform;
        driver_pose_detail::globals(skeleton, standing);
        skin_matrices_to_dual_quaternions(standing.skin, standing.dual_real,
                                          standing.dual_part);
        std::vector<glm::vec3> previous;
        for (uint32_t tick = 0; tick <= kVehicleTransitionTicks; ++tick) {
            VehicleDriverPose pose;
            REQUIRE(make_vehicle_transition_pose(kCruiser, skeleton,
                mesh.bounds, body, standing.world, standing.local,
                sample_vehicle_transition({direction, tick}), pose));
            REQUIRE_NEAR(pose.world.scale.y * mesh.bounds.size().y, 1.76f,
                         1e-5f);
            for (const auto& matrix : pose.skin)
                REQUIRE(driver_pose_detail::finite(matrix));
            const bool seated_endpoint =
                (direction == VehicleTransitionDirection::Enter &&
                 tick == kVehicleTransitionTicks) ||
                (direction == VehicleTransitionDirection::Exit && tick == 0);
            const auto& endpoint = seated_endpoint ? seated : standing;
            std::vector<glm::vec3> vertices;
            vertices.reserve(mesh.vertices.size());
            for (const auto& vertex : mesh.vertices) {
                const auto position = pose.world.transform_point(
                    skinned_vertex_position(vertex, pose.dual_real,
                                            pose.dual_part));
                REQUIRE(std::isfinite(position.x) &&
                        std::isfinite(position.y) &&
                        std::isfinite(position.z));
                if (!previous.empty()) largest_step = std::max(largest_step,
                    glm::distance(position, previous[vertices.size()]));
                if (tick == 0 || tick == kVehicleTransitionTicks) {
                    const auto expected = endpoint.world.transform_point(
                        skinned_vertex_position(vertex, endpoint.dual_real,
                                                endpoint.dual_part));
                    REQUIRE(glm::distance(position, expected) < 0.001f);
                }
                vertices.push_back(position);
            }
            previous = std::move(vertices);
        }
    }
    std::printf("      %s: %d bones, 1.76 m across all phases, "
                "max vertex step %.4f m\n", name, skeleton.bone_count(),
                static_cast<double>(largest_step));
    REQUIRE(largest_step < 0.08f);
}

}  // namespace

int main() {
    const auto police_asset = asset_path(
        "models/characters/psx_pack/police_male_17/skin.emesh");
    if (!std::filesystem::is_regular_file(police_asset)) {
        std::printf("SKIP police_character_pose_tests: stage private uniforms "
                    "with tools/stage_police_character_assets.py\n");
        return 0;
    }
    StaticEmesh cruiser;
    REQUIRE(read_static_emesh(asset_path(
        "models/vehicles/municipal_cruiser_91c/body.emesh"), cruiser));
    const auto fit = make_traffic_visual_layout(cruiser.bounds,
                                                .42f, .94f, 1.63f, 1.53f);
    Transform chassis;
    chassis.position = {12, 3, -7};
    chassis.set_euler_deg(0, 73, 0);
    const Transform body = chassis * fit.body;
    for (const char* name : {"police_male_17", "police_male_19"})
        check_uniform_pose(name, body, chassis.position.y);
    apricot_test::pass("supplied police uniforms bind every locomotion clip "
                       "and keep continuous cruiser entry/exit and height");
    return apricot_test::done("police_character_pose_tests");
}
