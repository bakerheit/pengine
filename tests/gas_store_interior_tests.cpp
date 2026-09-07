#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "city/map.h"
#include "city/start_area.h"
#include "city/authored_staff.h"
#include "game/character.h"
#include "test_assert.h"

using namespace apricot;

namespace {
constexpr float kDt=1.f/120.f;

glm::vec3 world_point(float x,float y,float z) {
    const auto& site=city::kGasStationSite;
    return {site.origin.x+site.cos_yaw*x+site.sin_yaw*z,
            site.ground_m+y,
            site.origin.z-site.sin_yaw*x+site.cos_yaw*z};
}
glm::vec3 local_point(glm::vec3 world) {
    const auto& site=city::kGasStationSite;
    const glm::vec3 d=world-glm::vec3{site.origin.x,site.ground_m,site.origin.z};
    return {site.cos_yaw*d.x-site.sin_yaw*d.z,d.y,
            site.sin_yaw*d.x+site.cos_yaw*d.z};
}
TerrainCollider gas_store_collider() {
    TerrainCollider collider{city::kMapSeed};
    const auto& site=city::kGasStationSite;
    const float site_yaw=std::atan2(site.sin_yaw,site.cos_yaw);
    // Match World registration: exact oriented baked solids, then explicit
    // support planes for the visible lot/walk/floor. No synthetic flat world
    // or simplified wall geometry can hide an actual threshold regression.
    for(const auto& p:city::bake_building(city::kGasStationPlan)) {
        const glm::vec3 centre=world_point(p.centre.x,p.bottom_m+p.height_m*.5f,p.centre.z);
        const float yaw=site_yaw+glm::radians(p.yaw_deg);
        if(p.solid) {
            REQUIRE(p.pitch_deg==0.f && p.roll_deg==0.f);
            collider.add_static_oriented_box(centre,
                {p.width_m*.5f,p.height_m*.5f,p.depth_m*.5f},yaw);
        }
        const bool plot_paving=p.finish==city::StartFinish::Asphalt &&
            std::strstr(p.name," lot") && p.bottom_m>=0.f && p.height_m>0.f;
        if(plot_paving || std::strcmp(p.name,"store interior entrance threshold")==0 ||
           std::strcmp(p.name,"store interior floor")==0) {
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
void walk_store_entrance_aisles_cooler_and_checkout() {
    const auto collider=gas_store_collider();
    const auto outside=world_point(8.8f,0.f,-4.f);
    auto state=spawn_character(collider,outside.x,outside.z);
    REQUIRE_NEAR(local_point(state.position).y,.10f,1e-4f);
    state=advance_toward(state,collider,world_point(8.8f,0.f,-8.6f),500);
    REQUIRE_NEAR(local_point(state.position).z,-8.6f,.035f);
    REQUIRE_NEAR(local_point(state.position).y,.20f,1e-4f);
    // The live controller visits both snack aisles, the rear cooler bank,
    // coffee counter approach and checkout without crossing merchandise.
    const glm::vec2 stops[]={{8.8f,-16.85f},{3.7f,-16.85f},{2.4f,-16.85f},
        {2.4f,-9.1f},{8.8f,-9.1f},{16.f,-9.1f},{16.f,-16.85f},
        {19.7f,-16.85f},{19.7f,-12.2f},{16.f,-12.2f},{16.f,-9.0f},
        {8.8f,-9.0f},{2.4f,-9.0f},{2.4f,-11.35f},{-1.9f,-11.35f}};
    for(const auto stop:stops) {
        state=advance_toward(state,collider,world_point(stop.x,0.f,stop.y),900);
        const auto local=local_point(state.position);
        REQUIRE_NEAR(local.x,stop.x,.04f);
        REQUIRE_NEAR(local.z,stop.y,.04f);
        REQUIRE_NEAR(local.y,.20f,1e-4f);
    }
    state=advance_toward(state,collider,world_point(-1.9f,0.f,-8.65f),240);
    REQUIRE(local_point(state.position).z<-10.9f);
    REQUIRE_NEAR(local_point(state.position).y,.20f,1e-4f);
    apricot_test::pass("live character enters gas store, browses aisles and stops at checkout");
}
void atm_approach_and_clerk_workstation_are_clear() {
    const auto collider=gas_store_collider();
    const auto entry=world_point(8.8f,0,-8.6f);
    auto state=spawn_character(collider,entry.x,entry.z);
    for(const glm::vec2 target: {glm::vec2{16.f,-9.65f},glm::vec2{18.1f,-9.65f}}) {
        state=advance_toward(state,collider,world_point(target.x,0,target.y),900);
        const auto p=local_point(state.position);
        REQUIRE_NEAR(p.x,target.x,.04f);
        REQUIRE_NEAR(p.z,target.y,.04f);
        REQUIRE_NEAR(p.y,.2f,1e-4f);
    }
    // The ATM has a real cabinet, not a face decal that the player can enter.
    state=advance_toward(state,collider,world_point(18.1f,0,-8.2f),400);
    REQUIRE(local_point(state.position).z<-8.9f);
    REQUIRE_NEAR(local_point(state.position).y,.2f,1e-4f);
    const auto staff_approach=world_point(8.8f,0,-8.65f);
    state=spawn_character(collider,staff_approach.x,staff_approach.z);
    const auto& clerk=city::kAuthoredStaff[0];
    REQUIRE_NEAR(clerk.position.x,-1.9f,.001f);
    REQUIRE_NEAR(clerk.position.z,-8.65f,.001f);
    REQUIRE(clerk.facing.z<-.99f);
    for(const glm::vec2 target:{glm::vec2{2.4f,-8.65f},glm::vec2{clerk.position.x,clerk.position.z}}) {
        state=advance_toward(state,collider,world_point(target.x,0,target.y),600);
        const auto p=local_point(state.position);
        REQUIRE_NEAR(p.x,target.x,.04f);
        REQUIRE_NEAR(p.z,target.y,.04f);
        REQUIRE_NEAR(p.y,.2f,1e-4f);
    }
    // The clerk can stand behind the POS, face the customer, and move sideways
    // without overlapping the counter or any new register parts.
    for(float x:{-2.3f,-1.9f,-1.5f})
        REQUIRE(character_position_clear(collider,world_point(x,.2f,-8.65f),CharacterTuning{}));
    apricot_test::pass("ATM customer approach and behind-counter clerk workstation stay walkable");
}
void matching_atm_and_reversed_checkout() {
    const auto parts=city::bake_building(city::kGasStationPlan);
    const auto named=[&](const char* name)->const city::BuildingPiece& {
        const auto it=std::find_if(parts.begin(),parts.end(),[&](const auto& p){return std::strcmp(p.name,name)==0;});
        REQUIRE(it!=parts.end());
        return *it;
    };
    const auto& monitor=named("store interior checkout register screen");
    const auto& display=named("store interior checkout register display");
    const auto& keyboard=named("store interior checkout keyboard");
    const auto& drawer=named("store interior checkout till drawer face");
    const auto& payment=named("store interior checkout payment card slot");
    REQUIRE_NEAR(display.centre.x,city::kAuthoredStaff[0].position.x,.001f);
    REQUIRE(display.centre.z>monitor.centre.z);
    REQUIRE(keyboard.centre.z>monitor.centre.z);
    REQUIRE(drawer.centre.z>-10.f);
    REQUIRE(payment.centre.z<-10.f);
    for(std::size_t i=0;i<city::kGasStoreAtm.size();++i) {
        const auto& gas=city::kGasStoreAtm[i];const auto& bank=city::kBankWestAtm[i];
        REQUIRE_NEAR(gas.width_m,bank.width_m,1e-6f);
        REQUIRE_NEAR(gas.height_m,bank.height_m,1e-6f);
        REQUIRE_NEAR(gas.depth_m,bank.depth_m,1e-6f);
        REQUIRE_NEAR(gas.bottom_m,bank.bottom_m,1e-6f);
        REQUIRE(gas.finish==bank.finish && gas.solid==bank.solid);
        REQUIRE_NEAR(gas.yaw_deg,180.f,1e-6f);
        REQUIRE_NEAR(gas.centre.x-18.1f,-(bank.centre.x+14.f),1e-5f);
        REQUIRE_NEAR(gas.centre.z+8.35f,-(bank.centre.z+6.f),1e-5f);
    }
    REQUIRE(city::kGasStoreAtm[2].centre.z<city::kGasStoreAtm[0].centre.z-.275f);
    apricot_test::pass("window-side clerk faces inward, checkout faces correctly and gas uses exact bank ATM component");
}
void closed_entrance_blocks_the_same_store_route() {
    auto collider=gas_store_collider();
    const auto& site=city::kGasStationSite;
    collider.add_static_oriented_box(world_point(8.8f,1.625f,-7.7f),
        {1.15f,1.625f,.08f},std::atan2(site.sin_yaw,site.cos_yaw));
    const auto outside=world_point(8.8f,0.f,-4.f);
    auto state=spawn_character(collider,outside.x,outside.z);
    state=advance_toward(state,collider,world_point(8.8f,0.f,-12.f),900);
    REQUIRE(local_point(state.position).z>-7.35f);
    apricot_test::pass("sealed gas-store door stops the same real movement sequence");
}
}

int main() {
    matching_atm_and_reversed_checkout();
    walk_store_entrance_aisles_cooler_and_checkout();
    closed_entrance_blocks_the_same_store_route();
    atm_approach_and_clerk_workstation_are_clear();
    return apricot_test::done("gas_store_interior_tests");
}
