#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "city/map.h"
#include "city/start_area.h"
#include "game/character.h"
#include "test_assert.h"

using namespace apricot;

namespace {
constexpr float kDt=1.f/120.f;

glm::vec3 world_point(float x,float y,float z) {
    const auto& site=city::kFastFoodSite;
    return {site.origin.x+site.cos_yaw*x+site.sin_yaw*z,
            site.ground_m+y,
            site.origin.z-site.sin_yaw*x+site.cos_yaw*z};
}
glm::vec3 local_point(glm::vec3 world) {
    const auto& site=city::kFastFoodSite;
    const glm::vec3 d=world-glm::vec3{site.origin.x,site.ground_m,site.origin.z};
    return {site.cos_yaw*d.x-site.sin_yaw*d.z,d.y,
            site.sin_yaw*d.x+site.cos_yaw*d.z};
}
TerrainCollider restaurant_collider() {
    TerrainCollider collider{city::kMapSeed};
    const auto& site=city::kFastFoodSite;
    const float site_yaw=std::atan2(site.sin_yaw,site.cos_yaw);
    // Match World registration: exact oriented baked solids, then explicit
    // support planes for the visible lot/walk/floor. No synthetic flat world
    // or simplified wall geometry can hide an actual threshold regression.
    for(const auto& p:city::bake_building(city::kFastFoodPlan)) {
        const glm::vec3 centre=world_point(p.centre.x,p.bottom_m+p.height_m*.5f,p.centre.z);
        const float yaw=site_yaw+glm::radians(p.yaw_deg);
        if(p.solid) {
            REQUIRE(p.pitch_deg==0.f && p.roll_deg==0.f);
            collider.add_static_oriented_box(centre,
                {p.width_m*.5f,p.height_m*.5f,p.depth_m*.5f},yaw);
        }
        const bool plot_paving=p.finish==city::StartFinish::Asphalt &&
            std::strstr(p.name," lot") && p.bottom_m>=0.f && p.height_m>0.f;
        if(plot_paving || std::strcmp(p.name,"quickbite front pedestrian walk")==0 ||
           std::strcmp(p.name,"quickbite interior floor")==0) {
            collider.add_static_ground_rect({centre.x,centre.z},
                site.ground_m+p.bottom_m+p.height_m,
                {p.width_m*.5f,p.depth_m*.5f},yaw,Surface::Rock);
        }
    }
    return collider;
}
PlayerCharacterState advance_toward(PlayerCharacterState state,const TerrainCollider& collider,
                                     glm::vec3 goal,int steps) {
    const CharacterTuning tuning;
    for(int i=0;i<steps;++i) {
        const glm::vec2 d{goal.x-state.position.x,goal.z-state.position.z};
        const float distance=glm::length(d);
        if(distance<.018f) break;
        InputFrame input;
        input.look_dx=std::atan2(d.x,-d.y)-state.view_yaw;
        input.throttle=std::min(1.f,distance/(tuning.walk_speed_mps*kDt));
        const auto before=state;
        state=step_character(state,tuning,input,collider,kDt);
        REQUIRE(std::isfinite(state.position.x) && std::isfinite(state.position.y) &&
                std::isfinite(state.position.z));
        REQUIRE(std::fabs(state.position.y-before.position.y)<=tuning.max_step_m+.001f);
        REQUIRE(character_position_clear(collider,state.position,tuning));
    }
    return state;
}
void walk_through_open_entrance_and_stop_at_counter() {
    const auto collider=restaurant_collider();
    const auto outside=world_point(-5.f,0.f,-10.f);
    auto state=spawn_character(collider,outside.x,outside.z);
    REQUIRE_NEAR(local_point(state.position).y,.10f,1e-4f);
    state=advance_toward(state,collider,world_point(-5.f,0.f,-6.4f),400);
    REQUIRE_NEAR(local_point(state.position).z,-6.4f,.03f);
    REQUIRE_NEAR(local_point(state.position).y,.20f,1e-4f);
    // From the raised front walk, pass both jambs and the exact wall opening.
    state=advance_toward(state,collider,world_point(-5.f,0.f,-3.6f),400);
    REQUIRE_NEAR(local_point(state.position).z,-3.6f,.03f);
    REQUIRE_NEAR(local_point(state.position).y,.20f,1e-4f);
    state=advance_toward(state,collider,world_point(-5.f,0.f,4.3f),800);
    REQUIRE_NEAR(local_point(state.position).x,-5.f,.035f);
    REQUIRE_NEAR(local_point(state.position).z,4.3f,.035f);
    REQUIRE_NEAR(local_point(state.position).y,.20f,1e-4f);
    REQUIRE(state.distance_walked_m>14.2f && state.distance_walked_m<14.5f);
    // Normal input into the counter must stop at its physical face, rather
    // than passing through it or snapping the player onto its worktop.
    state=advance_toward(state,collider,world_point(-5.f,0.f,6.5f),240);
    const auto stopped=local_point(state.position);
    REQUIRE(stopped.z>4.3f && stopped.z<4.60f);
    REQUIRE_NEAR(stopped.y,.20f,1e-4f);
    apricot_test::pass("character walks lot, threshold and open doorway to the real counter");
}
void staff_doors_connect_counter_pickup_and_screened_kitchen() {
    const auto collider=restaurant_collider();
    const auto begin=world_point(-5,0,3.4f);
    auto state=spawn_character(collider,begin.x,begin.z);
    const glm::vec2 stops[]={{7.3f,3.4f},{7.3f,6.4f},{0,6.4f},
        {7.3f,6.4f},{7.3f,8.15f},{-16.7f,8.15f}};
    for(const auto stop:stops) {
        state=advance_toward(state,collider,world_point(stop.x,0,stop.y),1500);
        const auto local=local_point(state.position);
        REQUIRE_NEAR(local.x,stop.x,.04f);
        REQUIRE_NEAR(local.z,stop.y,.04f);
        REQUIRE_NEAR(local.y,.20f,1e-4f);
    }
    // Kitchen access is through the actual east staff opening. The former
    // freely traversable boundary now physically stops an ordinary character.
    state=advance_toward(state,collider,world_point(-16.7f,0,6),500);
    REQUIRE(local_point(state.position).z>7.65f);
    apricot_test::pass("staff doors connect service aisle, drive-through pickup and separated kitchen");
}
void opaque_partition_limits_kitchen_sightlines() {
    const auto collider=restaurant_collider();
    const auto dir=glm::normalize(world_point(0,0,1)-world_point(0,0,0));
    for(float x:{-18.f,-13.f,-7.f,2.f,4.f,9.f}) {
        const auto hit=collider.raycast(world_point(x,1.8f,6.85f),dir,.8f);
        REQUIRE(hit.hit && hit.prop);
        REQUIRE_NEAR(local_point(hit.point).z,7.06f,.03f);
    }
    // The service pass is a bounded real opening: food-height view can pass,
    // while the upper wall still hides the hood and rear kitchen ceiling.
    REQUIRE(!collider.raycast(world_point(-2,1.7f,6.85f),dir,1.f).hit);
    REQUIRE(collider.raycast(world_point(-2,2.5f,6.85f),dir,1.f).hit);
    apricot_test::pass("opaque kitchen boundary blocks views except the bounded food pass");
}
void kitchen_equipment_follows_walls_and_keeps_working_aisle() {
    const auto parts=city::bake_building(city::kFastFoodPlan);
    const auto named=[&](const char* name)->const city::BuildingPiece& {
        const auto it=std::find_if(parts.begin(),parts.end(),[&](const auto& p){return std::strcmp(p.name,name)==0;});
        REQUIRE(it!=parts.end());return *it;
    };
    // The rear wall's interior face is z10.85; the east wall's is x9.85.
    // Cabinets sit at the wall with only a small worktop overhang, rather than
    // leaving an inaccessible strip behind equipment in the staff aisle.
    for(const char* name:{"quickbite interior kitchen dry storage base",
        "quickbite interior kitchen prep cabinet","quickbite interior kitchen prep worktop",
        "quickbite interior kitchen griddle base","quickbite interior kitchen fryer base",
        "quickbite interior kitchen fridge","quickbite interior kitchen extraction hood"}) {
        const auto& p=named(name);
        const float gap=10.85f-(p.centre.z+p.depth_m*.5f);
        REQUIRE_MSG(gap>=-.002f && gap<=.061f,"rear fixture misses wall",name);
        REQUIRE(p.centre.z-p.depth_m*.5f>=9.09f);
    }
    for(const char* name:{"quickbite interior kitchen pickup cabinet",
        "quickbite interior kitchen pickup worktop","quickbite interior kitchen sink cabinet"}) {
        const auto& p=named(name);
        const float gap=9.85f-(p.centre.x+p.width_m*.5f);
        REQUIRE_MSG(gap>=-.002f && gap<=.061f,"east fixture misses wall",name);
    }
    const auto collider=restaurant_collider();
    const auto begin=world_point(7.3f,0,8.15f);
    auto state=spawn_character(collider,begin.x,begin.z);
    // Visit the fridge, prep, cookline and dry-storage work fronts and return
    // along a separate central lane. Both levels stay clear of every solid.
    for(const glm::vec2 target:{glm::vec2{6.4f,9.f},{-1.f,9.f},{-9.5f,9.f},
        {-13.4f,9.f},{-18.1f,9.f},{-18.1f,8.1f},{7.3f,8.1f},{7.3f,6.4f}}) {
        state=advance_toward(state,collider,world_point(target.x,0,target.y),1800);
        const auto p=local_point(state.position);
        REQUIRE_NEAR(p.x,target.x,.04f);REQUIRE_NEAR(p.z,target.y,.04f);
        REQUIRE_NEAR(p.y,.20f,1e-4f);
    }
    const auto& oven=named("quickbite interior kitchen oven door");
    REQUIRE(oven.centre.z<named("quickbite interior kitchen griddle base").centre.z);
    apricot_test::pass("kitchen equipment meets rear/east walls and staff can reach every work front");
}
void closed_door_blocks_the_same_character_route() {
    auto collider=restaurant_collider();
    const auto& site=city::kFastFoodSite;
    // A real door slab is the counterexample: the old closed opening cannot
    // accidentally pass this test because the requested target is indoors.
    collider.add_static_oriented_box(world_point(-5.f,1.425f,-5.f),
        {1.25f,1.425f,.08f},std::atan2(site.sin_yaw,site.cos_yaw));
    const auto outside=world_point(-5.f,0.f,-10.f);
    auto state=spawn_character(collider,outside.x,outside.z);
    state=advance_toward(state,collider,world_point(-5.f,0.f,4.3f),1000);
    REQUIRE(local_point(state.position).z<-5.30f);
    REQUIRE_NEAR(local_point(state.position).y,.20f,1e-4f);
    apricot_test::pass("sealing the doorway blocks the same live movement sequence");
}
}

int main() {
    walk_through_open_entrance_and_stop_at_counter();
    closed_door_blocks_the_same_character_route();
    staff_doors_connect_counter_pickup_and_screened_kitchen();
    opaque_partition_limits_kitchen_sightlines();
    kitchen_equipment_follows_walls_and_keeps_working_aisle();
    return apricot_test::done("quickbite_interior_tests");
}
