#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "city/quickbite_doors.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "test_assert.h"

using namespace apricot;

namespace {
glm::vec2 world(glm::vec2 local) {
    const auto& site=city::kFastFoodSite;
    return {site.origin.x+site.cos_yaw*local.x+site.sin_yaw*local.y,
            site.origin.z-site.sin_yaw*local.x+site.cos_yaw*local.y};
}
glm::vec2 local(glm::vec3 point) {
    const auto& site=city::kFastFoodSite;
    const glm::vec2 d{point.x-site.origin.x,point.z-site.origin.z};
    return {site.cos_yaw*d.x-site.sin_yaw*d.y,
            site.sin_yaw*d.x+site.cos_yaw*d.y};
}
struct DoorFixture {
    TerrainCollider collider{city::kMapSeed};
    std::vector<city::QuickbiteDoor> doors=city::quickbite_doors();
    std::vector<HouseDoorState> states=std::vector<HouseDoorState>(doors.size());
    std::vector<std::size_t> ids;
    CharacterTuning tuning;
    DoorFixture() {
        const auto& site=city::kFastFoodSite;
        const float yaw=std::atan2(site.sin_yaw,site.cos_yaw);
        for(const auto& part:city::bake_building(city::kFastFoodPlan)) if(part.solid) {
            const auto p=world({part.centre.x,part.centre.z});
            collider.add_static_oriented_box(
                {p.x,site.ground_m+part.bottom_m+part.height_m*.5f,p.y},
                {part.width_m*.5f,part.height_m*.5f,part.depth_m*.5f},
                yaw+glm::radians(part.yaw_deg));
        }
        collider.add_static_ground_rect(world({-5,-5}),site.ground_m+.2f,
                                        {25,14},yaw,Surface::Rock);
        for(const auto& door:doors) {
            const auto& d=door.physics;
            ids.push_back(collider.add_kinematic_oriented_box(
                house_door_centre(d,0),{d.width*.5f,d.height*.5f,d.thickness*.5f},
                d.closed_yaw));
        }
    }
    void update(PlayerCharacterState& actor,const InputFrame& input) {
        const auto velocity=house_door_walk_velocity(actor,tuning,input);
        for(std::size_t i=0;i<doors.size();++i) {
            states[i]=step_house_door(doors[i].physics,states[i],&actor,tuning,
                                      velocity,1.f/120.f);
            const auto& d=doors[i].physics;
            collider.set_kinematic_oriented_box(ids[i],
                house_door_centre(d,states[i].angle),
                {d.width*.5f,d.height*.5f,d.thickness*.5f},
                d.closed_yaw+states[i].angle);
        }
        actor=step_character(actor,tuning,input,collider,1.f/120.f);
    }
};
}

int main() {
    const auto parts=city::bake_building(city::kFastFoodPlan);
    int stripes=0,stops=0;
    std::vector<float> stripe_x,stop_x;
    const VehicleTuning car;
    for(const auto& part:parts) {
        if(std::strstr(part.name,"restaurant parking stripe")) {
            ++stripes;
            stripe_x.push_back(part.centre.x);
            REQUIRE_NEAR(part.centre.z,city::kQuickbiteParkingStripeZ,.0001f);
            REQUIRE_NEAR(part.depth_m,5.f,.0001f);
        }
        if(std::strstr(part.name,"restaurant parking stop")) {
            ++stops;
            stop_x.push_back(part.centre.x);
            REQUIRE(part.solid);
            REQUIRE_NEAR(part.centre.z,city::kQuickbiteParkingStopZ,.0001f);
            REQUIRE(part.centre.x+part.width_m*.5f<-6.25f ||
                    part.centre.x-part.width_m*.5f>-3.75f);
        }
    }
    REQUIRE(stripes==7 && stops==5);
    std::sort(stripe_x.begin(),stripe_x.end());
    std::sort(stop_x.begin(),stop_x.end());
    constexpr std::array<std::pair<std::size_t,std::size_t>,5> bays{{
        {0,1},{1,2},{2,3},{4,5},{5,6}}};
    for(std::size_t i=0;i<bays.size();++i) {
        const auto [left,right]=bays[i];
        REQUIRE_NEAR(stripe_x[right]-stripe_x[left],3.5f,.0001f);
        REQUIRE_NEAR(stop_x[i],(stripe_x[left]+stripe_x[right])*.5f,.0001f);
    }
    // The unmarked center section is the door approach/crossing, not a sixth
    // cramped bay. Keep its cars clear of the pedestrian route.
    REQUIRE(stripe_x[4]-stripe_x[3]>10.f);
    const float bay_head=city::kQuickbiteParkingStripeZ+2.5f;
    REQUIRE_NEAR(city::kQuickbiteFrontWalkSouthEdgeZ-bay_head,1.005f,.001f);
    REQUIRE(city::kQuickbiteFrontWalkSouthEdgeZ-
            (city::kQuickbiteParkingStripeZ+car.car_collision_half_length)>.95f);
    REQUIRE(city::kQuickbiteParkingStopZ>city::kQuickbiteParkingStripeZ);
    REQUIRE(city::kQuickbiteParkingStopZ<city::kQuickbiteFrontWalkSouthEdgeZ);

    const auto doors=city::quickbite_doors();
    REQUIRE(doors.size()==2u);
    REQUIRE(doors[0].physics.width<1.418f && doors[1].physics.width<1.418f);
    REQUIRE_NEAR(doors[0].physics.width,1.18f,.0001f);
    REQUIRE_NEAR(doors[0].physics.pivot_inset,.09f,.0001f);
    const auto west=city::quickbite_door_parts(doors[0],0);
    const auto east=city::quickbite_door_parts(doors[1],0);
    REQUIRE(west.size()==8u && east.size()==8u);
    REQUIRE(west.front().solid && west.front().finish==city::StartFinish::Glass);
    REQUIRE(east.front().solid && east.front().finish==city::StartFinish::Glass);
    const float west_lo=west.front().centre.x-west.front().width_m*.5f;
    const float west_hi=west.front().centre.x+west.front().width_m*.5f;
    const float east_lo=east.front().centre.x-east.front().width_m*.5f;
    const float east_hi=east.front().centre.x+east.front().width_m*.5f;
    REQUIRE_NEAR(west_lo,-6.194f,.002f);
    REQUIRE_NEAR(east_hi,-3.806f,.002f);
    REQUIRE(east_lo-west_hi>.02f && east_lo-west_hi<.04f);

    DoorFixture fixture;
    const auto begin=world({-5,-9});
    auto actor=spawn_character(fixture.collider,begin.x,begin.y);
    const auto goal=world({-5,-2});
    for(int tick=0;tick<2400 && glm::distance(glm::vec2{actor.position.x,actor.position.z},goal)>.05f;++tick) {
        const glm::vec2 delta=goal-glm::vec2{actor.position.x,actor.position.z};
        InputFrame input;
        input.look_dx=std::atan2(delta.x,-delta.y)-actor.view_yaw;
        input.throttle=std::min(1.f,glm::length(delta)/
            (fixture.tuning.walk_speed_mps/120.f));
        fixture.update(actor,input);
    }
    REQUIRE(glm::distance(glm::vec2{actor.position.x,actor.position.z},goal)<.06f);
    REQUIRE(local(actor.position).y>-2.1f);
    REQUIRE(std::abs(fixture.states[0].angle)>.25f ||
            std::abs(fixture.states[1].angle)>.25f);
    for(int tick=0;tick<7200;++tick) fixture.update(actor,{});
    REQUIRE_NEAR(fixture.states[0].angle,0.f,.001f);
    REQUIRE_NEAR(fixture.states[1].angle,0.f,.001f);
    apricot_test::pass("Cloggers bays meet the walk with stops, and both narrow glass leaves use live push-door collision");
    return apricot_test::done("quickbite_door_tests");
}
