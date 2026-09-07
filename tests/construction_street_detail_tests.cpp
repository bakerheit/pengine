#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "city/construction_site.h"
#include "city/construction_street_detail.h"
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

float road_clearance(glm::vec2 point) {
    float clearance = 1e9f;
    for (const auto& road : city::kRoads) {
        for (int i = 1; i < road.count; ++i) {
            const glm::vec2 a{road.path[i - 1].x, road.path[i - 1].z};
            const glm::vec2 b{road.path[i].x, road.path[i].z};
            const glm::vec2 delta = b - a;
            const float t = std::clamp(glm::dot(point - a, delta) /
                                           glm::dot(delta, delta),
                                       0.0f, 1.0f);
            const float half = road.width_m > 0.0f
                                   ? road.width_m * 0.5f
                                   : city::road_width_m(road.cls) * 0.5f;
            clearance = std::min(clearance,
                                 glm::length(point - a - delta * t) - half -
                                     city::kWalkWidthM);
        }
    }
    return clearance;
}

void detail_strip_is_adjacent_and_clear() {
    const auto& detail = city::kConstructionStreetDetailSite;
    const auto& yard = city::kConstructionSite.site;
    const glm::vec2 local_delta{detail.lot_centre.x - yard.lot_centre.x,
                                detail.lot_centre.z - yard.lot_centre.z};
    REQUIRE_NEAR(local_delta.x, 31.0f, 0.001f);
    REQUIRE_NEAR(local_delta.y, 0.0f, 0.001f);
    REQUIRE(detail.lot_centre.x - detail.lot_width_m * 0.5f <
            yard.lot_width_m * 0.5f + 2.0f);
    REQUIRE(detail.lot_centre.x - detail.lot_width_m * 0.5f >
            yard.lot_width_m * 0.5f - 1.0f);

    for (float x : {-detail.lot_width_m * 0.5f,
                    detail.lot_width_m * 0.5f}) {
        for (float z : {-detail.lot_depth_m * 0.5f,
                        detail.lot_depth_m * 0.5f}) {
            const glm::vec2 point = world(
                detail, {detail.lot_centre.x + x, detail.lot_centre.z + z});
            REQUIRE_MSG(road_clearance(point) > 1.5f,
                        "street detail lot corner too close to road",
                        detail.name);
            REQUIRE_NEAR(mesh_height_at(city::kMapSeed, point.x, point.y),
                         detail.ground_m, 0.02f);
        }
    }
    apricot_test::pass("detail strip sits beside the yard and clears the east street");
}

void detail_parts_are_valid_and_grounded() {
    const auto parts = city::bake_construction_street_detail();
    REQUIRE(city::valid_start_parts(parts.data(), parts.size()));
    REQUIRE(parts.size() >= 30u && parts.size() <= 48u);

    std::size_t gate_markers = 0;
    std::size_t sign_boards = 0;
    std::size_t parking = 0;
    std::size_t lights = 0;
    std::size_t barriers = 0;
    std::size_t cabinets = 0;
    TerrainCollider collider(city::kMapSeed);
    const auto& site = city::kConstructionStreetDetailSite;
    const float yaw = std::atan2(site.sin_yaw, site.cos_yaw);
    for (const auto& part : parts) {
        gate_markers += std::strstr(part.name, "truck gate marker") != nullptr;
        sign_boards += std::strstr(part.name, "wayfinding board") != nullptr;
        parking += std::strcmp(part.name, "construction detail worker parking pad") == 0;
        lights += std::strstr(part.name, "detail light") != nullptr;
        barriers += std::strstr(part.name, "street barrier") != nullptr;
        cabinets += std::strstr(part.name, "utility cabinet") != nullptr;

        const glm::vec2 point =
            world(site, {part.centre.x, part.centre.z});
        if (part.solid) {
            collider.add_static_oriented_box(
                {point.x, site.ground_m + part.bottom_m + part.height_m * 0.5f,
                 point.y},
                {part.width_m * 0.5f, part.height_m * 0.5f,
                 part.depth_m * 0.5f},
                yaw);
        }
        if (std::strcmp(part.name, "construction detail service apron") == 0 ||
            std::strcmp(part.name, "construction detail worker parking pad") == 0 ||
            std::strcmp(part.name, "construction detail truck turn pad") == 0) {
            collider.add_static_ground_rect(
                point, site.ground_m + part.bottom_m + part.height_m,
                {part.width_m * 0.5f, part.depth_m * 0.5f}, yaw, Surface::Rock);
        }
    }

    REQUIRE(gate_markers == 4u);
    REQUIRE(sign_boards == 6u);
    REQUIRE(parking == 1u);
    REQUIRE(lights == 4u);
    REQUIRE(barriers == 6u);
    REQUIRE(cabinets == 2u);

    // Probe the full service strip and make sure the authored pads support a
    // person. The solid props stay beside the route rather than in it.
    for (float z = -17.0f; z <= 17.0f; z += 0.5f) {
        const glm::vec2 point = world(site, {31.0f, z});
        const auto support = collider.probe_down(
            {point.x, site.ground_m + 2.0f, point.y}, 4.0f);
        REQUIRE_MSG(support.hit, "detail strip has no support", site.name);
        REQUIRE(character_position_clear(collider, support.point,
                                         CharacterTuning{}));
    }
    std::printf("  %s: %zu pieces, %zu gate markers, %zu barriers, %zu cabinets\n",
                site.name, parts.size(), gate_markers, barriers, cabinets);
    apricot_test::pass("street detail has valid props, pads, and grounded worker access");
}

}  // namespace

int main() {
    detail_strip_is_adjacent_and_clear();
    detail_parts_are_valid_and_grounded();
    return apricot_test::done("construction_street_detail_tests");
}
