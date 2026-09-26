#include <cmath>
#include <cstdio>
#include <cstring>
#include "city/construction_expansion.h"
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
glm::vec2 world(const city::StartSite& s,glm::vec2 p) {
    return {s.origin.x+s.cos_yaw*p.x+s.sin_yaw*p.y,
            s.origin.z-s.sin_yaw*p.x+s.cos_yaw*p.y};
}
float road_clearance(glm::vec2 p,bool sidewalk) {
    float clearance=1e9f;
    for(const auto& road:city::kRoads) for(int i=1;i<road.count;++i) {
        const glm::vec2 a{road.path[i-1].x,road.path[i-1].z},b{road.path[i].x,road.path[i].z};
        const auto delta=b-a;
        const float t=std::clamp(glm::dot(p-a,delta)/glm::dot(delta,delta),0.f,1.f);
        const float half=road.width_m>0.f?road.width_m*.5f:city::road_width_m(road.cls)*.5f;
        clearance=std::min(clearance,glm::length(p-a-delta*t)-half-
            ((sidewalk && city::road_has_sidewalks(road.cls))?city::kWalkWidthM:0.f));
    }
    return clearance;
}
void plots_preserve_streets_and_neighbors() {
    const city::StartSite* neighbors[]={&city::kGasStationSite,&city::kMotelSite,
        &city::kApartmentSite,&city::kFastFoodSite,&city::kTacomacoSite,&city::kCarWashSite,&city::kBankSite,
        &city::kAutoRepairSite,&city::kLaundromatSite,&city::kNeighborhoodBarSite,
        &city::kConstructionSite.site,
        &city::kAdditionalConstructionSites[0].site,
        &city::kAdditionalConstructionSites[1].site,
        &city::kAdditionalConstructionSites[2].site,
        &city::kAdditionalConstructionSites[3].site,
        &city::kTwinSkyscraperBlockSites[0],
        &city::kTwinSkyscraperBlockSites[1]};
    std::size_t east_side_towers=0;
    std::size_t southeast_edge_towers=0;
    std::size_t southeast_towers=0;
    std::size_t south_facing_towers=0;
    for(const auto& tower:city::kNeighborhoodTowers) {
        const auto& s=tower.site;
        const glm::vec2 grid{
            city::kGridCos*(s.origin.x-70.f)-city::kGridSin*(s.origin.z+40.f),
            city::kGridSin*(s.origin.x-70.f)+city::kGridCos*(s.origin.z+40.f)};
        if(grid.x>180.f) ++east_side_towers;
        if(grid.x>280.f && grid.y>0.f) ++southeast_edge_towers;
        if(grid.x>90.f && grid.y>0.f) ++southeast_towers;
        if(tower.frontage==city::TowerFrontage::South) ++south_facing_towers;
        const float half_width=s.lot_width_m*.5f;
        const float half_depth=s.lot_depth_m*.5f;
        for(float x:{-half_width,half_width}) for(float z:{-half_depth,half_depth}) {
            const auto p=world(s,{x,z});
            REQUIRE_MSG(road_clearance(p,true)>1.5f,
                        "tower corner too close to road", s.name);
            const float height=mesh_height_at(city::kMapSeed,p.x,p.y);
            if (std::fabs(height-s.ground_m)>.02f)
                std::printf("  height %.3f at %s corner %.0f %.0f grid %.0f %.0f\n",
                            height,s.name,x,z,grid.x,grid.y);
            REQUIRE_NEAR(height,s.ground_m,.02f);
        }
        for(const auto* other:neighbors) {
            const glm::vec2 d{s.origin.x-other->origin.x,s.origin.z-other->origin.z};
            const glm::vec2 local{s.cos_yaw*d.x-s.sin_yaw*d.y,s.sin_yaw*d.x+s.cos_yaw*d.y};
            const bool separate =
                std::fabs(local.x) >
                    (s.lot_width_m + other->lot_width_m) * .5f + 4.f ||
                std::fabs(local.y) >
                    (s.lot_depth_m + other->lot_depth_m) * .5f + 4.f;
            if (!separate)
                std::printf("  overlap: %s <> %s\n", s.name, other->name);
            REQUIRE(separate);
        }
        const float distance=glm::length(glm::vec2{s.origin.x-city::kGasStationSite.origin.x,
                                                   s.origin.z-city::kGasStationSite.origin.z});
        REQUIRE(distance>130.f && distance<600.f);
        REQUIRE(std::atan2(city::neighborhood_tower_roof(tower),distance)>.15f);
    }
    REQUIRE(east_side_towers==18u);
    REQUIRE(southeast_edge_towers==5u);
    REQUIRE(southeast_towers==9u);
    REQUIRE(south_facing_towers==3u);
    REQUIRE(city::kNeighborhoodTowers.size()==25u);
    for(std::size_t i=0;i<city::kNeighborhoodTowers.size();++i)
        for(std::size_t j=i+1;j<city::kNeighborhoodTowers.size();++j) {
            const auto& a=city::kNeighborhoodTowers[i].site;
            const auto& b=city::kNeighborhoodTowers[j].site;
            const glm::vec2 delta{b.origin.x-a.origin.x,b.origin.z-a.origin.z};
            const glm::vec2 local{a.cos_yaw*delta.x-a.sin_yaw*delta.y,
                                  a.sin_yaw*delta.x+a.cos_yaw*delta.y};
            REQUIRE(std::fabs(local.x)>(a.lot_width_m+b.lot_width_m)*.5f+2.f ||
                    std::fabs(local.y)>(a.lot_depth_m+b.lot_depth_m)*.5f+2.f);
        }
    apricot_test::pass("twenty-five tower plots fit existing blocks, including nine southeast towers");
}
void towers_have_walkable_lobbies_and_restrained_piece_counts() {
    std::size_t total_parts=0;
    for(std::size_t i=0;i<city::kNeighborhoodTowers.size();++i) {
        const auto& tower=city::kNeighborhoodTowers[i];
        const auto& site=tower.site;
        const auto parts=city::bake_neighborhood_tower(i);
        REQUIRE(city::valid_start_parts(parts.data(),parts.size()));
        const std::size_t expected_window_lights=
            static_cast<std::size_t>(tower.floors*city::kTowerWindowsPerFloor);
        REQUIRE(parts.size()>expected_window_lights+100u &&
                parts.size()<expected_window_lights+280u);
        total_parts+=parts.size();
        TerrainCollider collider(city::kMapSeed);
        const float yaw=std::atan2(site.sin_yaw,site.cos_yaw);
        std::size_t panes=0,window_lights=0,floor_bands=0;float top=0.f;
        const city::StartPart *core=nullptr,*front_glass=nullptr,*side_glass=nullptr;
        const city::StartPart *front_pane=nullptr,*side_pane=nullptr;
        const city::StartPart *front_pier=nullptr,*side_pier=nullptr,*spandrel=nullptr;
        for(const auto& part:parts) {
            if(!core && std::strcmp(part.name,"tower structural core")==0) core=&part;
            if(!front_glass && std::strcmp(part.name,"tower front rear glazing")==0) front_glass=&part;
            if(!side_glass && std::strcmp(part.name,"tower side glazing")==0) side_glass=&part;
            if(std::strcmp(part.name,"tower office window light")==0) {
                if(!front_pane && part.depth_m<part.width_m) front_pane=&part;
                if(!side_pane && part.width_m<part.depth_m) side_pane=&part;
            }
            if(!front_pier && std::strcmp(part.name,"tower facade vertical pier")==0) front_pier=&part;
            if(!side_pier && std::strcmp(part.name,"tower side vertical pier")==0) side_pier=&part;
            if(!spandrel && std::strcmp(part.name,"tower floor spandrel")==0) spandrel=&part;
            top=std::max(top,part.bottom_m+part.height_m);
            panes+=part.finish==city::BuildingFinish::Glass;
            window_lights+=std::strcmp(part.name,"tower office window light")==0;
            floor_bands+=std::strcmp(part.name,"tower floor spandrel")==0;
            const auto p=world(site,{part.centre.x,part.centre.z});
            if(part.solid) collider.add_static_oriented_box(
                {p.x,site.ground_m+part.bottom_m+part.height_m*.5f,p.y},
                {part.width_m*.5f,part.height_m*.5f,part.depth_m*.5f},yaw);
            if(std::strcmp(part.name,"tower plaza lot")==0 ||
               std::strcmp(part.name,"tower podium floor")==0 ||
               std::strcmp(part.name,"tower entrance walk")==0) {
                collider.add_static_ground_rect(p,site.ground_m+part.bottom_m+part.height_m,
                    {part.width_m*.5f,part.depth_m*.5f},yaw,Surface::Rock);
            }
            if(part.bottom_m<3.f) for(float x:{-.5f,.5f}) for(float z:{-.5f,.5f}) {
                const auto corner=world(site,{part.centre.x+part.width_m*x,part.centre.z+part.depth_m*z});
                REQUIRE(road_clearance(corner,false)>1.5f);
            }
        }
        REQUIRE(panes>=10u);
        REQUIRE(window_lights==expected_window_lights);
        REQUIRE(floor_bands==static_cast<std::size_t>(tower.floors+2));
        REQUIRE(top>70.f && top<140.f);
        REQUIRE(core && front_glass && side_glass && front_pane && side_pane &&
                front_pier && side_pier && spandrel);
        // These are opaque boxes. A layer only centimetres ahead of its
        // backing collapses to the same depth value in the distant skyline.
        const auto check_facade_depth=[&](bool side,const city::StartPart& glass,
                                           const city::StartPart& pane,
                                           const city::StartPart& pier) {
            const auto outward=[&](const city::StartPart& part) {
                const float centre=side?part.centre.x:part.centre.z;
                const float core_centre=side?core->centre.x:core->centre.z;
                const float thickness=side?part.width_m:part.depth_m;
                return std::fabs(centre-core_centre)+thickness*.5f;
            };
            const float core_face=(side?core->width_m:core->depth_m)*.5f;
            const float glass_face=outward(glass);
            const float pane_face=outward(pane);
            const float pier_face=outward(pier);
            REQUIRE(glass_face-core_face>.20f);
            REQUIRE(pane_face-glass_face>.15f);
            REQUIRE(pier_face-pane_face>.10f);
            REQUIRE(outward(*spandrel)-pane_face>.15f);
        };
        check_facade_depth(false,*front_glass,*front_pane,*front_pier);
        check_facade_depth(true,*side_glass,*side_pane,*side_pier);
        // Real character collision, from the public sidewalk apron through
        // the central entrance into the recessed lobby.
        for(float distance=7.f;distance<=22.f;distance+=.1f) {
            const float z=tower.frontage==city::TowerFrontage::South
                ? distance : -distance;
            const auto p=world(site,{0,z});
            const auto support=collider.probe_down({p.x,site.ground_m+2.f,p.y},4.f);
            REQUIRE(support.hit);
            REQUIRE(character_position_clear(collider,support.point,CharacterTuning{}));
        }
        std::printf("  %s: %.1fm, %zu pieces, %zu dynamic windows; sidewalk-to-lobby clear\n",
            site.name,top,parts.size(),window_lights);
    }
    REQUIRE(total_parts<25000u);
    apricot_test::pass("towers have suite-scale facade rhythm, distinct crowns, clear lobbies and bounded geometry");
}
}
int main() {
    plots_preserve_streets_and_neighbors();
    towers_have_walkable_lobbies_and_restrained_piece_counts();
    return apricot_test::done("neighborhood_towers_tests");
}
