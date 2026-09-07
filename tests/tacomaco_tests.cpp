#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "city/building_access.h"
#include "city/quickbite_doors.h"
#include "city/spines.h"
#include "city/tacomaco.h"
#include "game/character.h"
#include "test_assert.h"

using namespace apricot;

namespace {
constexpr float kDt = 1.0f / 120.0f;

glm::vec3 world_point(const city::StartSite& site, float y, glm::vec2 local) {
    return {site.origin.x + site.cos_yaw * local.x + site.sin_yaw * local.y,
            site.ground_m + y,
            site.origin.z - site.sin_yaw * local.x + site.cos_yaw * local.y};
}

glm::vec2 local_point(const city::StartSite& site, glm::vec3 world) {
    const glm::vec2 delta{world.x - site.origin.x, world.z - site.origin.z};
    return {site.cos_yaw * delta.x - site.sin_yaw * delta.y,
            site.sin_yaw * delta.x + site.cos_yaw * delta.y};
}

TerrainCollider tacomaco_collider() {
    TerrainCollider collider{city::kMapSeed};
    const auto& site = city::kTacomacoSite;
    const float site_yaw = std::atan2(site.sin_yaw, site.cos_yaw);
    for (const auto& part : city::bake_building(city::kTacomacoPlan)) {
        const glm::vec3 centre = world_point(
            site, part.bottom_m + part.height_m * 0.5f,
            {part.centre.x, part.centre.z});
        const float yaw = site_yaw + glm::radians(part.yaw_deg);
        if (part.solid) {
            REQUIRE(part.pitch_deg == 0.0f && part.roll_deg == 0.0f);
            collider.add_static_oriented_box(
                centre, {part.width_m * 0.5f, part.height_m * 0.5f,
                         part.depth_m * 0.5f}, yaw);
        }
        const bool lot = std::strcmp(part.name, "restaurant lot") == 0;
        const bool walk = std::strcmp(part.name, "quickbite front pedestrian walk") == 0 ||
                          std::strcmp(part.name, "quickbite interior floor") == 0;
        if (lot || walk) {
            collider.add_static_ground_rect(
                {centre.x, centre.z},
                site.ground_m + part.bottom_m + part.height_m,
                {part.width_m * 0.5f, part.depth_m * 0.5f}, yaw,
                Surface::Rock);
        }
    }
    return collider;
}

PlayerCharacterState advance_toward(PlayerCharacterState state,
                                    const TerrainCollider& collider,
                                    glm::vec3 goal, int steps) {
    const CharacterTuning tuning;
    for (int i = 0; i < steps; ++i) {
        const glm::vec2 delta{goal.x - state.position.x,
                              goal.z - state.position.z};
        const float distance = glm::length(delta);
        if (distance < 0.018f) break;
        InputFrame input;
        input.look_dx = std::atan2(delta.x, -delta.y) - state.view_yaw;
        input.throttle = std::min(1.0f,
            distance / (tuning.walk_speed_mps * kDt));
        state = step_character(state, tuning, input, collider, kDt);
        REQUIRE(character_position_clear(collider, state.position, tuning));
    }
    return state;
}

void copied_shell_has_the_same_geometry() {
    REQUIRE(city::valid_building_plan(city::kTacomacoPlan));
    REQUIRE(city::kTacomacoPlan.walls == city::kFastFoodPlan.walls);
    REQUIRE(city::kTacomacoPlan.roofs == city::kFastFoodPlan.roofs);
    REQUIRE(city::kTacomacoPlan.fixtures == city::kFastFoodPlan.fixtures);

    const auto cloggers = city::bake_building(city::kFastFoodPlan);
    const auto tacomaco = city::bake_building(city::kTacomacoPlan);
    REQUIRE(tacomaco.size() == cloggers.size());
    for (std::size_t i = 0; i < tacomaco.size(); ++i) {
        const auto& a = cloggers[i];
        const auto& b = tacomaco[i];
        REQUIRE(std::strcmp(a.name, b.name) == 0);
        REQUIRE_NEAR(a.centre.x, b.centre.x, 1e-5f);
        REQUIRE_NEAR(a.centre.z, b.centre.z, 1e-5f);
        REQUIRE_NEAR(a.bottom_m, b.bottom_m, 1e-5f);
        REQUIRE_NEAR(a.width_m, b.width_m, 1e-5f);
        REQUIRE_NEAR(a.height_m, b.height_m, 1e-5f);
        REQUIRE_NEAR(a.depth_m, b.depth_m, 1e-5f);
        REQUIRE(a.finish == b.finish && a.solid == b.solid);
    }
    REQUIRE_NEAR(city::kTacomacoSite.cos_yaw, city::kFastFoodSite.cos_yaw, 1e-5f);
    REQUIRE_NEAR(city::kTacomacoSite.sin_yaw, city::kFastFoodSite.sin_yaw, 1e-5f);
    REQUIRE(glm::distance(glm::vec2{city::kTacomacoSite.origin.x,
                                    city::kTacomacoSite.origin.z},
                          glm::vec2{city::kFastFoodSite.origin.x,
                                    city::kFastFoodSite.origin.z}) > 170.0f);
    apricot_test::pass("Tacomaco reuses the complete Cloggers shell at a separate site");
}

void parcel_connects_to_the_authored_sidewalk() {
    const auto lots = city::authored_building_access_lots();
    const auto lot = std::find_if(lots.begin(), lots.end(), [](const auto& item) {
        return std::strcmp(item.site.name, "Tacomaco") == 0;
    });
    REQUIRE(lot != lots.end());

    const TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    roads.build(city::map_spines(), {}, ground.sampler());
    const auto ribbons = bake_ribbons(roads, ground.sampler());
    const auto bake = city::bake_building_access(
        roads, ribbons, ground.sampler(), lots);
    const auto result = std::find_if(bake.lots.begin(), bake.lots.end(),
        [](const auto& item) { return std::strcmp(item.name, "restaurant lot") == 0 &&
                                      std::fabs(item.site.origin.z - city::kTacomacoSite.origin.z) < 0.01f; });
    REQUIRE(result != bake.lots.end());
    REQUIRE(result->connected);
    REQUIRE(result->entrance.sidewalk);
    REQUIRE(city::access_clear_of_junctions(
        roads, result->entrance.curb, result->width_m * 0.5f));
    REQUIRE(result->entrance.road_key >> 32 != 151u);
    apricot_test::pass("Tacomaco has a measured driveway and sidewalk connection");
}

void player_reaches_the_copied_restaurant() {
    const auto collider = tacomaco_collider();
    const auto& site = city::kTacomacoSite;
    const auto outside = world_point(site, 0.0f, {-5.0f, -10.0f});
    auto state = spawn_character(collider, outside.x, outside.z);
    state = advance_toward(state, collider,
                           world_point(site, 0.0f, {-5.0f, -3.6f}), 600);
    REQUIRE_NEAR(local_point(site, state.position).y, -3.6f, 0.04f);
    REQUIRE_NEAR(state.position.y - site.ground_m, 0.20f, 1e-4f);
    state = advance_toward(state, collider,
                           world_point(site, 0.0f, {-5.0f, 4.3f}), 800);
    const auto inside = local_point(site, state.position);
    REQUIRE_NEAR(inside.x, -5.0f, 0.04f);
    REQUIRE_NEAR(inside.y, 4.3f, 0.04f);
    REQUIRE_NEAR(state.position.y - site.ground_m, 0.20f, 1e-4f);

    const auto doors = city::quickbite_doors(site);
    REQUIRE(doors.size() == 2u);
    REQUIRE(doors[0].site == &site && doors[1].site == &site);
    const auto leaves = city::quickbite_door_parts(doors[0], 0.0f);
    REQUIRE(leaves.front().solid);
    const auto door_world = world_point(site, leaves.front().bottom_m,
                                        {leaves.front().centre.x, leaves.front().centre.z});
    REQUIRE(glm::distance(glm::vec2{door_world.x, door_world.z},
                          glm::vec2{outside.x, outside.z}) > 1.0f);
    apricot_test::pass("character reaches Tacomaco through the copied real doorway");
}
}

int main() {
    copied_shell_has_the_same_geometry();
    parcel_connects_to_the_authored_sidewalk();
    player_reaches_the_copied_restaurant();
    return apricot_test::done("tacomaco_tests");
}
