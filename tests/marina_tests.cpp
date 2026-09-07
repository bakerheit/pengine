#include <cstdio>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <glm/gtc/quaternion.hpp>
#include "app/driving_mechanics.h"
#include "app/vehicle_model_tuning.h"
#include "city/authored_staff.h"
#include "city/marina.h"
#include "city/roads.h"
#include "city/spines.h"
#include "game/delivery_mission.h"
#include "game/character.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "road/road_graph.h"
#include "test_assert.h"
using namespace apricot;

namespace {
float cross2(glm::vec2 a,glm::vec2 b) { return a.x*b.y-a.y*b.x; }
bool triangle_covers(glm::vec2 a,glm::vec2 b,glm::vec2 c,glm::vec2 p) {
    constexpr float eps=1e-4f;
    const float area=cross2(b-a,c-a);
    if(std::fabs(area)<1e-7f) return false;
    const float u=cross2(b-p,c-p)/area;
    const float v=cross2(c-p,a-p)/area;
    const float w=1.f-u-v;
    return u>=-eps && v>=-eps && w>=-eps;
}
bool collision_covers(const RoadCollision& collision,glm::vec2 p) {
    for(const auto& tri:collision.triangles) {
        if(triangle_covers({tri.geom.a.x,tri.geom.a.z},
                           {tri.geom.b.x,tri.geom.b.z},
                           {tri.geom.c.x,tri.geom.c.z},p)) return true;
    }
    return false;
}
bool mesh_covers(const RoadMesh& mesh,glm::vec2 p) {
    for(std::size_t i=0;i+2<mesh.indices.size();i+=3) {
        const auto a=mesh.vertices[mesh.indices[i]].position;
        const auto b=mesh.vertices[mesh.indices[i+1]].position;
        const auto c=mesh.vertices[mesh.indices[i+2]].position;
        if(triangle_covers({a.x,a.z},{b.x,b.z},{c.x,c.z},p)) return true;
    }
    return false;
}
}

int main() {
    REQUIRE(std::strcmp(city::kMarlinDockSite.name,"Ostend Bait & Tackle")==0);
    const auto map_parts=city::marina_map_footprints();
    REQUIRE(map_parts.size()==8); // Four decks, two roof slopes, lot and ramp.
    int map_decks=0,map_lots=0;float deck_area=0,roof_area=0;
    for(const auto& fp:map_parts) {
        REQUIRE(fp.name!=nullptr);
        if(fp.dock && std::strcmp(fp.name,"Ostend pier")==0) {
            ++map_decks;deck_area+=fp.width_m*fp.depth_m;
        } else if(fp.lot) {
            ++map_lots;
            REQUIRE_NEAR(fp.width_m,city::kMarinaParkingWidth,.001);
            REQUIRE_NEAR(fp.depth_m,city::kMarinaParkingDepth,.001);
        } else if(std::strcmp(fp.name,"Ostend Bait & Tackle")==0) {
            roof_area+=fp.width_m*fp.depth_m;
        }
        // Water between the berthing fingers stays water on the map.
        if(std::strcmp(fp.name,"Ostend pier")==0)
            REQUIRE(std::fabs(fp.centre.x+17.f)>fp.width_m*.5f ||
                    std::fabs(fp.centre.z-7.f)>fp.depth_m*.5f);
    }
    REQUIRE(map_decks==4 && map_lots==1);
    REQUIRE_NEAR(deck_area,32.f*3.f+2.f*1.8f*9.8f+12.f*9.8f,.001);
    REQUIRE_NEAR(roof_area,5.7f*5.f,.001);
    const auto parts=city::bake_marina();
    const auto again=city::bake_marina();
    REQUIRE(parts.size()==again.size());
    REQUIRE(parts.size()>400 && parts.size()<950);
    TerrainCollider collider{city::kMapSeed};
    int decks=0,planks=0,lights=0,rings=0;
    for(std::size_t i=0;i<parts.size();++i) {
        const auto& p=parts[i];
        REQUIRE(p.name && p.width_m>0 && p.depth_m>0 && p.height_m>0);
        REQUIRE(std::isfinite(p.bottom_m));
        REQUIRE(p.centre.x==again[i].centre.x && p.centre.z==again[i].centre.z);
        const glm::vec3 pos{city::kMarlinDockSite.origin.x+p.centre.x,p.bottom_m+p.height_m*.5f,
                            city::kMarlinDockSite.origin.z+p.centre.z};
        const glm::vec3 half{p.width_m*.5f,p.height_m*.5f,p.depth_m*.5f};
        if(p.solid) {
            const glm::mat3 rot=glm::mat3_cast(glm::quat(glm::radians(glm::vec3{p.pitch_deg,p.yaw_deg,p.roll_deg})));
            const glm::vec3 ext=glm::abs(rot[0])*half.x+glm::abs(rot[1])*half.y+glm::abs(rot[2])*half.z;
            collider.add_static_box({pos-ext,pos+ext});
            // The existing speedboat's entire berth stays unobstructed.
            if(p.bottom_m+p.height_m>0 && p.bottom_m<1.5f)
                REQUIRE(p.centre.x+half.x< -8.7f || p.centre.x-half.x> -1.2f ||
                        p.centre.z+half.z< -4.7f || p.centre.z-half.z> -2.1f);
        }
        if(std::strcmp(p.name,"marina deck structure")==0) {
            ++decks;collider.add_static_ground_rect({pos.x,pos.z},city::kMarinaDeckTop,{half.x,half.z},0);
        }
        planks+=std::strcmp(p.name,"marina timber plank")==0;
        lights+=std::strcmp(p.name,"marina light emitter")==0;
        rings+=std::strcmp(p.name,"marina life ring")==0;
    }
    REQUIRE(decks==4 && planks>100 && lights==4 && rings==24);
    const city::Road* access_road=nullptr;
    for(int i=0;i<city::kRoadCount;++i)
        if(city::kRoads[i].id==84) access_road=&city::kRoads[i];
    REQUIRE(access_road!=nullptr);
    REQUIRE(std::strcmp(access_road->name,"Boatworks Road")==0);
    REQUIRE(access_road->cls==city::RoadClass::Alley);
    REQUIRE(access_road->width_m==8.f && access_road->shapes_ground);
    REQUIRE(access_road->curb_cut_tee);
    REQUIRE_NEAR(access_road->corridor_feather_m,12.f,.001f);
    REQUIRE_NEAR(access_road->path[0].z,
                 city::kMarlinDockSite.origin.z+city::kMarinaEntranceApproachLocalZ,.001f);
    const auto end=access_road->path[access_road->count-1];
    REQUIRE(std::fabs(end.x-(city::kMarlinDockSite.origin.x+city::kMarinaParkingCentre.x))<=
            city::kMarinaParkingWidth*.5f+.001f);
    REQUIRE(std::fabs(end.z-(city::kMarlinDockSite.origin.z+city::kMarinaParkingCentre.z))<
            city::kMarinaParkingDepth*.5f);
    RoadGraph road_graph;
    TerrainGround road_ground{city::kMapSeed};
    road_graph.build(city::map_spines(),RoadGraphParams{},road_ground.sampler());
    int berth_joined=0;
    int apron_joined=0;
    for(const auto& node:road_graph.nodes()) {
        bool apron=false,berth=false,access=false;
        for(const auto edge_id:node.edges) {
            const auto id=road_graph.edge(edge_id).spine_id;
            apron|=id==80;berth|=id==82;access|=id==84;
        }
        berth_joined+=berth && access;
        apron_joined+=apron && access;
    }
    REQUIRE(berth_joined==1);
    REQUIRE(apron_joined==0);
    REQUIRE_NEAR(end.x,-1981.f,.001f);
    REQUIRE_NEAR(end.z,-620.f,.001f);
    REQUIRE_NEAR(access_road->path[access_road->count-2].x,-1979.f,.001f);
    REQUIRE_NEAR(access_road->path[access_road->count-2].z,end.z,.001f);

    const auto launch=city::bake_marina_access();
    REQUIRE(launch.ramp_quads.size()==2 && launch.surfaces.triangles.size()==4);
    int lots=0,stripes=0,stops=0,ramps=0,aprons=0;
    for(const auto& p:launch.parts) {
        lots+=std::strcmp(p.name,"marina parking lot")==0;
        stripes+=std::strcmp(p.name,"marina parking stripe")==0;
        stops+=std::strcmp(p.name,"marina parking stop")==0;
        ramps+=std::strcmp(p.name,"marina boat ramp")==0;
        aprons+=std::strcmp(p.name,"marina parking entrance apron")==0;
    }
    REQUIRE(lots==1 && stripes==18 && stops==16 && ramps==2 && aprons==0);
    for(const auto& p:launch.parts) {
        if(std::strcmp(p.name,"marina parking stop")==0) {
            REQUIRE(p.centre.z==city::kMarinaNorthStopZ ||
                    p.centre.z==city::kMarinaSouthStopZ);
            REQUIRE(p.centre.x<city::kMarinaParkingLastStripeX);
        }
        if(std::strcmp(p.name,"marina parking stripe")==0)
            REQUIRE(p.centre.x<=city::kMarinaParkingLastStripeX);
    }
    const glm::vec2 road_end{end.x-city::kMarlinDockSite.origin.x,
                             end.z-city::kMarlinDockSite.origin.z};
    REQUIRE_NEAR(road_end.x,city::kMarinaParkingEntrance.x-
                            city::kMarinaRoadIntoLotM,.001f);
    REQUIRE_NEAR(road_end.y,city::kMarinaParkingEntrance.z,.001f);
    const float road_facing_edge=city::kMarinaParkingCentre.x+
        city::kMarinaParkingWidth*.5f;
    REQUIRE_NEAR(road_facing_edge,city::kMarinaParkingEntrance.x,.001f);
    REQUIRE_NEAR(city::kMarinaEntranceApproachLocalX-road_facing_edge,10.f,.001f);
    REQUIRE_NEAR(city::kMarinaEntranceApproachLocalX-road_facing_edge-
                     access_road->width_m*.5f,6.f,.001f);
    REQUIRE_NEAR(city::kMarinaParkingNorthEdgeLocalZ-
                     city::kMarinaEntranceApproachLocalZ,10.f,.001f);
    REQUIRE_NEAR(city::kMarinaParkingNorthEdgeLocalZ-
                     city::kMarinaEntranceApproachLocalZ-
                     access_road->width_m*.5f,6.f,.001f);
    // The dock/ramp edge did not move while the road-facing edge retreated.
    REQUIRE_NEAR(city::kMarinaParkingCentre.x-city::kMarinaParkingWidth*.5f,
                 city::kMarinaRampEastX,.001f);

    // Drive the authored approach, kerb cut and centre aisle using the same
    // road collision, static obstacles and Classic GTA player tuning as the
    // game. The ten-metre setback only counts if a car can use the bend.
    const auto road_bake=bake_ribbons(road_graph,road_ground.sampler());
    const float parking_edge_world=city::kMarlinDockSite.origin.x+
        city::kMarinaParkingEntrance.x;
    const float entrance_world_z=city::kMarlinDockSite.origin.z+
        city::kMarinaParkingEntrance.z;
    auto drive_collision=build_road_collision(road_bake);
    // Boatworks Road itself crosses the exact lot edge at its full width and
    // carries on two metres beneath the parking surface. There is no separate
    // patch mesh left to hide an undersized road cap.
    for(float side:{-3.5f,0.f,3.5f}) {
        const glm::vec2 seam{parking_edge_world,entrance_world_z+side};
        REQUIRE(mesh_covers(road_bake.layer(RoadLayer::Carriageway),seam));
        REQUIRE(collision_covers(drive_collision,seam));
    }
    city::append_marina_access_collision(drive_collision,launch);
    TerrainCollider drive_collider{city::kMapSeed};
    drive_collider.set_road_collision(drive_collision);
    drive_collider.add_static_ground_rect(
        {city::kMarlinDockSite.origin.x+city::kMarinaParkingCentre.x,
         city::kMarlinDockSite.origin.z+city::kMarinaParkingCentre.z},
        city::kMarinaParkingTop,
        {city::kMarinaParkingWidth*.5f,city::kMarinaParkingDepth*.5f},0,Surface::Rock);
    for(const auto& p:launch.parts) if(p.solid) {
        drive_collider.add_static_oriented_box(
            {city::kMarlinDockSite.origin.x+p.centre.x,
             p.bottom_m+p.height_m*.5f,
             city::kMarlinDockSite.origin.z+p.centre.z},
            {p.width_m*.5f,p.height_m*.5f,p.depth_m*.5f},glm::radians(p.yaw_deg));
    }
    std::vector<glm::vec2> drive_path;
    const auto add_drive_segment=[&](glm::vec2 a,glm::vec2 b) {
        const int count=std::max(1,static_cast<int>(std::ceil(glm::length(b-a)*2.f)));
        for(int i=0;i<count;++i)
            drive_path.push_back(a+(b-a)*static_cast<float>(i)/static_cast<float>(count));
    };
    for(int i=0;i+1<access_road->count;++i)
        add_drive_segment({access_road->path[i].x,access_road->path[i].z},
                          {access_road->path[i+1].x,access_road->path[i+1].z});
    add_drive_segment({end.x,end.z},{-1995.f,-620.f});
    drive_path.push_back({-1995.f,-620.f});
    const auto initial=glm::normalize(drive_path[1]-drive_path[0]);
    const auto drive_approach=[&](VehicleTuning tuning,const char* name) {
        tuning.service_brake_grip_boost=1.f;
        auto vehicle=spawn_vehicle(tuning,drive_collider,drive_path.front().x,
                                   drive_path.front().y,
                                   std::atan2(-initial.x,-initial.y));
        std::size_t nearest=0;
        float peak_speed=0.f;
        for(int tick=0;tick<12000 && nearest+2<drive_path.size();++tick) {
            const glm::vec2 here{vehicle.position.x,vehicle.position.z};
            while(nearest+1<drive_path.size() &&
                  glm::length(drive_path[nearest+1]-here)<
                      glm::length(drive_path[nearest]-here))
                ++nearest;
            std::size_t look=nearest;
            while(look+1<drive_path.size() &&
                  glm::length(drive_path[look]-here)<4.f) ++look;
            const auto local=glm::inverse(vehicle.orientation)*
                glm::vec3{drive_path[look].x-here.x,0,drive_path[look].y-here.y};
            const float speed=glm::length(glm::vec2{vehicle.velocity.x,
                                                    vehicle.velocity.z});
            InputFrame input;
            input.steer=glm::clamp(
                std::atan2(4.f*tuning.half_wheelbase*local.x,
                           local.x*local.x+local.z*local.z)/tuning.max_steer,-1.f,1.f);
            input.throttle=speed<2.5f?.26f:.02f;
            input.brake=speed>2.9f?.18f:0.f;
            vehicle=step_vehicle(vehicle,tuning,input,drive_collider,1.f/120.f);
            peak_speed=std::max(peak_speed,speed);
            if(vehicle.impact_count) break;
        }
        std::printf("Boatworks %s: waypoint %zu/%zu at %.2f %.2f, peak %.2f m/s, impacts %u\n",
                    name,nearest,drive_path.size(),vehicle.position.x,vehicle.position.z,
                    peak_speed,vehicle.impact_count);
        REQUIRE(nearest+3>=drive_path.size());
        REQUIRE(vehicle.position.x<-1992.f);
        REQUIRE(std::fabs(vehicle.position.z+620.f)<2.f);
        REQUIRE(vehicle.impact_count==0u);
    };
    drive_approach(player_vehicle_tuning(DrivingMechanicsStyle::ClassicGta),"car");
    drive_approach(player_model_tuning(DrivingMechanicsStyle::ClassicGta,
                                       PlayerCarId::HarrowHauler),"truck");

    TerrainCollider launch_collider{city::kMapSeed};
    launch_collider.set_road_collision(launch.surfaces);
    launch_collider.add_static_ground_rect(
        {city::kMarlinDockSite.origin.x+city::kMarinaParkingCentre.x,
         city::kMarlinDockSite.origin.z+city::kMarinaParkingCentre.z},
        city::kMarinaParkingTop,
        {city::kMarinaParkingWidth*.5f,city::kMarinaParkingDepth*.5f},0,Surface::Rock);
    const float run=city::kMarinaRampEastX-city::kMarinaRampWestX;
    const float rise=city::kMarinaParkingTop-city::kMarinaRampWaterTop;
    REQUIRE(rise/run<.13f);
    for(float x=city::kMarinaRampWestX+.25f;x<city::kMarinaRampEastX;x+=1.f) {
        const float expected=city::marina_ramp_height(x);
        const auto hit=launch_collider.probe_down(
            {city::kMarlinDockSite.origin.x+x,expected+1.f,
             city::kMarlinDockSite.origin.z+city::kMarinaRampZ},2.f);
        REQUIRE(hit.hit && hit.road);
        REQUIRE_NEAR(hit.point.y,expected,.002);
        REQUIRE(hit.normal.y>.99f);
    }
    const auto clear=[&](float x,float z) {
        glm::vec3 p{city::kMarlinDockSite.origin.x+x,city::kMarinaDeckTop,
                          city::kMarlinDockSite.origin.z+z};
        // The shore end is intentionally buried into the sloping bank.
        p.y=std::max(p.y,collider.height(p.x,p.z));
        REQUIRE(character_position_clear(collider,p,CharacterTuning{}));
        const auto ground=collider.probe_down(p+glm::vec3{0,.2f,0},.25f);
        REQUIRE(ground.hit);REQUIRE_NEAR(ground.point.y,p.y,.025);
    };
    // Main pier, finger aisles and a dogleg past the waiting bench to the
    // actual open shed doorway, with support under every sampled step.
    for(float x=-22.8f;x<5.1f;x+=.3f)clear(x,0);
    for(float x:{-21.f,-13.f})for(float z=1.5f;z<9.5f;z+=.2f)clear(x-.50f,z);
    for(float z=1.3f;z<4.8f;z+=.2f)clear(.5f,z);
    for(float x=.5f;x<2.1f;x+=.2f)clear(x,4.8f);
    for(float z=4.8f;z<8.8f;z+=.2f)clear(2,z);
    const auto& devon=city::kAuthoredStaff[city::kDevonStaffIndex];
    REQUIRE(std::strcmp(devon.name,"Devon")==0);
    REQUIRE(devon.site==&city::kMarlinDockSite);
    REQUIRE(devon.civilian_model<8u);
    REQUIRE_NEAR(city::devon_position().x,city::kMarlinDockSite.origin.x+1.5f,.001f);
    REQUIRE_NEAR(city::devon_position().y,city::kMarinaDeckTop,.001f);
    REQUIRE_NEAR(city::devon_position().z,city::kMarlinDockSite.origin.z+8.f,.001f);
    clear(devon.position.x,devon.position.z);
    // Johnny can stand in the clear customer aisle for the handoff.
    clear(2.f,7.3f);
    REQUIRE(delivery_contact({city::kMarlinDockSite.origin.x+2.f,
                              city::kMarinaDeckTop,
                              city::kMarlinDockSite.origin.z+7.3f},true));
    // Exercise movement across the land/deck transition, not only stationary
    // probes. Starting on shore also matches the real approach to the marina.
    auto walker=spawn_character(collider,city::kMarlinDockSite.origin.x+8.f,
                                city::kMarlinDockSite.origin.z);
    InputFrame walk{};walk.steer=-1.f;
    for(int tick=0;tick<1530;++tick) {
        walker=step_character(walker,CharacterTuning{},walk,collider,1.f/120.f);
        REQUIRE(walker.position.y>=city::kMarinaDeckTop-.025f);
    }
    REQUIRE(walker.position.x<city::kMarlinDockSite.origin.x-21.5f);
    REQUIRE_NEAR(walker.position.y,city::kMarinaDeckTop,.025);
    std::printf("marina: %zu parts, %d planks, %d lamps; shore/pier/finger/shed paths clear\n",
                parts.size(),planks,lights);
    return apricot_test::done("marina_tests");
}
