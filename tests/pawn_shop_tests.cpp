#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

#include "city/map.h"
#include "city/pawn_shop.h"
#include "city/neighborhood_shops.h"
#include "city/tacomaco.h"
#include "city/billboards.h"
#include "city/roads.h"
#include "game/character.h"
#include "test_assert.h"

using namespace apricot;

namespace {
constexpr float kDt=1.f/120.f;

glm::vec3 world_point(float x,float y,float z) {
    const auto& site=city::kPawnShopSite;
    return {site.origin.x+site.cos_yaw*x+site.sin_yaw*z,
            site.ground_m+y,
            site.origin.z-site.sin_yaw*x+site.cos_yaw*z};
}
glm::vec3 local_point(glm::vec3 world) {
    const auto& site=city::kPawnShopSite;
    const glm::vec3 d=world-glm::vec3{site.origin.x,site.ground_m,site.origin.z};
    return {site.cos_yaw*d.x-site.sin_yaw*d.z,d.y,
            site.sin_yaw*d.x+site.cos_yaw*d.z};
}
TerrainCollider pawn_collider() {
    TerrainCollider collider{city::kMapSeed};
    const auto& site=city::kPawnShopSite;
    const float site_yaw=std::atan2(site.sin_yaw,site.cos_yaw);
    // Match World registration: exact oriented baked solids, then explicit
    // support planes for the visible lot/walk/floor. No synthetic flat world
    // or simplified wall geometry can hide an actual threshold regression.
    for(const auto& p:city::bake_pawn_shop()) {
        const glm::vec3 centre=world_point(p.centre.x,p.bottom_m+p.height_m*.5f,p.centre.z);
        const float yaw=site_yaw+glm::radians(p.yaw_deg);
        if(p.solid) {
            REQUIRE(p.pitch_deg==0.f && p.roll_deg==0.f);
            collider.add_static_oriented_box(centre,
                {p.width_m*.5f,p.height_m*.5f,p.depth_m*.5f},yaw);
        }
        const bool plot_paving=p.finish==city::StartFinish::Asphalt &&
            std::strstr(p.name," lot") && p.bottom_m>=0.f && p.height_m>0.f;
        if(plot_paving || std::strcmp(p.name,"pawn entrance walk")==0 || std::strcmp(p.name,"pawn interior threshold")==0 ||
           std::strcmp(p.name,"pawn interior floor")==0) {
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
void walk_pawn_displays_counter_and_staff_side() {
    const auto collider=pawn_collider();
    const auto outside=world_point(0,0,16);
    auto state=spawn_character(collider,outside.x,outside.z);
    REQUIRE_NEAR(local_point(state.position).y,.20f,1e-4f);
    // Start at the public frontage, enter the genuine opening, browse both
    // display islands, reach the counter and continue around its staff end.
    const glm::vec2 stops[]={{0,4},{0,0},{0,-5.8f},{-3.8f,-5.8f},
        {-3.8f,-1},{0,-1},{3.8f,-1},{3.8f,-5.8f},{0,-5.8f},{0,-6.8f}};
    for(const auto stop:stops) {
        state=advance_toward(state,collider,world_point(stop.x,0,stop.y),1500);
        const auto local=local_point(state.position);
        REQUIRE_NEAR(local.x,stop.x,.04f);
        REQUIRE_NEAR(local.z,stop.y,.04f);
        REQUIRE_NEAR(local.y,.20f,1e-4f);
    }
    state=advance_toward(state,collider,world_point(0,0,-9),240);
    REQUIRE(local_point(state.position).z>-6.9f);
    state=advance_toward(state,collider,world_point(7.5f,0,-6.8f),900);
    state=advance_toward(state,collider,world_point(7.5f,0,-9.3f),500);
    state=advance_toward(state,collider,world_point(0,0,-9.3f),900);
    REQUIRE_NEAR(local_point(state.position).x,0,.04f);
    REQUIRE_NEAR(local_point(state.position).z,-9.3f,.04f);
    apricot_test::pass("actual character enters pawn shop, browses stock and reaches both counter sides");
}
void closed_entrance_blocks_same_route() {
    auto collider=pawn_collider();
    const auto& site=city::kPawnShopSite;
    collider.add_static_oriented_box(world_point(0,1.55f,2.5f),
        {1.2f,1.55f,.08f},std::atan2(site.sin_yaw,site.cos_yaw));
    const auto outside=world_point(0,0,4);
    auto state=spawn_character(collider,outside.x,outside.z);
    state=advance_toward(state,collider,world_point(0,0,-2),900);
    REQUIRE(local_point(state.position).z>2.85f);
    apricot_test::pass("sealed pawn entrance stops the same actual movement sequence");
}
void parcel_stays_clear_of_existing_sites_and_roads() {
    const auto& pawn=city::kPawnShopSite;
    for(const auto* other:{&city::kGasStationSite,&city::kMotelSite,&city::kApartmentSite,
        &city::kFastFoodSite,&city::kTacomacoSite,&city::kBankSite,&city::kCarWashSite,&city::kAutoRepairSite,
        &city::kLaundromatSite,&city::kNessBillboardSite,&city::kPinnatyTaxiBillboardSite}) {
        // All sites use the same authored six-degree grid, so projected local
        // intervals give exact parcel separation rather than inflated AABBs.
        // What that separating-axis test actually needs is that the lot axes
        // are PARALLEL to the pawn shop's; it does not care which way a
        // building faces, because a rectangle turned through 180 degrees
        // occupies the identical footprint. Requiring an identical yaw was
        // stricter than the maths and broke when Tacomaco was authored on the
        // same grid facing the other way (174 degrees against -6). The dot
        // product of the two headings is +-1 for exactly the turns that keep
        // the axes parallel, and rejects the 6-degree reflection that an
        // abs() comparison would have let through.
        const float heading_dot=other->cos_yaw*pawn.cos_yaw+
                                other->sin_yaw*pawn.sin_yaw;
        REQUIRE_MSG(std::fabs(heading_dot)>.9999f,
                    "site lot axes are not parallel to the pawn parcel, so the "
                    "interval separation below would not be exact",other->name);
        const auto c=local_point({other->origin.x+other->cos_yaw*other->lot_centre.x+
            other->sin_yaw*other->lot_centre.z,other->ground_m,
            other->origin.z-other->sin_yaw*other->lot_centre.x+other->cos_yaw*other->lot_centre.z});
        REQUIRE_MSG(std::fabs(c.x)>=(pawn.lot_width_m+other->lot_width_m)*.5f ||
                    std::fabs(c.z)>=(pawn.lot_depth_m+other->lot_depth_m)*.5f,
                    "pawn parcel overlaps another site",other->name);
    }
    // Sample the complete parcel including its boundary against every real
    // road ribbon, including sidewalks. Ground margins must not hide a road.
    for(float x=-28;x<=28;x+=1) for(float z=-16.9f;z<=16.9f;z+=.5f) {
        const auto point=world_point(x,0,z);
        for(const auto& road:city::kRoads) for(int i=0;i+1<road.count;++i) {
            const glm::vec2 a{road.path[i].x,road.path[i].z};
            const glm::vec2 b{road.path[i+1].x,road.path[i+1].z};
            const glm::vec2 d=b-a;
            const float t=std::clamp(glm::dot(glm::vec2{point.x,point.z}-a,d)/glm::dot(d,d),0.f,1.f);
            const float distance=glm::length(glm::vec2{point.x,point.z}-(a+d*t));
            REQUIRE_MSG(distance>=road.ribbon_half_m()-.002f,"pawn parcel crosses road or sidewalk",road.name);
        }
    }
    for(const auto& p:city::bake_pawn_shop()) {
        REQUIRE_MSG(std::fabs(p.centre.x)+p.width_m*.5f<=28.001f &&
                    std::fabs(p.centre.z)+p.depth_m*.5f<=16.901f,"pawn fixture exceeds parcel",p.name);
    }
    apricot_test::pass("pawn parcel clears all nearby authored sites and road ribbons");
}
}
int main() {
    parcel_stays_clear_of_existing_sites_and_roads();
    walk_pawn_displays_counter_and_staff_side();
    closed_entrance_blocks_same_route();
    return apricot_test::done("pawn_shop_tests");
}
