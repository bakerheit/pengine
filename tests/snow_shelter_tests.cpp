#include <cmath>
#include <cstring>
#include <limits>

#include "city/precipitation_cover.h"
#include "game/snow_clearance.h"
#include "physics/snow_shelter.h"
#include "road/ribbon.h"
#include "test_assert.h"

using namespace apricot;

namespace {

void exact_yaw_edges_and_vertical_layers() {
    TerrainCollider roofs(1);
    roofs.add_static_oriented_box({0, 5, 0}, {5, 0.2f, 1}, glm::radians(45.0f));
    SnowShelterField field;
    field.build(roofs.static_boxes());
    REQUIRE(field.covered(0, 0, 0));
    REQUIRE(field.covered(0, 3.8f, 0));
    REQUIRE(!field.covered(3.5f, 0, 3.5f));
    REQUIRE(!field.covered(0, 5.2f, 0));
    REQUIRE(!field.covered(0, 9, 0));
    REQUIRE(!field.covered(0, std::numeric_limits<float>::quiet_NaN(), 0));
    // A second roof shelters the lower roof top but leaves its own top open.
    roofs.add_static_box({{-2, 12, -2}, {2, 12.3f, 2}});
    field.build(roofs.static_boxes());
    REQUIRE(field.covered(0, 5.2f, 0));
    REQUIRE(!field.covered(0, 12.3f, 0));
    apricot_test::pass("exact rotated cover and stacked roofs preserve uncovered corners and topmost rooftops");
}

void index_crosses_cells_and_rebuild_replaces_cover() {
    TerrainCollider roofs(1);
    roofs.add_static_box({{-65, 10, -33}, {65, 10.3f, 33}});
    const auto ignored = roofs.add_kinematic_box({{200, 10, 200}, {220, 10.3f, 220}});
    roofs.set_kinematic_vehicle(ignored, true);
    const auto disabled = roofs.add_kinematic_box({{300, 10, 300}, {320, 10.3f, 320}});
    roofs.set_kinematic_enabled(disabled, false);
    SnowShelterField field;
    field.build(roofs.static_boxes());
    REQUIRE(field.boxes().size() == 1);
    for (float x : {-65.0f, -64.0f, -32.0f, -0.1f, 0.0f, 32.0f, 64.0f, 65.0f})
        for (float z : {-33.0f, -32.0f, 0.0f, 32.0f, 33.0f})
            REQUIRE(field.covered(x, 0, z));
    REQUIRE(!field.covered(65.1f, 0, 0));
    REQUIRE(!field.covered(210, 0, 210));
    REQUIRE(!field.covered(310, 0, 310));
    field.build({});
    REQUIRE(field.boxes().empty());
    REQUIRE(!field.covered(0, 0, 0));
    apricot_test::pass("roof index covers negative and boundary cells and rebuilds without stale or vehicle cover");
}

void all_ground_collision_paths_respect_roofs_without_plows() {
    constexpr float x = 91, z = -36;
    TerrainCollider collider(0xC0FFEEu);
    const float base_y = collider.height(x, z);
    TerrainCollider roofs(1);
    roofs.add_static_box({{x - 2, base_y + 6, z - 2}, {x + 2, base_y + 6.3f, z + 2}});
    SnowShelterField shelter;
    shelter.build(roofs.static_boxes());
    collider.set_snow_shelter(&shelter);
    collider.set_snow_collision_depth(0.7f);
    collider.set_snow_clearance(nullptr, 0.8f);
    REQUIRE_NEAR(collider.height(x, z), base_y, 1e-5);
    REQUIRE(collider.snow_depth_at(x, base_y, z) == 0);
    REQUIRE_NEAR(collider.height(x + 3, z),
                 TerrainCollider(0xC0FFEEu).height(x + 3, z) + 0.7f, 1e-5);
    RoadCollision road;
    RoadCollisionTri triangle;
    triangle.geom = {{x - 2, base_y + 1, z - 2}, {x + 2, base_y + 1, z + 2},
                     {x + 2, base_y + 1, z - 2}, {0, 1, 0}};
    triangle.layer = RoadLayer::Carriageway;
    road.triangles.push_back(triangle);
    collider.set_road_collision(road);
    auto hit = collider.probe_down({x + 0.2f, base_y + 4, z}, 5);
    REQUIRE(hit.hit && hit.road);
    REQUIRE_NEAR(hit.point.y, base_y + 1, 1e-5);
    REQUIRE(hit.snow_depth_m == 0);
    collider.add_static_ground_rect({x, z}, base_y + 2, {1, 1}, 0);
    hit = collider.probe_down({x, base_y + 4, z}, 5);
    REQUIRE(hit.hit && !hit.road && !hit.prop);
    REQUIRE_NEAR(hit.point.y, base_y + 2, 1e-5);
    REQUIRE(hit.snow_depth_m == 0);
    // Even a plow's residual dusting must not override a sheltered floor.
    SnowClearanceField clearance;
    REQUIRE(clearance.clear_segment({x - 1, base_y + 2, z},
                                    {x + 1, base_y + 2, z}, 1, 0.8f));
    collider.set_snow_clearance(&clearance, 0.8f);
    REQUIRE(collider.snow_depth_at(x, base_y + 2, z) == 0);
    collider.set_snow_shelter(nullptr);
    collider.set_snow_clearance(nullptr, 0.8f);
    REQUIRE_NEAR(collider.height(x, z), base_y + 0.7f, 1e-5);
    apricot_test::pass("terrain roads and ground slabs suppress physical snow under roofs with or without plows");
}

void actual_cloggers_non_solid_roof_keeps_floor_and_shelves_dry() {
    const auto& site = city::kFastFoodSite;
    const float yaw = std::atan2(site.sin_yaw, site.cos_yaw);
    const auto world = [&](glm::vec3 p) {
        return glm::vec3{site.origin.x + site.cos_yaw * p.x + site.sin_yaw * p.z,
                         site.ground_m + p.y,
                         site.origin.z - site.sin_yaw * p.x + site.cos_yaw * p.z};
    };
    TerrainCollider collider(city::kMapSeed);
    std::vector<StaticBox> roofs;
    std::size_t non_solid_roofs = 0;
    for (const auto& part : city::bake_building(city::kFastFoodPlan)) {
        Transform t;
        t.position = world({part.centre.x, part.bottom_m + part.height_m * 0.5f, part.centre.z});
        t.rotation = glm::angleAxis(yaw, glm::vec3{0, 1, 0}) *
            glm::quat(glm::radians(glm::vec3{part.pitch_deg, part.yaw_deg, part.roll_deg}));
        t.scale = {part.width_m, part.height_m, part.depth_m};
        const auto before = roofs.size();
        city::append_precipitation_cover(part, t, roofs);
        if (!part.solid && roofs.size() > before) ++non_solid_roofs;
        const bool ground = std::strcmp(part.name, "quickbite interior floor") == 0 ||
                            std::strcmp(part.name, "restaurant lot") == 0;
        if (ground) {
            collider.add_static_ground_rect({t.position.x, t.position.z},
                site.ground_m + part.bottom_m + part.height_m,
                {part.width_m * 0.5f, part.depth_m * 0.5f}, yaw + glm::radians(part.yaw_deg));
        } else if (part.solid && part.pitch_deg == 0 && part.roll_deg == 0) {
            collider.add_static_oriented_box(t.position, t.scale * 0.5f,
                                             yaw + glm::radians(part.yaw_deg));
        }
    }
    REQUIRE(non_solid_roofs > 0);
    SnowShelterField shelter;
    shelter.build(roofs);
    collider.set_snow_shelter(&shelter);
    collider.set_snow_clearance(nullptr, 0.8f);
    std::size_t floor_contacts = 0;
    for (float x = -19; x <= 9; x += 2) {
        for (float z = -4; z <= 10; z += 2) {
            const glm::vec3 p = world({x, 2, z});
            REQUIRE(shelter.covered(p.x, p.y, p.z));
            const glm::vec3 shelf = world({x, 3.5f, z});
            REQUIRE(shelter.covered(shelf.x, shelf.y, shelf.z));
            collider.set_snow_collision_depth(0);
            const auto bare = collider.probe_down(p, 5);
            collider.set_snow_collision_depth(0.7f);
            const auto snowy = collider.probe_down(p, 5);
            REQUIRE(bare.hit && snowy.hit);
            REQUIRE_NEAR(snowy.point.y, bare.point.y, 1e-5);
            REQUIRE(snowy.snow_depth_m == 0);
            if (!bare.prop) ++floor_contacts;
        }
    }
    REQUIRE(floor_contacts > 10);
    const glm::vec3 parking = world({-14.5f, 2, -11.2f});
    REQUIRE(!shelter.covered(parking.x, parking.y, parking.z));
    collider.set_snow_collision_depth(0);
    const auto bare_parking = collider.probe_down(parking, 5);
    collider.set_snow_collision_depth(0.7f);
    const auto snowy_parking = collider.probe_down(parking, 5);
    REQUIRE(bare_parking.hit && !bare_parking.prop);
    REQUIRE_NEAR(snowy_parking.point.y, bare_parking.point.y + 0.7f, 1e-5);
    REQUIRE_NEAR(snowy_parking.snow_depth_m, 0.8f, 1e-6);
    const glm::vec3 rooftop = world({-5, 7, 3});
    REQUIRE(!shelter.covered(rooftop.x, rooftop.y, rooftop.z));
    apricot_test::pass("actual non-solid Cloggers roofs shelter indoor floor and shelf heights while outdoor parking still accumulates");
}

}  // namespace

int main() {
    exact_yaw_edges_and_vertical_layers();
    index_crosses_cells_and_rebuild_replaces_cover();
    all_ground_collision_paths_respect_roofs_without_plows();
    actual_cloggers_non_solid_roof_keeps_floor_and_shelves_dry();
    return apricot_test::done("snow_shelter");
}
