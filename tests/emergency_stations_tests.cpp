#include <algorithm>
#include <cmath>
#include <cstring>
#include "city/building_access.h"
#include "city/neighborhood_towers.h"
#include "city/spines.h"
#include "app/vehicle_model_tuning.h"
#include "game/character.h"
#include "test_assert.h"
using namespace apricot;
namespace {
constexpr float dt=1.f/120.f;
glm::vec3 world(const city::StartSite& site,float x,float y,float z) {
    const auto p=city::access_world(site,{x,z});return {p.x,site.ground_m+y,p.y};
}
TerrainCollider station_collider(bool fire,const RoadCollision& road) {
    const auto& site=fire?city::kFireStationSite:city::kPoliceStationSite;
    TerrainCollider collider(city::kMapSeed);collider.set_road_collision(road);
    for(const auto& p:city::bake_emergency_station(fire)) {
        const auto c=world(site,p.centre.x,p.bottom_m+p.height_m*.5f,p.centre.z);
        const float yaw=std::atan2(site.sin_yaw,site.cos_yaw)+glm::radians(p.yaw_deg);
        if(p.solid) collider.add_static_oriented_box(c,{p.width_m*.5f,p.height_m*.5f,p.depth_m*.5f},yaw);
        if(std::strstr(p.name,"station lot") || std::strcmp(p.name,"station interior floor")==0 ||
           std::strcmp(p.name,"station apparatus apron")==0 || std::strcmp(p.name,"station entrance walk")==0)
            collider.add_static_ground_rect({c.x,c.z},site.ground_m+p.bottom_m+p.height_m,
                {p.width_m*.5f,p.depth_m*.5f},yaw,Surface::Rock);
    }
    return collider;
}
void clear_parcels(const RoadGraph& roads) {
    auto neighbors=city::authored_building_access_lots();
    for(bool fire:{true,false}) {
        const auto& site=fire?city::kFireStationSite:city::kPoliceStationSite;
        for(const auto& other:neighbors) {
            if(city::access_same_site(site,other.site)) continue;
            const auto c=city::access_local(site,city::access_world(other.site,
                {other.pavement.centre.x,other.pavement.centre.z}));
            REQUIRE(std::fabs(c.x)>=(site.lot_width_m+other.pavement.width_m)*.5f ||
                    std::fabs(c.y)>=(site.lot_depth_m+other.pavement.depth_m)*.5f);
        }
        for(const auto& tower:city::kNeighborhoodTowers) {
            const auto c=city::access_local(site,{tower.site.origin.x,tower.site.origin.z});
            REQUIRE(std::fabs(c.x)>=(site.lot_width_m+tower.site.lot_width_m)*.5f ||
                    std::fabs(c.y)>=(site.lot_depth_m+tower.site.lot_depth_m)*.5f);
        }
        for(float x=-34;x<=34;x+=1) for(float z=-19;z<=19;z+=1) {
            const auto p=city::access_world(site,{x,z});
            for(const auto& edge:roads.edges()) for(std::size_t i=1;i<edge.points.size();++i) {
                const auto a=edge.points[i-1],d=edge.points[i]-a;
                const float t=std::clamp(glm::dot(p-a,d)/glm::dot(d,d),0.f,1.f);
                REQUIRE(glm::length(p-a-d*t)>=edge.half_width_m()+(edge.sidewalks()?kSidewalkWidthM:0.f)-.01f);
            }
        }
    }
    apricot_test::pass("station plots clear actual road/sidewalk ribbons, existing lots and nearby towers");
}
void station_paths(bool fire,const RoadCollision& road,const city::BuildingAccessBake& access) {
    const auto& site=fire?city::kFireStationSite:city::kPoliceStationSite;
    const auto collider=station_collider(fire,road);
    const auto it=std::find_if(access.lots.begin(),access.lots.end(),[&](const auto& l){return city::access_same_site(l.site,site);});
    REQUIRE(it!=access.lots.end() && it->connected);
    if(fire) {
        REQUIRE_NEAR(it->width_m,27.5f,.001f);
        REQUIRE_NEAR(it->entry_local,-13.4f,.001f);
        const auto tuning=player_model_tuning(DrivingMechanicsStyle::ClassicGta,PlayerCarId::MunicipalFiretruck);
        const float yaw=std::atan2(site.sin_yaw,site.cos_yaw);
        for(float bay:{-22.4f,-13.5f,-4.5f}) {
            REQUIRE(std::fabs(bay-it->entry_local)+1.4f<it->width_m*.5f);
            const auto start=world(site,bay,0,25);
            auto car=spawn_vehicle(tuning,collider,start.x,start.z,yaw);
            // Actual player firetruck suspension/body collision, from the road
            // through the broad lowered curb and open door into each bay.
            for(int step=0;step<1500;++step) {
                const auto q=city::access_local(site,{car.position.x,car.position.z});
                if(q.y<-8.f) break;
                car.velocity.x=-site.sin_yaw*3.f;car.velocity.z=-site.cos_yaw*3.f;
                car=step_vehicle(car,tuning,{},collider,dt);
                REQUIRE(car.impact_count==0u);
                REQUIRE(glm::dot(vehicle_up(car),glm::vec3{0,1,0})>.97f);
            }
            const auto q=city::access_local(site,{car.position.x,car.position.z});
            REQUIRE(q.y<-8.f);REQUIRE_NEAR(q.x,bay,.20f);
        }
    } else {
        REQUIRE_NEAR(it->entry_local,22.f,.001f);
        const VehicleTuning tuning;
        const auto start=world(site,22,0,25);
        auto car=spawn_vehicle(tuning,collider,start.x,start.z,std::atan2(site.sin_yaw,site.cos_yaw));
        for(int step=0;step<600;++step) {
            const auto q=city::access_local(site,{car.position.x,car.position.z});
            if(q.y<16.4f) break;
            car.velocity.x=-site.sin_yaw*3.f;car.velocity.z=-site.cos_yaw*3.f;
            car=step_vehicle(car,tuning,{},collider,dt);
            REQUIRE(car.impact_count==0u);
        }
        const auto q=city::access_local(site,{car.position.x,car.position.z});
        REQUIRE(q.y<16.4f);REQUIRE_NEAR(q.x,22.f,.20f);
    }
    const float door=fire?14.f:0.f;
    const auto start=world(site,door,0,22);
    auto walker=spawn_character(collider,start.x,start.z);
    const CharacterTuning tuning;
    for(int step=0;step<1800;++step) {
        const auto goal=world(site,door,0,2.f);
        const glm::vec2 d{goal.x-walker.position.x,goal.z-walker.position.z};
        if(glm::length(d)<.025f) break;
        InputFrame input;
        input.look_dx=std::atan2(d.x,-d.y)-walker.view_yaw;
        input.throttle=std::min(1.f,glm::length(d)/(tuning.walk_speed_mps*dt));
        walker=step_character(walker,tuning,input,collider,dt);
        REQUIRE(character_position_clear(collider,walker.position,tuning));
    }
    const auto end=city::access_local(site,{walker.position.x,walker.position.z});
    REQUIRE_NEAR(end.x,door,.04f);REQUIRE_NEAR(end.y,2.f,.04f);
    REQUIRE_NEAR(walker.position.y,site.ground_m+.2f,.01f);
    apricot_test::pass(fire?"real firetruck reaches all three apparatus bays; public crew entrance walks cleanly":"public police entrance reaches the reception lobby from sidewalk");
}
}
int main() {
    const TerrainGround ground{city::kMapSeed};RoadGraph roads;
    roads.build(city::map_spines(),{},ground.sampler());
    auto ribbon=bake_ribbons(roads,ground.sampler());
    const auto access=city::bake_building_access(roads,ribbon,ground.sampler());
    city::append_building_access(ribbon,access);
    const auto collision=build_road_collision(ribbon);
    clear_parcels(roads);
    station_paths(true,collision,access);station_paths(false,collision,access);
    return apricot_test::done("emergency_stations_tests");
}
