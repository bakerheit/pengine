#include <algorithm>
#include <cmath>
#include <cstring>
#include "city/authored_staff.h"
#include "city/building_access.h"
#include "city/hospital_overhaul_logistics.h"
#include "city/neighborhood_bar.h"
#include "city/neighborhood_towers.h"
#include "city/roads.h"
#include "city/spines.h"
#include "game/character.h"
#include "physics/vehicle.h"
#include "test_assert.h"

using namespace apricot;
namespace {
constexpr float dt=1.f/120.f;
glm::vec3 point(float x,float z,float y=0) {
    const auto p=city::access_world(city::kGunStoreSite,{x,z});
    return {p.x,city::kGunStoreSite.ground_m+y,p.y};
}
glm::vec2 local(glm::vec3 p) {return city::access_local(city::kGunStoreSite,{p.x,p.z});}

struct Fixture {
    TerrainCollider collider{city::kMapSeed};
    city::BuildingAccessResult entrance;
    Fixture() {
        const TerrainGround ground{city::kMapSeed};
        RoadGraph roads;roads.build(city::map_spines(),{},ground.sampler());
        auto ribbon=bake_ribbons(roads,ground.sampler());
        auto lots=city::authored_building_access_lots();
        const auto access=city::bake_building_access(roads,ribbon,ground.sampler(),lots);
        bool found=false;
        for(std::size_t i=0;i<lots.size();++i) if(city::access_same_site(lots[i].site,city::kGunStoreSite)) {
            entrance=access.lots[i];found=true;
        }
        REQUIRE(found && entrance.connected);
        REQUIRE(entrance.entrance.road_key>>32==36u);
        REQUIRE_NEAR(entrance.width_m,6.f,.001f);
        city::append_building_access(ribbon,access);
        collider.set_road_collision(build_road_collision(ribbon));
        auto parts=city::bake_gun_store();
        city::apply_building_access_layout(city::kGunStoreSite,parts,access);
        for(const auto& p:parts) {
            if(city::building_access_replaces_pavement(city::kGunStoreSite,p)) continue;
            const auto centre=point(p.centre.x,p.centre.z,p.bottom_m+p.height_m*.5f);
            const float yaw=std::atan2(city::kGridSin,city::kGridCos)+glm::radians(p.yaw_deg);
            if(p.solid) {
                REQUIRE(p.pitch_deg==0 && p.roll_deg==0);
                collider.add_static_oriented_box(centre,{p.width_m*.5f,p.height_m*.5f,p.depth_m*.5f},yaw);
            }
            if(city::gun_store_ground_piece(p)) collider.add_static_ground_rect(
                {centre.x,centre.z},city::kGunStoreSite.ground_m+p.bottom_m+p.height_m,
                {p.width_m*.5f,p.depth_m*.5f},yaw,Surface::Rock);
        }
    }
};

PlayerCharacterState walk(PlayerCharacterState state,const TerrainCollider& collider,glm::vec2 target,
                          bool should_arrive=true) {
    const auto goal=point(target.x,target.y);
    const CharacterTuning tuning;
    for(int i=0;i<1800;++i) {
        const glm::vec2 delta{goal.x-state.position.x,goal.z-state.position.z};
        const float distance=glm::length(delta);
        if(distance<.025f)break;
        InputFrame input;input.look_dx=std::atan2(delta.x,-delta.y)-state.view_yaw;
        input.throttle=std::min(1.f,distance/(tuning.walk_speed_mps*dt));
        const auto previous=state;
        state=step_character(state,tuning,input,collider,dt);
        REQUIRE(character_position_clear(collider,state.position,tuning));
        REQUIRE(std::fabs(state.position.y-previous.position.y)<=tuning.max_step_m+.001f);
        REQUIRE(std::isfinite(state.position.y));
    }
    if(should_arrive) {
        if(glm::length(local(state.position)-target)>.05f)
            std::printf("walk target %.2f %.2f reached %.2f %.2f y%.3f\n",target.x,target.y,
                local(state.position).x,local(state.position).y,state.position.y);
        REQUIRE(glm::length(local(state.position)-target)<.05f);
    }
    return state;
}

void walk_customer_and_staff_routes(const Fixture& f) {
    const auto start=point(0,22.5f);
    auto player=spawn_character(f.collider,start.x,start.z);
    for(const auto stop:{glm::vec2{0,18},{0,12},{0,7},{0,3},{0,0},{0,-4.7f},
                        {-2,-4.7f},{-2,-1},{0,-1},{4,-1},{4,-4.7f},{0,-4.7f}})
        player=walk(player,f.collider,stop);
    REQUIRE_NEAR(player.position.y,12.2f,.001f);
    auto blocked=walk(player,f.collider,{0,-8},false);
    REQUIRE(local(blocked.position).y>-5.65f);
    player=walk(player,f.collider,{5.8f,-4.7f});
    player=walk(player,f.collider,{5.8f,-7.1f});
    player=walk(player,f.collider,{0,-8});
    REQUIRE_NEAR(player.position.y,12.2f,.001f);
    player=walk(player,f.collider,{-5.8f,-8});
    player=walk(player,f.collider,{-5.8f,-4.7f});
    player=walk(player,f.collider,{-2,-4.7f});
    player=walk(player,f.collider,{0,0});
    player=walk(player,f.collider,{0,22.5f});
    auto sealed=f.collider;
    sealed.add_static_oriented_box(point(0,2,1.6f),{1.2f,1.4f,.08f},std::atan2(city::kGridSin,city::kGridCos));
    auto outside=point(0,4);
    auto stopped=walk(spawn_character(sealed,outside.x,outside.z),sealed,{0,-2},false);
    REQUIRE(local(stopped.position).y>2.3f);
    apricot_test::pass("real character walks sidewalk to counter, around staff side and out; counter and sealed-door controls block");
}

void driveway_and_parking(const Fixture& f) {
    VehicleTuning tuning;tuning.service_brake_grip_boost=1;
    const float x=f.entrance.entry_local;
    std::printf("  gun driveway centre %.2f, width %.2f\n",x,f.entrance.width_m);
    // The access solver reserves its fixture margin at the parcel edge.
    REQUIRE(x-f.entrance.width_m*.5f>9.f);
    REQUIRE(x+f.entrance.width_m*.5f<16.f);
    for(float direction:{-1.f,1.f}) {
        const auto from=point(x,direction<0?26.f:7.f);
        auto car=spawn_vehicle(tuning,f.collider,from.x,from.z,
            std::atan2(-city::kGridSin*direction,-city::kGridCos*direction));
        const glm::vec3 forward{city::kGridSin*direction,0,city::kGridCos*direction};
        for(int tick=0;tick<400;++tick) {
            car.velocity.x=forward.x*5.f;car.velocity.z=forward.z*5.f;
            car=step_vehicle(car,tuning,{},f.collider,dt);
            REQUIRE(car.impact_count==0);
            REQUIRE(std::isfinite(car.position.y));
            REQUIRE(std::fabs(car.velocity.y)<1.5f);
        }
        REQUIRE(std::fabs(local(car.position).x-x)<.25f);
        REQUIRE(direction<0?local(car.position).y<10:local(car.position).y>23);
    }
    for(float bay:{-7.2f,-4.4f,4.4f,7.2f}) {
        // Driver-side space connects each parked bay to the painted crossing.
        const float side=bay-1.05f;
        const auto from=point(side,12.8f);
        auto pedestrian=spawn_character(f.collider,from.x,from.z);
        pedestrian=walk(pedestrian,f.collider,{side,8});
        pedestrian=walk(pedestrian,f.collider,{0,8});
        pedestrian=walk(pedestrian,f.collider,{0,0});
    }
    apricot_test::pass("vehicle traverses actual Sixth driveway both ways; each parking bay has a clear pedestrian route");
}

void parcel_and_staff(const Fixture& f) {
    REQUIRE(city::valid_building_plan(city::kGunStorePlan));
    const auto hospital_local = city::access_local(
        city::kHospitalSite,
        city::access_world(city::kGunStoreSite,
                           {city::kGunStoreSite.lot_centre.x,
                            city::kGunStoreSite.lot_centre.z}));
    const float gun_half_width = city::kGunStoreSite.lot_width_m * .5f;
    const float gun_half_depth = city::kGunStoreSite.lot_depth_m * .5f;
    REQUIRE_MSG(
        hospital_local.x + gun_half_width <= city::kHospitalServiceYardMinX ||
            hospital_local.x - gun_half_width >= city::kHospitalServiceYardMaxX ||
            hospital_local.y + gun_half_depth <= city::kHospitalServiceYardMinZ ||
            hospital_local.y - gun_half_depth >= city::kHospitalServiceYardMaxZ,
        "gun parcel overlaps hospital shipping and receiving yard",
        city::kGunStoreSite.name);
    std::vector<city::StartSite> neighbors;
    for(const auto& lot:city::authored_building_access_lots())neighbors.push_back(lot.site);
    neighbors.push_back(city::kNeighborhoodBarSite);
    for(const auto& tower:city::kNeighborhoodTowers)neighbors.push_back(tower.site);
    for(const auto& site:neighbors) {
        if(city::access_same_site(site,city::kGunStoreSite))continue;
        glm::vec2 lo{1e9f},hi{-1e9f};
        for(float x:{-1.f,1.f})for(float z:{-1.f,1.f}) {
            const auto p=city::access_local(city::kGunStoreSite,city::access_world(site,
                {site.lot_centre.x+x*site.lot_width_m*.5f,site.lot_centre.z+z*site.lot_depth_m*.5f}));
            lo=glm::min(lo,p);hi=glm::max(hi,p);
        }
        REQUIRE_MSG(hi.x<=-16 || lo.x>=16 || hi.y<=-17 || lo.y>=17,"gun parcel overlaps authored neighbor",site.name);
    }
    const TerrainGround ground{city::kMapSeed};
    for(float x=-16;x<=16;x+=.5f)for(float z=-17;z<=17;z+=.5f) {
        const auto p=point(x,z);
        REQUIRE_NEAR(ground.sampler().at(p.x,p.z),12.f,.001f);
        for(const auto& road:city::kRoads)for(int i=0;i+1<road.count;++i) {
            const glm::vec2 a{road.path[i].x,road.path[i].z},b{road.path[i+1].x,road.path[i+1].z};
            const auto delta=b-a;
            const float t=std::clamp(glm::dot(glm::vec2{p.x,p.z}-a,delta)/glm::dot(delta,delta),0.f,1.f);
            REQUIRE_MSG(glm::length(glm::vec2{p.x,p.z}-(a+t*delta))>=road.ribbon_half_m(),"gun lot crosses road/sidewalk",road.name);
        }
    }
    for(const auto& p:city::bake_gun_store()) {
        REQUIRE_MSG(std::fabs(p.centre.x)+p.width_m*.5f<16.001f &&
                    std::fabs(p.centre.z)+p.depth_m*.5f<17.001f,"gun fixture exceeds lot",p.name);
    }
    bool clerk=false;
    for(const auto& staff:city::kAuthoredStaff)if(staff.site==&city::kGunStoreSite) {
        clerk=true;
        REQUIRE(character_position_clear(f.collider,city::authored_staff_position(staff),CharacterTuning{}));
        REQUIRE(glm::dot(city::authored_staff_forward(staff),glm::vec3{city::kGridSin,0,city::kGridCos})>.999f);
    }
    REQUIRE(clerk);
    REQUIRE(std::strcmp(city::kAuthoredStaff[city::kDevonStaffIndex].name,"Devon")==0);
    apricot_test::pass("gun lot clears current roads and authored neighbors; plate support, fixtures and clerk verified");
}
}
int main() {
    const Fixture fixture;
    parcel_and_staff(fixture);
    walk_customer_and_staff_routes(fixture);
    driveway_and_parking(fixture);
    return apricot_test::done("gun_store_tests");
}
