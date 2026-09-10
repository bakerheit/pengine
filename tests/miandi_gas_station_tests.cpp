#include <filesystem>
#include <cstring>
#include "city/miandi_gas_station_asset.h"
#include "city/north_pinatty_gas_station.h"
#include "city/north_airbase.h"
#include "city/terrain_ops.h"
#include "city/building_access.h"
#include "city/spines.h"
#include "city/roads.h"
#include "city/miandi_layout.h"
#include "city/miandi_context.h"
#include "city/miandi_streets.h"
#include "core/emesh_reader.h"
#include "game/character.h"
#include "test_assert.h"
using namespace apricot;
namespace {
PlayerCharacterState walk(PlayerCharacterState s,const TerrainCollider& collider,glm::vec3 goal) {
    const CharacterTuning tuning;
    constexpr float dt=1.f/120;
    for(int i=0;i<4000;++i) {
        glm::vec2 d{goal.x-s.position.x,goal.z-s.position.z};
        if(glm::length(d)<.025f)break;
        InputFrame input;
        input.look_dx=std::atan2(d.x,-d.y)-s.view_yaw;
        input.throttle=std::min(1.f,glm::length(d)/(tuning.walk_speed_mps*dt));
        s=step_character(s,tuning,input,collider,dt);
        REQUIRE(character_position_clear(collider,s.position,tuning));
    }
    return s;
}
}
void check_station(const city::StartSite& site,const char* root,unsigned road_id,bool require_assets) {
    TerrainGround ground{city::kMapSeed};RoadGraph roads;
    roads.build(city::map_spines(),{},ground.sampler());
    auto ribbon=bake_ribbons(roads,ground.sampler());
    const auto access=city::bake_building_access(roads,ribbon,ground.sampler());
    int connected=0;
    for(const auto& lot:access.lots)if(city::access_same_site(lot.site,site)) {
        REQUIRE(lot.connected);REQUIRE((lot.entrance.road_key>>32)==road_id);
        REQUIRE(lot.width_m>=10.f);++connected;
    }
    REQUIRE(connected==1);
    for(float x:{-20.f,0.f,20.f})for(float z:{-28.f,0.f,28.f}) {
        const auto p=city::miandi_gas_station_world({x,0,z},site);
        REQUIRE_NEAR(mesh_height_at(city::kMapSeed,p.x,p.z),site.ground_m,.02f);
        if(road_id==230) {
            REQUIRE(city::miandi_city_contains(p.x,p.z));
            REQUIRE(p.z<8000-city::road_ribbon_half_m(city::RoadClass::Street));
        } else {
            REQUIRE(p.z<-1800);
            REQUIRE(city::authored_site_clearance_weight(p.x,p.z)>.99f);
            REQUIRE(p.z>city::kHalberdFieldSite.origin.z+city::kHalberdFieldSite.lot_depth_m*.5f);
        }
        // Every perimeter sample clears all nearby road ribbons, including the
        // Yard Road and airbase approach; a map label alone is not clearance.
        for(const auto& road:city::kRoads)for(int i=1;i<road.count;++i) {
            const glm::vec2 a{road.path[i-1].x,road.path[i-1].z};
            const glm::vec2 b{road.path[i].x,road.path[i].z};
            const auto d=b-a;
            const float t=std::clamp(glm::dot(glm::vec2{p.x,p.z}-a,d)/glm::dot(d,d),0.f,1.f);
            REQUIRE(glm::length(glm::vec2{p.x,p.z}-(a+t*d))>city::road_ribbon_half_m(road.cls));
        }
    }
    // The finished/context grid is south of Gateway; retained west massing
    // must also stay clear of this parcel, not merely its map marker.
    if(road_id==230)for(const auto& part:city::kMiandiBuildingParts)if(city::miandi_keeps_rough_part(part)) {
        const auto c=city::miandi_world_point(part.centre);
        REQUIRE(c.x+part.width_m*.5f<site.origin.x-20 || c.x-part.width_m*.5f>site.origin.x+20 ||
                c.z+part.depth_m*.5f<site.origin.z-28 || c.z-part.depth_m*.5f>site.origin.z+28);
    }
    if(road_id==230)for(const auto& part:city::bake_miandi_context()) {
        const auto c=city::miandi_world_point(part.centre);
        REQUIRE(c.z-part.depth_m*.5f>site.origin.z+28);
    }
    apricot_test::pass("station parcel clears roads and existing massing and connects to its assigned street");
    if(!std::filesystem::exists(city::miandi_gas_station_path("materials.txt",root)) &&
       !require_assets) {
        std::puts("SKIP private station asset checks: run tools/cook_miandi_gas_station.py");
        return;
    }
    city::MiandiGasStationAsset asset;REQUIRE(city::load_miandi_gas_station_asset(asset,root));
    REQUIRE(asset.materials.size()>45);REQUIRE(asset.lights.size()>=10);REQUIRE(asset.covers.size()==2);
    std::size_t triangles=0;
    for(const auto& mat:asset.materials) {
        StaticEmesh mesh;REQUIRE(read_static_emesh(city::miandi_gas_station_path(mat.mesh,root),mesh));
        triangles+=mesh.indices.size()/3;
        REQUIRE(mesh.bounds.min.x>=-20 && mesh.bounds.max.x<=20);
        REQUIRE(mesh.bounds.min.z>=-28 && mesh.bounds.max.z<=28);
        if(mat.texture!="-")REQUIRE(std::filesystem::exists(city::miandi_gas_station_path(mat.texture,root)));
    }
    REQUIRE(triangles>10000);
    city::append_building_access(ribbon,access);
    TerrainCollider collider{city::kMapSeed};collider.set_road_collision(build_road_collision(ribbon));
    collider.add_static_ground_rect({site.origin.x,site.origin.z},site.ground_m+.07f,{20,28},std::atan2(site.sin_yaw,site.cos_yaw),Surface::Rock);
    city::add_miandi_gas_station_collision(collider,asset,site);
    const float street_z=road_id==230?40.f:46.f;
    auto start=city::miandi_gas_station_world({-2.3f,0,street_z},site);
    auto state=spawn_character(collider,start.x,start.z);
    for(const auto p:std::vector<glm::vec2>{{-2.3f,23},{-2.3f,0},{-2.3f,-5},{-2.3f,-8.3f},
        {-2.3f,-11.6f},{-5,-11.6f},{-2.3f,-11.6f},{-2.3f,-8.3f},{-2.3f,0},{-2.3f,street_z}}) {
        const auto goal=city::miandi_gas_station_world({p.x,0,p.y},site);
        state=walk(state,collider,goal);
        std::printf("walk %.2f %.2f reached %.2f %.2f y %.2f\n",p.x,p.y,state.position.x-site.origin.x,state.position.z-site.origin.z,state.position.y);
        REQUIRE_NEAR(state.position.x,goal.x,.06f);REQUIRE_NEAR(state.position.z,goal.z,.06f);
        REQUIRE(state.position.y>=site.ground_m && state.position.y<site.ground_m+.5f);
    }
    apricot_test::pass("character walks from its street through forecourt and original door into stocked aisles and back");
    // A 2.2 m car corridor between the two pump islands stays clear.
    for(float z=-2;z<=street_z-2;z+=.5f)for(float x:{-3.4f,-2.3f,-1.2f}) {
        const auto p=city::miandi_gas_station_world({x,0,z},site);
        REQUIRE(character_position_clear(collider,{p.x,site.ground_m+.25f,p.z},CharacterTuning{}));
        REQUIRE_NEAR(collider.height(p.x,p.z),site.ground_m+.1f,.25f);
    }
    start=city::miandi_gas_station_world({-2.3f,0,-8.3f},site);
    state=spawn_character(collider,start.x,start.z);
    state=walk(state,collider,city::miandi_gas_station_world({0,0,-8.3f},site));
    REQUIRE((state.position.x-site.origin.x)*site.cos_yaw<-.3f); // original checkout counter
    collider.add_static_oriented_box(city::miandi_gas_station_world({-2.3f,1.5f,-6.8f},site),{.6f,1.5f,.1f},std::atan2(site.sin_yaw,site.cos_yaw));
    start=city::miandi_gas_station_world({-2.3f,0,-5},site);state=spawn_character(collider,start.x,start.z);
    state=walk(state,collider,city::miandi_gas_station_world({-2.3f,0,-8.3f},site));
    REQUIRE((state.position.z-site.origin.z)*site.cos_yaw>-6.4f);
    apricot_test::pass("pump corridor is clear; original counter and blocked-door control stop movement");
    if(road_id==90) {
        city::MiandiGasStationAsset original;
        REQUIRE(city::load_miandi_gas_station_asset(original));
        REQUIRE(asset.boxes.size()==original.boxes.size());
        REQUIRE(asset.grounds.size()==original.grounds.size());
        REQUIRE(asset.lights==original.lights);
        for(std::size_t i=0;i<asset.boxes.size();++i) {
            REQUIRE(asset.boxes[i].centre==original.boxes[i].centre);
            REQUIRE(asset.boxes[i].half==original.boxes[i].half);
        }
        for(std::size_t i=0;i<asset.covers.size();++i) {
            REQUIRE(asset.covers[i].centre==original.covers[i].centre);
            REQUIRE(asset.covers[i].half==original.covers[i].half);
        }
        std::ifstream manifest(city::miandi_gas_station_path("manifest.json",root));
        std::string content{std::istreambuf_iterator<char>(manifest),{}};
        REQUIRE(content.find("Six Twelve")!=std::string::npos);
        REQUIRE(std::strcmp(site.name,"Six Twelve")==0);
        apricot_test::pass("Six Twelve copy retains furnished geometry, collision, canopy cover and light layout");
    }
}

int main(int argc,char** argv) {
    const bool required=argc>1 && std::strcmp(argv[1],"--require-assets")==0;
    check_station(city::kMiandiGasStationSite,city::kMiandiGasStationAssetRoot,230,required);
    check_station(city::kNorthPinattyGasStationSite,city::kNorthPinattyGasStationAssetRoot,90,required);
    return apricot_test::done("miandi_gas_station_tests");
}
