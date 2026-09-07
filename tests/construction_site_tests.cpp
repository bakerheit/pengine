#include <cmath>
#include <cstdio>
#include <cstring>
#include <algorithm>

#include "city/construction_site.h"
#include "city/neighborhood_towers.h"
#include "city/neighborhood_bar.h"
#include "city/neighborhood_shops.h"
#include "city/tacomaco.h"
#include "city/roads.h"
#include "game/character.h"
#include "terrain/chunk.h"
#include "test_assert.h"

using namespace apricot;

namespace {

glm::vec2 world(const city::StartSite& site, glm::vec2 local) {
    return {site.origin.x + site.cos_yaw * local.x + site.sin_yaw * local.y,
            site.origin.z - site.sin_yaw * local.x + site.cos_yaw * local.y};
}

float road_clearance(glm::vec2 p) {
    float clearance = 1e9f;
    for (const auto& road : city::kRoads) {
        for (int i = 1; i < road.count; ++i) {
            const glm::vec2 a{road.path[i - 1].x, road.path[i - 1].z};
            const glm::vec2 b{road.path[i].x, road.path[i].z};
            const glm::vec2 delta = b - a;
            const float t = std::clamp(glm::dot(p - a, delta) /
                                           glm::dot(delta, delta),
                                       0.0f, 1.0f);
            const float half = road.width_m > 0.0f
                                   ? road.width_m * 0.5f
                                   : city::road_width_m(road.cls) * 0.5f;
            clearance = std::min(clearance,
                                 glm::length(p - a - delta * t) - half -
                                     city::kWalkWidthM);
        }
    }
    return clearance;
}

void site_fits_the_east_side() {
    const auto& site = city::kConstructionSite.site;
    const float half_width = site.lot_width_m * 0.5f;
    const float half_depth = site.lot_depth_m * 0.5f;
    for (float x : {-half_width, half_width}) {
        for (float z : {-half_depth, half_depth}) {
            const glm::vec2 p = world(site, {x, z});
            REQUIRE_MSG(road_clearance(p) > 1.5f,
                        "construction lot corner too close to road", site.name);
            REQUIRE_NEAR(mesh_height_at(city::kMapSeed, p.x, p.y),
                         site.ground_m, 0.02f);
        }
    }

    const city::StartSite* neighbors[] = {
        &city::kGasStationSite, &city::kMotelSite, &city::kApartmentSite,
        &city::kFastFoodSite, &city::kTacomacoSite, &city::kCarWashSite,
        &city::kBankSite, &city::kAutoRepairSite, &city::kLaundromatSite,
        &city::kNeighborhoodBarSite,
    };
    for (const auto* neighbor : neighbors) {
        const glm::vec2 delta{neighbor->origin.x - site.origin.x,
                              neighbor->origin.z - site.origin.z};
        const glm::vec2 local{site.cos_yaw * delta.x - site.sin_yaw * delta.y,
                              site.sin_yaw * delta.x + site.cos_yaw * delta.y};
        REQUIRE(std::fabs(local.x) >
                    (site.lot_width_m + neighbor->lot_width_m) * 0.5f + 2.0f ||
                std::fabs(local.y) >
                    (site.lot_depth_m + neighbor->lot_depth_m) * 0.5f + 2.0f);
    }
    for (const auto& tower : city::kNeighborhoodTowers) {
        const glm::vec2 delta{tower.site.origin.x - site.origin.x,
                              tower.site.origin.z - site.origin.z};
        const glm::vec2 local{site.cos_yaw * delta.x - site.sin_yaw * delta.y,
                              site.sin_yaw * delta.x + site.cos_yaw * delta.y};
        REQUIRE(std::fabs(local.x) >
                    (site.lot_width_m + tower.site.lot_width_m) * 0.5f + 2.0f ||
                std::fabs(local.y) >
                    (site.lot_depth_m + tower.site.lot_depth_m) * 0.5f + 2.0f);
    }
    apricot_test::pass("construction yard fits the east-side block without eating streets or tower lots");
}

void frame_and_props_are_authored_and_walkable() {
    const auto parts = city::bake_construction_site();
    REQUIRE(city::valid_start_parts(parts.data(), parts.size()));
    REQUIRE(parts.size() > 100u && parts.size() < 220u);
    REQUIRE_NEAR(static_cast<float>(city::kConstructionSite.built_floor_count) /
                     static_cast<float>(city::kConstructionSite.planned_floor_count),
                 0.70f, 0.001f);

    std::size_t slabs = 0;
    std::size_t fence = 0;
    std::size_t crane = 0;
    std::size_t props = 0;
    std::size_t floor_detail = 0;
    std::size_t formwork = 0;
    std::size_t utility = 0;
    std::size_t material_stacks = 0;
    TerrainCollider collider(city::kMapSeed);
    const auto& site = city::kConstructionSite.site;
    const float yaw = std::atan2(site.sin_yaw, site.cos_yaw);
    for (const auto& part : parts) {
        slabs += std::strcmp(part.name, "construction frame floor slab") == 0;
        fence += std::strstr(part.name, "construction fence mesh") != nullptr ||
                 std::strstr(part.name, "construction top safety mesh") != nullptr;
        crane += std::strstr(part.name, "construction crane") != nullptr;
        floor_detail += std::strstr(part.name, "construction unfinished floor") != nullptr;
        formwork += std::strstr(part.name, "construction plywood formwork") != nullptr;
        utility += std::strstr(part.name, "construction utility") != nullptr;
        material_stacks += std::strstr(part.name, "construction steel beam stack") != nullptr ||
                           std::strstr(part.name, "construction timber stack") != nullptr;
        props += std::strstr(part.name, "construction ") != nullptr &&
                 std::strstr(part.name, "frame ") == nullptr &&
                 std::strstr(part.name, "fence ") == nullptr &&
                 std::strstr(part.name, "top safety") == nullptr &&
                 std::strstr(part.name, "site lot") == nullptr &&
                 std::strstr(part.name, "haul pad") == nullptr &&
                 std::strstr(part.name, "sidewalk") == nullptr;
        const glm::vec2 p = world(site, {part.centre.x, part.centre.z});
        if (part.solid) {
            collider.add_static_oriented_box(
                {p.x, site.ground_m + part.bottom_m + part.height_m * 0.5f, p.y},
                {part.width_m * 0.5f, part.height_m * 0.5f,
                 part.depth_m * 0.5f}, yaw);
        }
        if (std::strcmp(part.name, "construction site lot") == 0 ||
            std::strcmp(part.name, "construction haul pad") == 0 ||
            std::strcmp(part.name, "construction sidewalk") == 0) {
            collider.add_static_ground_rect(
                p, site.ground_m + part.bottom_m + part.height_m,
                {part.width_m * 0.5f, part.depth_m * 0.5f}, yaw, Surface::Rock);
        }
    }
    REQUIRE(slabs == static_cast<std::size_t>(city::kConstructionSite.built_floor_count));
    REQUIRE(fence >= 7u);
    REQUIRE(crane >= 7u);
    REQUIRE(props >= 10u);
    REQUIRE(floor_detail >= 20u);
    REQUIRE(formwork >= 3u);
    REQUIRE(utility >= 4u);
    REQUIRE(material_stacks >= 3u);

    // Real character support through the open front gate and into the yard.
    for (float z = -22.0f; z <= -11.0f; z += 0.1f) {
        const glm::vec2 p = world(site, {0.0f, z});
        const auto support = collider.probe_down(
            {p.x, site.ground_m + 2.0f, p.y}, 4.0f);
        REQUIRE(support.hit);
        REQUIRE(character_position_clear(collider, support.point,
                                         CharacterTuning{}));
    }
    std::printf("  %s: %zu pieces, %zu floor slabs, %zu floor-detail pieces, "
                "%zu crane pieces, %zu fence pieces\n",
                site.name, parts.size(), slabs, floor_detail, crane, fence);
    apricot_test::pass("70% frame, crane and construction props have grounded collision and an open walk-up gate");
}

}  // namespace

int main() {
    site_fits_the_east_side();
    frame_and_props_are_authored_and_walkable();
    return apricot_test::done("construction_site_tests");
}
