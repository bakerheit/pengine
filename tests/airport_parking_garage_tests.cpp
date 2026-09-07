#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include "city/airport_parking_garage.h"
#include "physics/vehicle.h"
#include "game/character.h"
#include "test_assert.h"

using namespace apricot;
namespace {
constexpr float dt=1.f/120.f;
TerrainCollider make_collider(const city::AirportGarageBake& garage) {
    TerrainCollider c{city::kMapSeed};
    c.set_road_collision(garage.surfaces);
    auto parts=city::bake_airport();
    parts.erase(std::remove_if(parts.begin(),parts.end(),city::airport_garage_replaces),parts.end());
    parts.insert(parts.end(),garage.parts.begin(),garage.parts.end());
    for(const auto& p:parts) {
        const auto centre=city::airport_garage_world({p.centre.x,p.bottom_m+p.height_m*.5f,p.centre.z});
        if(p.solid) c.add_static_oriented_box(centre,{p.width_m*.5f,p.height_m*.5f,p.depth_m*.5f},glm::radians(p.yaw_deg));
        if(std::strstr(p.name,"terminal parking walk"))
            c.add_static_ground_rect({centre.x,centre.z},city::kAirportSite.ground_m+p.bottom_m+p.height_m,
                                     {p.width_m*.5f,p.depth_m*.5f},0.f);
    }
    return c;
}
void support_and_clearance() {
    const auto g=city::bake_airport_parking_garage();
    auto c=make_collider(g);
    REQUIRE(g.ramp_quads.size()==100u);
    for(const auto& tri:g.surfaces.triangles) REQUIRE(tri.geom.normal.y>.995f);
    for(int level=0;level<3;++level) {
        const float y=.13f+static_cast<float>(level)*4.f;
        auto hit=c.probe_down(city::airport_garage_world({-60,y+1.f,232}),2.f);
        REQUIRE(hit.hit);REQUIRE_NEAR(hit.point.y,6.f+y,1e-4f);
        for(const auto& box:c.static_boxes())
            REQUIRE(!box.bounds.contains(city::airport_garage_world({-60,y+3.6f,232})));
    }
    for(int floor=0;floor<2;++floor) for(int i=0;i<=100;++i) {
        const float x=-95.f+static_cast<float>(i)*.5f;
        const float y=city::airport_garage_ramp_height(x,floor);
        auto hit=c.probe_down(city::airport_garage_world({x,y+.5f,196}),1.f);
        REQUIRE(hit.hit);REQUIRE_NEAR(hit.point.y,6.f+y,.0021f);
    }
    apricot_test::pass("stacked decks and exact ramp triangles select the correct floor");
}
void drive_each_ramp_both_ways() {
    const auto g=city::bake_airport_parking_garage();
    const auto c=make_collider(g);
    VehicleTuning tuning;tuning.service_brake_grip_boost=1.f;
    for(int floor=0;floor<2;++floor) for(int direction:{1,-1}) {
        const float start_x=direction==1?-101.f:-39.f;
        const float start_y=.13f+static_cast<float>(floor+(direction==-1?1:0))*4.f;
        const float lane_z=direction==1?200.f:196.f;
        const auto start=city::airport_garage_world({start_x,start_y,lane_z});
        auto s=spawn_vehicle(tuning,c,start.x,start.z,direction==1?-glm::half_pi<float>():glm::half_pi<float>());
        // spawn_vehicle starts on terrain; pick the requested
        // deck once at setup. Every subsequent height change is actual physics.
        s.position.y+=start.y-c.height(start.x,start.z);
        auto clone=s;
        const float initial_y=s.position.y;
        float peak_speed=0.f;
        for(int i=0;i<7000;++i) {
            const float speed=glm::length(glm::vec2{s.velocity.x,s.velocity.z});
            InputFrame input;
            input.throttle=speed<3.2f?.30f:.05f;
            input.brake=speed>3.6f?.20f:0.f;
            const auto before=s;
            s=step_vehicle(s,tuning,input,c,dt);
            clone=step_vehicle(clone,tuning,input,c,dt);
            REQUIRE(s.position==clone.position);REQUIRE(s.velocity==clone.velocity);
            REQUIRE(glm::length(s.position-before.position)<.15f);
            REQUIRE(std::fabs(s.position.z-start.z)<.55f);
            peak_speed=std::max(peak_speed,speed);
            const float local_x=s.position.x-city::kAirportSite.origin.x;
            if((direction==1 && local_x>-39.f)||(direction==-1 && local_x<-101.f)) break;
        }
        const float travel=static_cast<float>(direction)*(s.position.x-start.x);
        std::printf("ramp %d direction %d: travel %.2fm rise %.3fm max %.2fm/s impacts %u\n",
                    floor,direction,travel,s.position.y-initial_y,peak_speed,s.impact_count);
        REQUIRE(travel>61.f);
        REQUIRE_NEAR(s.position.y-initial_y,static_cast<float>(direction)*4.f,.18f);
        REQUIRE(s.impact_count==0u);
    }
    apricot_test::pass("real vehicle physics drives up and down both ramps without jumps or damage");
}
void lower_aisle_and_existing_crossings_stay_open() {
    const auto g=city::bake_airport_parking_garage();
    const auto c=make_collider(g);
    for(float x:{-110.f,-35.f,40.f}) for(float z=189.f;z<=240.f;z+=.5f) {
        const auto point=city::airport_garage_world({x,.3f,z});
        REQUIRE(character_position_clear(c,point,CharacterTuning{}));
    }
    VehicleTuning tuning;tuning.service_brake_grip_boost=1.f;
    auto s=spawn_vehicle(tuning,c,12.f,2355.f,-glm::half_pi<float>());
    s.position.y+=DRAPE_EPS_M;
    for(int i=0;i<15000 && s.position.x<218.f;++i) {
        const float speed=glm::length(glm::vec2{s.velocity.x,s.velocity.z});
        InputFrame input;input.throttle=speed<3.2f?.3f:.04f;
        input.brake=speed>3.6f?.2f:0.f;
        s=step_vehicle(s,tuning,input,c,dt);
        REQUIRE(s.position.y<7.2f);REQUIRE(s.impact_count==0u);
    }
    REQUIRE(s.position.x>217.f);
    apricot_test::pass("ground-level aisle stays drivable under both decks and all three terminal walks stay clear");
}

void a_car_can_return_between_ramps() {
    const auto c=make_collider(city::bake_airport_parking_garage());
    VehicleTuning tuning;tuning.service_brake_grip_boost=1.f;
    std::vector<glm::vec2> path;
    auto line=[&](glm::vec2 a,glm::vec2 b) {
        const int count=static_cast<int>(std::ceil(glm::length(b-a)));
        for(int i=0;i<count;++i) path.push_back(a+(b-a)*static_cast<float>(i)/static_cast<float>(count));
    };
    auto arc=[&](glm::vec2 centre,float from,float to) {
        for(int i=0;i<20;++i) {
            const float a=glm::radians(from+(to-from)*static_cast<float>(i)/20.f);
            path.push_back(centre+10.f*glm::vec2{std::cos(a),std::sin(a)});
        }
    };
    // One continuous real-physics trip: first ramp, broad return around the
    // intermediate parking aisle, then second ramp. No resets between floors.
    line({-103,200},{-29,200});arc({-29,210},-90,0);
    line({-19,210},{-19,218});arc({-29,218},0,90);
    line({-29,228},{-103,228});arc({-103,218},90,180);
    line({-113,218},{-113,210});arc({-103,210},180,270);
    line({-103,200},{-36,200});path.push_back({-36,200});
    const auto start=city::airport_garage_world({-103,.13f,200});
    auto s=spawn_vehicle(tuning,c,start.x,start.z,-glm::half_pi<float>());
    s.position.y+=.13f;
    std::size_t nearest=0;
    for(int step=0;step<24000;++step) {
        const glm::vec2 p{s.position.x-150.f,s.position.z-2140.f};
        while(nearest+1<path.size() && glm::length(path[nearest+1]-p)<glm::length(path[nearest]-p)) ++nearest;
        if(nearest+2>=path.size()) break;
        std::size_t look=nearest;
        while(look+1<path.size() && glm::length(path[look]-p)<5.f) ++look;
        const auto d=glm::inverse(s.orientation)*glm::vec3{path[look].x-p.x,0,path[look].y-p.y};
        const float speed=glm::length(glm::vec2{s.velocity.x,s.velocity.z});
        InputFrame input;
        input.steer=glm::clamp(std::atan2(4.f*tuning.half_wheelbase*d.x,d.x*d.x+d.z*d.z)/tuning.max_steer,-1.f,1.f);
        input.throttle=speed<3.f?.28f:.03f;input.brake=speed>3.4f?.2f:0.f;
        s=step_vehicle(s,tuning,input,c,dt);
        if(s.impact_count) break;
    }
    std::printf("connected trip waypoint %zu/%zu at %.2f %.2f %.2f impacts %u\n",nearest,path.size(),s.position.x-150.f,s.position.y-6.f,s.position.z-2140.f,s.impact_count);
    REQUIRE(nearest+3>=path.size());REQUIRE(s.impact_count==0u);
    REQUIRE_NEAR(s.position.y-6.f-static_ride_height(tuning),8.13f,.12f);
    apricot_test::pass("continuous drive links both ramps through 10m-radius turns and the parking aisle");
}

void public_stairs_connect_all_levels() {
    const auto c=make_collider(city::bake_airport_parking_garage());
    const CharacterTuning tuning;
    auto walk=[&](PlayerCharacterState state,glm::vec3 goal) {
        for(int i=0;i<4000;++i) {
            const glm::vec2 d{goal.x-state.position.x,goal.z-state.position.z};
            const float distance=glm::length(d);
            if(distance<.03f) break;
            InputFrame input;
            input.look_dx=std::atan2(d.x,-d.y)-state.view_yaw;
            input.throttle=std::min(1.f,distance/(tuning.walk_speed_mps*dt));
            state=step_character(state,tuning,input,c,dt);
        }
        REQUIRE(glm::length(glm::vec2{state.position.x-goal.x,state.position.z-goal.z})<.05f);
        REQUIRE_NEAR(state.position.y,goal.y,.035f);
        return state;
    };
    auto state=spawn_character(c,32.f,2330.7f);
    state.position.y=6.13f;
    for(int floor=0;floor<2;++floor) {
        const float y=.13f+static_cast<float>(floor+1)*4.f;
        state=walk(state,city::airport_garage_world({-118,y,206}));
        if(floor==0) {
            state=walk(state,city::airport_garage_world({-123,y,206}));
            state=walk(state,city::airport_garage_world({-123,y,190.7f}));
            state=walk(state,city::airport_garage_world({-118,y,190.7f}));
        }
    }
    apricot_test::pass("actual character climbs both stair flights and crosses the intermediate landing");
}

}
int main(){support_and_clearance();lower_aisle_and_existing_crossings_stay_open();drive_each_ramp_both_ways();public_stairs_connect_all_levels();a_car_can_return_between_ramps();}
