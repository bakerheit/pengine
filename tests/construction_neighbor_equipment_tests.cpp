#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "city/construction_neighbor_equipment.h"
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

void parcel_support_and_road_clearance() {
    const auto& site = city::kConstructionNeighborEquipment.site;
    REQUIRE(std::fabs(site.origin.x - city::construction_grid_point(138, -279).x) <
            0.001f);
    REQUIRE(std::fabs(site.origin.z - city::construction_grid_point(138, -279).z) <
            0.001f);

    for (float x : {-site.lot_width_m * 0.5f, site.lot_width_m * 0.5f}) {
        for (float z : {-site.lot_depth_m * 0.5f, site.lot_depth_m * 0.5f}) {
            const glm::vec2 p = world(site, {x, z});
            REQUIRE_MSG(road_clearance(p) > 1.5f,
                        "equipment parcel corner too close to road", site.name);
            REQUIRE_NEAR(mesh_height_at(city::kMapSeed, p.x, p.y),
                         site.ground_m, 0.02f);
        }
    }
    apricot_test::pass("equipment parcel at grid 138,-279 has road and terrain support");
}

void building_and_yard_are_separated() {
    const auto& site = city::kConstructionNeighborEquipment.site;
    const auto separated = [&](const city::StartSite& other, float margin) {
        const glm::vec2 delta{other.origin.x - site.origin.x,
                              other.origin.z - site.origin.z};
        const glm::vec2 local{site.cos_yaw * delta.x - site.sin_yaw * delta.y,
                              site.sin_yaw * delta.x + site.cos_yaw * delta.y};
        return std::fabs(local.x) >
                   (site.lot_width_m + other.lot_width_m) * 0.5f + margin ||
               std::fabs(local.y) >
                   (site.lot_depth_m + other.lot_depth_m) * 0.5f + margin;
    };
    REQUIRE(separated(city::kConstructionSite.site, 2.0f));
    for (const auto& tower : city::kNeighborhoodTowers)
        REQUIRE_MSG(separated(tower.site, 2.0f),
                    "equipment yard overlaps a tower parcel", tower.site.name);
    apricot_test::pass("equipment yard stays clear of the construction site and all tower lots");
}

void baked_shell_apron_storage_and_props_are_valid() {
    const auto parts = city::bake_construction_neighbor_equipment();
    REQUIRE(city::valid_start_parts(parts.data(), parts.size()));
    REQUIRE(parts.size() >= 35u && parts.size() <= 80u);

    std::size_t office = 0;
    std::size_t garage = 0;
    std::size_t apron = 0;
    std::size_t fence = 0;
    std::size_t props = 0;
    TerrainCollider collider(city::kMapSeed);
    const auto& site = city::kConstructionNeighborEquipment.site;
    const float yaw = std::atan2(site.sin_yaw, site.cos_yaw);
    for (const auto& part : parts) {
        office += std::strstr(part.name, "equipment office") != nullptr;
        garage += std::strstr(part.name, "equipment garage") != nullptr;
        apron += std::strstr(part.name, "service apron") != nullptr ||
                 std::strstr(part.name, "entrance walk") != nullptr;
        fence += std::strstr(part.name, "storage fence") != nullptr;
        props += std::strstr(part.name, "equipment ") != nullptr &&
                 std::strstr(part.name, "office") == nullptr &&
                 std::strstr(part.name, "garage") == nullptr &&
                 std::strstr(part.name, "lot") == nullptr &&
                 std::strstr(part.name, "apron") == nullptr &&
                 std::strstr(part.name, "walk") == nullptr &&
                 std::strstr(part.name, "fence") == nullptr;

        const glm::vec2 p = world(site, {part.centre.x, part.centre.z});
        if (part.solid) {
            collider.add_static_oriented_box(
                {p.x, site.ground_m + part.bottom_m + part.height_m * 0.5f, p.y},
                {part.width_m * 0.5f, part.height_m * 0.5f,
                 part.depth_m * 0.5f},
                yaw);
        }
        if (std::strstr(part.name, "lot") != nullptr ||
            std::strstr(part.name, "apron") != nullptr ||
            std::strstr(part.name, "walk") != nullptr ||
            std::strstr(part.name, "storage pad") != nullptr) {
            collider.add_static_ground_rect(
                p, site.ground_m + part.bottom_m + part.height_m,
                {part.width_m * 0.5f, part.depth_m * 0.5f}, yaw, Surface::Rock);
        }
    }

    REQUIRE(office >= 10u);
    REQUIRE(garage >= 7u);
    REQUIRE(apron == 2u);
    REQUIRE(fence >= 3u);
    REQUIRE(props >= 10u);

    // The walk from the public sidewalk to the office door must remain usable.
    for (float z = -15.0f; z <= -4.5f; z += 0.1f) {
        const glm::vec2 p = world(site, {8.0f, z});
        const auto support = collider.probe_down(
            {p.x, site.ground_m + 2.0f, p.y}, 4.0f);
        REQUIRE(support.hit);
        REQUIRE(character_position_clear(collider, support.point,
                                         CharacterTuning{}));
    }
    std::printf("  %s: %zu pieces, %zu office, %zu garage, %zu storage-fence, %zu props\n",
                site.name, parts.size(), office, garage, fence, props);
    apricot_test::pass("equipment rental shell, service apron, fenced storage and props are grounded");
}

}  // namespace

int main() {
    parcel_support_and_road_clearance();
    building_and_yard_are_separated();
    baked_shell_apron_storage_and_props_are_valid();
    return apricot_test::done("construction_neighbor_equipment_tests");
}
