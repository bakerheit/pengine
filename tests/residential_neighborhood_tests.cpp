#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <queue>
#include <string>
#include "city/residential_neighborhood.h"
#include "city/building_access.h"
#include "city/landmarks.h"
#include "city/neighborhood_towers.h"
#include "city/spines.h"
#include "core/asset_root.h"
#include "game/character.h"
#include "physics/vehicle.h"
#include "test_assert.h"
using namespace apricot;
namespace {
constexpr float dt=1.f/120.f;
glm::vec3 world(const city::StartSite& site,float x,float y,float z) {
    const auto p=city::residential_world(site,{x,z});return {p.x,site.ground_m+y,p.y};
}
TerrainCollider collider_for(std::size_t index,const std::vector<city::StartPart>& parts,const RoadCollision& roads) {
    TerrainCollider collider(city::kMapSeed);collider.set_road_collision(roads);
    const auto& site=city::kResidentialHouses[index].site;
    for(const auto& p:parts) {
        const auto c=world(site,p.centre.x,p.bottom_m+p.height_m*.5f,p.centre.z);
        const float yaw=std::atan2(site.sin_yaw,site.cos_yaw)+glm::radians(p.yaw_deg);
        if(p.solid)collider.add_static_oriented_box(c,{p.width_m*.5f,p.height_m*.5f,p.depth_m*.5f},yaw);
        if(city::residential_ground_piece(p))collider.add_static_ground_rect({c.x,c.z},
            site.ground_m+p.bottom_m+p.height_m,{p.width_m*.5f,p.depth_m*.5f},yaw,Surface::Rock);
    }
    return collider;
}
void clear_parcels(const RoadGraph& roads,GroundSampler ground) {
    for(std::size_t i=0;i<city::kResidentialHouses.size();++i) {
        const auto& h=city::kResidentialHouses[i];const auto& site=h.site;
        REQUIRE(glm::length(glm::vec2{site.origin.x-18,site.origin.z+12})>950.f);
        REQUIRE(glm::length(glm::vec2{site.origin.x-150,site.origin.z-2350})>1800.f);
        REQUIRE(glm::length(glm::vec2{site.origin.x-1150,site.origin.z-300})>50.f);
        for(std::size_t j=0;j<i;++j) {
            const auto& other=city::kResidentialHouses[j].site;bool separated=false;
            for(const auto* axis_site:{&site,&other})for(glm::vec2 axis:{glm::vec2{axis_site->cos_yaw,-axis_site->sin_yaw},glm::vec2{axis_site->sin_yaw,axis_site->cos_yaw}}) {
                const auto radius=[&](const city::StartSite& s) {return 16.f*std::fabs(glm::dot(axis,{s.cos_yaw,-s.sin_yaw}))+17.f*std::fabs(glm::dot(axis,{s.sin_yaw,s.cos_yaw}));};
                separated|=std::fabs(glm::dot(axis,glm::vec2{site.origin.x-other.origin.x,site.origin.z-other.origin.z}))>=radius(site)+radius(other)+.5f;
            }
            REQUIRE(separated);
        }
        float terrain_min=1000,terrain_max=-1000;
        for(float x=-16;x<=16;x+=1)for(float z=-17;z<=17;z+=1) {
            const auto p=city::residential_world(site,{x,z});
            terrain_min=std::min(terrain_min,ground.at(p.x,p.y));terrain_max=std::max(terrain_max,ground.at(p.x,p.y));
            for(const auto& e:roads.edges())for(std::size_t j=1;j<e.points.size();++j) {
                const auto d=e.points[j]-e.points[j-1];const float t=std::clamp(glm::dot(p-e.points[j-1],d)/glm::dot(d,d),0.f,1.f);
                REQUIRE_MSG(glm::length(p-e.points[j-1]-d*t)>=e.half_width_m()+(e.sidewalks()?kSidewalkWidthM:0)+.90f,"house parcel enters street/sidewalk",site.name);
            }
        }
        REQUIRE(terrain_max-terrain_min<.8f);
        const float floor=city::residential_floor_top(i,ground);
        for(float x=-h.width_m*.5f;x<=h.width_m*.5f;x+=.25f)for(float z=-6;z<=6;z+=.25f)
            REQUIRE(city::residential_height(site,ground,x,z)<floor-.10f);
        const auto parts=city::bake_residential_house(i,ground);REQUIRE(city::valid_start_parts(parts.data(),parts.size()));
        REQUIRE(parts.size()<(h.enterable?480u:350u));
        // No slab paints over the lawn. Only narrow drives, walks and the house support.
        for(const auto& p:parts) if(city::residential_ground_piece(p)) {
            REQUIRE(p.width_m<16.f);REQUIRE(p.depth_m<=12.f);
            if(std::strcmp(p.name,"house driveway paving")==0)REQUIRE_NEAR(p.width_m,5.2f,.001f);
        }
        std::printf("  %s: %zu parts, floor %.2fm, yard relief %.2fm\n",site.name,parts.size(),site.ground_m+floor,terrain_max-terrain_min);
    }
    apricot_test::pass("six suburban parcels clear roads, sidewalks, each other, landmark and distant commercial/airport areas");
}
PlayerCharacterState walk(PlayerCharacterState s,const TerrainCollider& collider,const city::StartSite& site,glm::vec2 local) {
    const CharacterTuning tuning;const auto goal=city::residential_world(site,local);
    for(int tick=0;tick<2100;++tick) {
        const glm::vec2 delta{goal.x-s.position.x,goal.y-s.position.z};if(glm::length(delta)<.02f)break;
        InputFrame input;input.look_dx=std::atan2(delta.x,-delta.y)-s.view_yaw;
        input.throttle=std::min(1.f,glm::length(delta)/(tuning.walk_speed_mps*dt));
        s=step_character(s,tuning,input,collider,dt);REQUIRE(character_position_clear(collider,s.position,tuning));
    }
    const auto q=city::access_local(site,{s.position.x,s.position.z});
    if(glm::length(q-local)>.04f)std::printf("walk blocked toward %.2f %.2f at %.2f %.2f\n",local.x,local.y,q.x,q.y);
    REQUIRE_NEAR(q.x,local.x,.04f);REQUIRE_NEAR(q.y,local.y,.04f);return s;
}
void sampler_consistency(const RoadGraph& roads,const RibbonBake& ribbon,GroundSampler base) {
    struct RaisedGround {GroundSampler base;};const RaisedGround context{base};
    const GroundSampler raised{[](const void* raw,float x,float z) {
        return static_cast<const RaisedGround*>(raw)->base.at(x,z)+.75f;
    },&context};
    const auto normal=city::authored_building_access_lots(base);
    const auto alternate=city::authored_building_access_lots(raised);
    const auto access=city::bake_building_access(roads,ribbon,raised);
    for(std::size_t i=normal.size()-6u;i<normal.size();++i) {
        REQUIRE_NEAR(city::access_lot_top(alternate[i])-city::access_lot_top(normal[i]),.75f,.0001f);
        REQUIRE_NEAR(access.lots[i].plot.bottom_m,alternate[i].pavement.bottom_m,.0001f);
        const auto parts=city::bake_residential_house(i-(normal.size()-6u),raised);
        for(const auto& part:parts)if(std::strcmp(part.name,"house driveway paving")==0 && part.centre.z>17.f)
            REQUIRE_NEAR(part.bottom_m,alternate[i].pavement.bottom_m,.0001f);
    }
    apricot_test::pass("house geometry and default access bake share the caller's terrain sampler");
}
void live_routes(const RoadCollision& roads,const city::BuildingAccessBake& access,GroundSampler ground) {
    for(std::size_t i=0;i<city::kResidentialHouses.size();++i) {
        const auto& site=city::kResidentialHouses[i].site;
        const auto parts=city::bake_residential_house(i,ground);const auto collider=collider_for(i,parts,roads);
        const auto it=std::find_if(access.lots.begin(),access.lots.end(),[&](const auto& lot){return city::access_same_site(site,lot.site);});
        REQUIRE(it!=access.lots.end() && it->connected);REQUIRE(it->frontage.empty() && it->replacement_pavement.empty());
        REQUIRE(it->entrance.road_key>>32==123u);REQUIRE_NEAR(it->entry_local,11.f,.001f);
        const VehicleTuning tuning;const auto start=world(site,11,0,26);
        auto car=spawn_vehicle(tuning,collider,start.x,start.z,std::atan2(site.sin_yaw,site.cos_yaw));
        for(int tick=0;tick<1800;++tick) {
            const auto q=city::access_local(site,{car.position.x,car.position.z});if(q.y<-6)break;
            car.velocity.x=-site.sin_yaw*3.f;car.velocity.z=-site.cos_yaw*3.f;
            car=step_vehicle(car,tuning,{},collider,dt);
            REQUIRE_MSG(car.impact_count==0u,"driveway impact",site.name);
            REQUIRE(glm::dot(vehicle_up(car),glm::vec3{0,1,0})>.96f);
        }
        const auto q=city::access_local(site,{car.position.x,car.position.z});REQUIRE(q.y<-6);REQUIRE_NEAR(q.x,11.f,.20f);
        const auto foot=world(site,0,0,19.5f);auto person=spawn_character(collider,foot.x,foot.z);
        person=walk(person,collider,site,{0,7});
        REQUIRE_NEAR(person.position.y,site.ground_m+city::residential_floor_top(i,ground),.02f);
        if(i==city::kResidentialTargetHouse) {
            for(glm::vec2 goal:std::vector<glm::vec2>{{0,1},{-3,1},{-3,-2.5f},{-3,-3.5f},{-3,1},
                {0,1},{0,-2.7f},{0,1},{3,1},{3,-1.7f},{2.3f,-3.8f},{2.3f,-1.7f},{3,1},
                {0,1},{0,7},{0,19.5f},{0,7},{0,1},
                {-3,0},{-8,0},{-12,0},{-12,-10},{-12,0},{-8,0},{-3,0},{0,1},{0,7}})
                person=walk(person,collider,site,goal);
        }
    }
    apricot_test::pass("actual vehicle suspension crosses all six lowered curbs and parks; character reaches every porch and all target-house rooms/garden");
}
void interior_detail_contract(GroundSampler ground) {
    const auto parts=city::bake_residential_house(city::kResidentialTargetHouse,ground);
    const auto find=[&](const char* name)->const city::StartPart& {
        const auto it=std::find_if(parts.begin(),parts.end(),[&](const auto& p){return std::strcmp(p.name,name)==0;});
        REQUIRE(it!=parts.end());return *it;
    };
    for(const char* name:{"house living rug field","house curtain fold","house painted trim",
        "house paper newspaper","house ceramic plate","house study radio case",
        "house bedroom folded quilt","house bath tile floor","house bath folded towel"})
        REQUIRE(!find(name).solid);
    // All newly authored details are visual only. The known furniture, walls,
    // floor and doors retain the exact collision setup exercised by live_routes.
    const auto begin=std::find_if(parts.begin(),parts.end(),[](const auto& p){return std::strcmp(p.name,"house painted trim")==0;});
    REQUIRE(begin!=parts.end());
    for(auto it=begin;it!=parts.end();++it)REQUIRE(!it->solid);
    const float floor=city::residential_floor_top(city::kResidentialTargetHouse,ground);
    REQUIRE(find("house living rug field").bottom_m+find("house living rug field").height_m<floor+.04f);
    REQUIRE(std::count_if(parts.begin(),parts.end(),[](const auto& p){return std::strcmp(p.name,"house interior light lens")==0;})==5);
    // No detail pass silently spills into the other five closed homes.
    for(std::size_t i=0;i<city::kResidentialHouses.size();++i)if(i!=city::kResidentialTargetHouse) {
        const auto other=city::bake_residential_house(i,ground);
        REQUIRE(std::none_of(other.begin(),other.end(),[](const auto& p){return std::strcmp(p.name,"house paper newspaper")==0;}));
    }
    apricot_test::pass("102 detail stays visual-only, low-profile and bounded; five warm ceiling lights; other homes unchanged");
}
void residential_gables_are_closed(GroundSampler ground) {
    for(std::size_t i=0;i<city::kResidentialHouses.size();++i) {
        const auto& house=city::kResidentialHouses[i];
        const auto parts=city::bake_residential_house(i,ground);
        int ends=0,bases=0;
        for(const auto& part:parts) {
            if(part.shape==city::BuildingPieceShape::GablePrism) {
                ++ends;
                REQUIRE(part.finish==house.wall_finish);
                REQUIRE(!part.solid);
                REQUIRE_NEAR(part.yaw_deg,i%2==0 ? 0.f : 90.f,.0001f);
                REQUIRE_NEAR(part.bottom_m+part.height_m,
                    city::residential_floor_top(i,ground)+house.wall_height_m+house.roof_rise_m,.0001f);
            }
            if(std::strcmp(part.name,"gable end base")==0) ++bases;
        }
        REQUIRE_MSG(ends==2 && bases==2,"missing roof end closure",house.site.name);
    }
    apricot_test::pass("six residential roofs have two wall-matched closed gable ends below their overhangs");
}
void psx_texture_style_contract(GroundSampler ground) {
    std::size_t textured=0;
    for(std::size_t i=0;i<city::kResidentialHouses.size();++i) {
        const auto& house=city::kResidentialHouses[i];
        REQUIRE(city::residential_house_index(house.site)==i);
        if(!city::residential_has_imported_texture(i))continue;
        ++textured;
        REQUIRE(!house.enterable);
        const auto& style=city::kResidentialTextureStyles[i];
        REQUIRE(style.wall_texture!=nullptr);
        REQUIRE(style.roof_texture!=nullptr);
        const std::string root="models/world/psx_house_textures/";
        REQUIRE(std::filesystem::is_regular_file(asset_path(root+style.wall_texture)));
        REQUIRE(std::filesystem::is_regular_file(asset_path(root+style.roof_texture)));

        const auto parts=city::bake_residential_house(i,ground);
        REQUIRE(std::count_if(parts.begin(),parts.end(),
            city::residential_wall_texture_piece)>4);
        REQUIRE(std::count_if(parts.begin(),parts.end(),
            city::residential_roof_texture_piece)>0);
        for(const auto& part:parts)if(city::residential_ground_piece(part))
            REQUIRE(!city::residential_wall_texture_piece(part) &&
                    !city::residential_roof_texture_piece(part));
    }
    REQUIRE(textured==3u);
    REQUIRE(!city::residential_has_imported_texture(city::kResidentialTargetHouse));
    apricot_test::pass("three closed Sycamore homes use PSX wall and roof maps while 102 stays authored and enterable");
}
}
int main(){const TerrainGround ground{city::kMapSeed};RoadGraph roads;roads.build(city::map_spines(),{},ground.sampler());
    auto ribbon=bake_ribbons(roads,ground.sampler());const auto access=city::bake_building_access(roads,ribbon,ground.sampler());
    sampler_consistency(roads,ribbon,ground.sampler());
    city::append_building_access(ribbon,access);clear_parcels(roads,ground.sampler());
    psx_texture_style_contract(ground.sampler());
    interior_detail_contract(ground.sampler());
    residential_gables_are_closed(ground.sampler());
    live_routes(build_road_collision(ribbon),access,ground.sampler());return apricot_test::done("residential_neighborhood_tests");}
