#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "city/construction_neighbor_materials.h"
#include "city/construction_site.h"
#include "city/neighborhood_towers.h"
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

bool separated(const city::StartSite& a, const city::StartSite& b,
               float margin) {
    const glm::vec2 delta{b.origin.x - a.origin.x, b.origin.z - a.origin.z};
    const glm::vec2 local{a.cos_yaw * delta.x - a.sin_yaw * delta.y,
                          a.sin_yaw * delta.x + a.cos_yaw * delta.y};
    return std::fabs(local.x) >
               (a.lot_width_m + b.lot_width_m) * 0.5f + margin ||
           std::fabs(local.y) >
               (a.lot_depth_m + b.lot_depth_m) * 0.5f + margin;
}

void parcel_preserves_the_east_side() {
    const auto& site = city::kConstructionNeighborMaterialsSite;
    for (float x : {-site.lot_width_m * 0.5f, site.lot_width_m * 0.5f}) {
        for (float z : {-site.lot_depth_m * 0.5f, site.lot_depth_m * 0.5f}) {
            const glm::vec2 p = world(site, {x, z});
            REQUIRE_MSG(road_clearance(p) > 1.5f,
                        "materials depot lot corner too close to road",
                        site.name);
            REQUIRE_NEAR(mesh_height_at(city::kMapSeed, p.x, p.y),
                         site.ground_m, 0.02f);
        }
    }
    REQUIRE(separated(site, city::kConstructionSite.site, 2.0f));
    for (const auto& tower : city::kNeighborhoodTowers)
        REQUIRE(separated(site, tower.site, 2.0f));
    apricot_test::pass(
        "materials depot parcel clears the yard, tower lots, roads and terrain");
}

void depot_has_loading_storage_parking_and_grounded_collision() {
    const auto parts = city::bake_construction_neighbor_materials();
    REQUIRE(city::valid_start_parts(parts.data(), parts.size()));
    REQUIRE(parts.size() >= 28u && parts.size() < 64u);

    std::size_t depot_walls = 0;
    std::size_t racks = 0;
    std::size_t silos = 0;
    std::size_t parking_stops = 0;
    bool loading_apron = false;
    TerrainCollider collider(city::kMapSeed);
    const auto& site = city::kConstructionNeighborMaterialsSite;
    const float yaw = std::atan2(site.sin_yaw, site.cos_yaw);
    for (const auto& part : parts) {
        depot_walls += std::strstr(part.name, "materials depot ") != nullptr &&
                       std::strstr(part.name, "wall") != nullptr;
        racks += std::strstr(part.name, "materials rack") != nullptr;
        silos += std::strstr(part.name, "materials aggregate silo") != nullptr;
        parking_stops += std::strcmp(part.name, "materials parking stop") == 0;
        loading_apron |= std::strcmp(part.name, "materials loading apron") == 0;

        const glm::vec2 p = world(site, {part.centre.x, part.centre.z});
        if (part.solid) {
            collider.add_static_oriented_box(
                {p.x, site.ground_m + part.bottom_m + part.height_m * 0.5f,
                 p.y},
                {part.width_m * 0.5f, part.height_m * 0.5f,
                 part.depth_m * 0.5f},
                yaw);
        }
        if (std::strcmp(part.name, "materials depot lot") == 0 ||
            std::strcmp(part.name, "materials loading apron") == 0 ||
            std::strcmp(part.name, "materials parking strip") == 0) {
            collider.add_static_ground_rect(
                p, site.ground_m + part.bottom_m + part.height_m,
                {part.width_m * 0.5f, part.depth_m * 0.5f}, yaw, Surface::Rock);
        }
    }
    REQUIRE(depot_walls == 5u);
    REQUIRE(racks >= 4u);
    REQUIRE(silos == 2u);
    REQUIRE(parking_stops == 3u);
    REQUIRE(loading_apron);

    // A worker can cross the public apron and reach the loading edge without
    // being forced through a solid rack, dock, or parking stop.
    for (float z = -11.5f; z <= -2.0f; z += 0.1f) {
        const glm::vec2 p = world(site, {6.0f, z});
        const auto support = collider.probe_down(
            {p.x, site.ground_m + 2.0f, p.y}, 4.0f);
        REQUIRE(support.hit);
        REQUIRE(character_position_clear(collider, support.point,
                                         CharacterTuning{}));
    }
    std::printf("  %s: %zu pieces, %zu walls, %zu rack pieces, %zu parking stops\n",
                site.name, parts.size(), depot_walls, racks, parking_stops);
    apricot_test::pass(
        "materials depot has a small supplier shell, loading apron, racks and parking");
}

}  // namespace

int main() {
    parcel_preserves_the_east_side();
    depot_has_loading_storage_parking_and_grounded_collision();
    return apricot_test::done("construction_neighbor_materials_tests");
}
