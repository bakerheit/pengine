#include <cmath>
#include <limits>

#include <glm/gtc/quaternion.hpp>

#include "city/precipitation_cover.h"
#include "gfx/snow_shelter_grid.h"
#include "test_assert.h"

using namespace apricot;

namespace {
void packed_grid_matches_actual_cloggers_roofs() {
    const auto& site=city::kFastFoodSite;
    const auto world=[&](glm::vec3 p) {
        return glm::vec3{site.origin.x+site.cos_yaw*p.x+site.sin_yaw*p.z,
            site.ground_m+p.y,site.origin.z-site.sin_yaw*p.x+site.cos_yaw*p.z};
    };
    std::vector<StaticBox> roofs;
    for (const auto& part:city::bake_building(city::kFastFoodPlan)) {
        Transform transform;
        transform.position=world({part.centre.x,part.bottom_m+part.height_m*.5f,part.centre.z});
        transform.rotation=glm::angleAxis(std::atan2(site.sin_yaw,site.cos_yaw),glm::vec3{0,1,0})*
            glm::quat(glm::radians(glm::vec3{part.pitch_deg,part.yaw_deg,part.roll_deg}));
        transform.scale={part.width_m,part.height_m,part.depth_m};
        city::append_precipitation_cover(part,transform,roofs);
    }
    REQUIRE(!roofs.empty());
    SnowShelterField field; field.build(roofs);
    SnowShelterGrid grid;
    REQUIRE(grid.build(field));
    REQUIRE(grid.roofs.size()==field.boxes().size()*4u);
    for(float x=-23;x<=13;x+=.37f) for(float z=-14;z<=15;z+=.41f)
        for(float y:{.2f,1.f,2.f,3.5f,7.f}) {
            const auto p=world({x,y,z});
            REQUIRE(grid.covered(p)==field.covered(p.x,p.y,p.z));
        }
    REQUIRE(grid.covered(world({-5,.2f,3})));
    REQUIRE(!grid.covered(world({-5,.2f,-12}))); // Outdoor parking pavement.
    REQUIRE(!grid.covered(world({-5,7,3})));     // Above the roof.
}

void all_roofs_survive_overlap_and_distance() {
    std::vector<StaticBox> roofs;
    for(int i=0;i<200;++i) {
        const float x=static_cast<float>(i)*64.f-6400.f;
        StaticBox box; box.bounds={{x,4,-2},{x+8,4.3f,2}};
        roofs.push_back(box);
    }
    // More overlapping candidates than a typical uniform budget. Only the
    // final high roof shelters this point; a capped list would miss it.
    for(int i=0;i<180;++i) {
        const float y=static_cast<float>(i)+10.f;
        StaticBox box; box.bounds={{-2,y,30},{2,y+.2f,34}};
        roofs.push_back(box);
    }
    SnowShelterField field; field.build(roofs);
    SnowShelterGrid grid;
    REQUIRE(grid.build(field));
    REQUIRE(grid.roofs.size()==roofs.size()*4u);
    for(const auto& roof:roofs) {
        const auto p=roof.bounds.center()-glm::vec3{0,1,0};
        REQUIRE(grid.covered(p)==field.covered(p.x,p.y,p.z));
    }
    REQUIRE(grid.covered({0,188.5f,32}));
    uint32_t max_candidates=0;
    for(const auto& cell:grid.cells) max_candidates=std::max(max_candidates,cell.y);
    REQUIRE(max_candidates>=180u);
    REQUIRE(!grid.covered({0,190.f,32}));
    REQUIRE(!grid.covered({-6420,0,0}));
    REQUIRE(!grid.covered({6400,0,0}));
    // Insufficient storage fails explicitly instead of publishing partial data.
    SnowShelterGrid small;
    REQUIRE(!small.build(field,32u));
}

void packed_edges_and_roof_top_are_exact() {
    TerrainCollider collider{1};
    collider.add_static_oriented_box({0,5,0},{5,.2f,1},glm::radians(45.f));
    SnowShelterField field; field.build(collider.static_boxes());
    SnowShelterGrid grid; REQUIRE(grid.build(field));
    REQUIRE(grid.covered({0,0,0}));
    REQUIRE(!grid.covered({3,0,3})); // Empty rotated AABB corner.
    const auto& roof=field.boxes().front();
    const float underside=roof.bounds.min.y-SnowShelterField::kRoofClearanceM;
    REQUIRE(grid.covered({0,underside-.001f,0}));
    REQUIRE(!grid.covered({0,underside,0}));
    REQUIRE(!grid.covered({0,roof.bounds.max.y,0}));
    for(float x=-6;x<6;x+=.073f) for(float z=-6;z<6;z+=.083f)
        REQUIRE(grid.covered({x,1,z})==field.covered(x,1,z));
    REQUIRE(!grid.covered({std::numeric_limits<float>::quiet_NaN(),0,0}));
    field.build({}); REQUIRE(grid.build(field));
    REQUIRE(grid.cells.empty() && grid.roofs.empty() && grid.indices.empty());
    REQUIRE(!grid.covered({0,0,0}));
}

void coarse_cells_preserve_small_remote_roofs() {
    StaticBox a,b;
    a.bounds={{-10000,4,-10000},{-9998,4.2f,-9998}};
    b.bounds={{10000,4,10000},{10002,4.2f,10002}};
    SnowShelterField field; field.build({a,b});
    SnowShelterGrid grid; REQUIRE(grid.build(field,64u));
    REQUIRE(grid.cell_size>32.f);
    REQUIRE(grid.cells.size()<=64u);
    REQUIRE(grid.covered({-9999,0,-9999}));
    REQUIRE(grid.covered({10001,0,10001}));
    REQUIRE(!grid.covered({0,0,0}));
}
}

int main() {
    packed_grid_matches_actual_cloggers_roofs();
    all_roofs_survive_overlap_and_distance();
    packed_edges_and_roof_top_are_exact();
    coarse_cells_preserve_small_remote_roofs();
    return 0;
}
