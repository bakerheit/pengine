#include <algorithm>
#include <cmath>
#include <cstring>
#include "city/neighborhood_bar.h"
#include "city/building_access.h"
#include "city/neighborhood_towers.h"
#include "city/spines.h"
#include "game/character.h"
#include "physics/vehicle.h"
#include "test_assert.h"
using namespace apricot;
namespace {
constexpr float dt=1.f/120.f;
glm::vec3 world(float x,float y,float z) {
    const auto& site=city::kNeighborhoodBarSite;
    const auto p=city::access_world(site,{x,z});return {p.x,site.ground_m+y,p.y};
}
bool support(const city::StartPart& p) {
    return std::strcmp(p.name,"bar lot")==0 || std::strcmp(p.name,"bar front pavement")==0 ||
        std::strcmp(p.name,"bar back alley")==0 || std::strcmp(p.name,"bar interior floor")==0 ||
        std::strcmp(p.name,"bar alley entrance walk")==0 || std::strstr(p.name,"threshold");
}
TerrainCollider bar_collider(const std::vector<city::StartPart>& parts,const RoadCollision& roads) {
    TerrainCollider collider(city::kMapSeed);collider.set_road_collision(roads);
    const auto& site=city::kNeighborhoodBarSite;
    for(const auto& p:parts) {
        if(city::building_access_replaces_pavement(site,p)) continue;
        const auto centre=world(p.centre.x,p.bottom_m+p.height_m*.5f,p.centre.z);
        const float yaw=std::atan2(site.sin_yaw,site.cos_yaw)+glm::radians(p.yaw_deg);
        if(p.solid) collider.add_static_oriented_box(centre,
            {p.width_m*.5f,p.height_m*.5f,p.depth_m*.5f},yaw);
        if(support(p)) collider.add_static_ground_rect({centre.x,centre.z},
            site.ground_m+p.bottom_m+p.height_m,{p.width_m*.5f,p.depth_m*.5f},yaw,Surface::Rock);
    }
    return collider;
}
void parcel_and_contents(const std::vector<city::StartPart>& parts,const RoadGraph& roads) {
    const auto& site=city::kNeighborhoodBarSite;
    const auto lots=city::expand_building_access_lots(city::authored_building_access_lots(),roads);
    for(const auto& other:lots) {
        if(city::access_same_site(site,other.site))continue;
        const auto p=city::access_local(site,city::access_world(other.site,
            {other.pavement.centre.x,other.pavement.centre.z}));
        REQUIRE_MSG(p.x+other.pavement.width_m*.5f<=site.lot_centre.x-site.lot_width_m*.5f ||
                    p.x-other.pavement.width_m*.5f>=site.lot_centre.x+site.lot_width_m*.5f ||
                    p.y+other.pavement.depth_m*.5f<=site.lot_centre.z-site.lot_depth_m*.5f ||
                    p.y-other.pavement.depth_m*.5f>=site.lot_centre.z+site.lot_depth_m*.5f,
                    "bar parcel overlaps expanded authored site",other.name);
    }
    for(const auto& tower:city::kNeighborhoodTowers) {
        const auto p=city::access_local(site,{tower.site.origin.x,tower.site.origin.z});
        REQUIRE(std::fabs(p.x)>=(site.lot_width_m+tower.site.lot_width_m)*.5f ||
                std::fabs(p.y)>=(site.lot_depth_m+tower.site.lot_depth_m)*.5f);
    }
    const float lot_lo_x=site.lot_centre.x-site.lot_width_m*.5f;
    const float lot_hi_x=site.lot_centre.x+site.lot_width_m*.5f;
    const float lot_lo_z=site.lot_centre.z-site.lot_depth_m*.5f;
    const float lot_hi_z=site.lot_centre.z+site.lot_depth_m*.5f;
    for(float x=lot_lo_x;x<=lot_hi_x;x+=1) for(float z=lot_lo_z;z<=lot_hi_z;z+=1) {
        const auto p=city::access_world(site,{x,z});
        for(const auto& edge:roads.edges()) for(std::size_t i=1;i<edge.points.size();++i) {
            const auto a=edge.points[i-1],d=edge.points[i]-a;
            const float t=std::clamp(glm::dot(p-a,d)/glm::dot(d,d),0.f,1.f);
            REQUIRE(glm::length(p-a-d*t)>=edge.half_width_m()+(edge.sidewalks()?kSidewalkWidthM:0.f)-.01f);
        }
        REQUIRE_NEAR(mesh_height_at(city::kMapSeed,p.x,p.y),site.ground_m,.02f);
    }
    REQUIRE(city::valid_start_parts(parts.data(),parts.size()));
    REQUIRE(parts.size()<300u);
    std::size_t stools=0,pool=0,booths=0,bottles=0,stripes=0,stops=0;
    for(const auto& p:parts) {
        REQUIRE_MSG(p.centre.x-p.width_m*.5f>=lot_lo_x-.001f,
                    "bar fixture crosses west parcel edge",p.name);
        REQUIRE_MSG(p.centre.x+p.width_m*.5f<=lot_hi_x+.001f,
                    "bar fixture crosses east parcel edge",p.name);
        REQUIRE_MSG(p.centre.z-p.depth_m*.5f>=lot_lo_z-.001f,
                    "bar fixture crosses rear parcel edge",p.name);
        REQUIRE_MSG(p.centre.z+p.depth_m*.5f<=lot_hi_z+.001f,
                    "bar fixture crosses front parcel edge",p.name);
        stools+=std::strcmp(p.name,"bar cracked stool seat")==0;
        pool+=std::strcmp(p.name,"bar pool table felt")==0;
        booths+=std::strcmp(p.name,"bar booth table")==0;
        bottles+=std::strcmp(p.name,"bar dusty bottle")==0;
        stripes+=std::strcmp(p.name,"bar parking stripe")==0;
        stops+=std::strcmp(p.name,"bar parking stop")==0;
        // Both display-window view lines remain open at standing eye height.
        for(float x:{-5.f,5.f}) if(p.solid)
            REQUIRE(!(std::fabs(p.centre.x-x)<p.width_m*.5f &&
                std::fabs(p.centre.z-11.85f)<p.depth_m*.5f &&
                p.bottom_m<2.2f && p.bottom_m+p.height_m>2.2f));
    }
    REQUIRE(stools==5u && pool==1u && booths==2u && bottles==42u);
    REQUIRE(stripes==6u && stops==5u);
    std::printf("  The Bent Elbow: %zu parts; 55x38m parcel, 24x20m bar, five parking spaces\n",parts.size());
    apricot_test::pass("bar and left-side parking clear expanded plots, towers and roads; all fixtures stay within parcel");
}
PlayerCharacterState walk(PlayerCharacterState s,const TerrainCollider& collider,glm::vec2 local) {
    const CharacterTuning tuning;
    const auto goal=world(local.x,0,local.y);
    for(int i=0;i<1800;++i) {
        const glm::vec2 d{goal.x-s.position.x,goal.z-s.position.z};
        if(glm::length(d)<.02f) break;
        InputFrame input;input.look_dx=std::atan2(d.x,-d.y)-s.view_yaw;
        input.throttle=std::min(1.f,glm::length(d)/(tuning.walk_speed_mps*dt));
        s=step_character(s,tuning,input,collider,dt);
        REQUIRE(character_position_clear(collider,s.position,tuning));
    }
    const auto end=city::access_local(city::kNeighborhoodBarSite,{s.position.x,s.position.z});
    REQUIRE_NEAR(end.x,local.x,.04f);REQUIRE_NEAR(end.y,local.y,.04f);
    return s;
}
void actual_customer_and_alley_paths(const TerrainCollider& collider) {
    const auto start=world(0,0,20);
    auto s=spawn_character(collider,start.x,start.z);
    // Front door, stools, full pool-table circuit, booth aisle, then the real
    // back door and alley. No teleporting through or simplified test walls.
    for(glm::vec2 p:std::vector<glm::vec2>{{0,9},{0,4},{-3.5f,4},{-3.5f,0},
        {0,0},{0,-5},{6.5f,-5},{6.5f,3},{6.5f,8},{0,8},{0,-10},{0,-17}})
        s=walk(s,collider,p);
    s=walk(s,collider,{0,-5});
    s=walk(s,collider,{-9.4f,-5});s=walk(s,collider,{-9.4f,3});
    REQUIRE_NEAR(s.position.y,city::kNeighborhoodBarSite.ground_m+.2f,.01f);
    apricot_test::pass("actual character walks front door, bar, pool-table circuit, booths, back alley and staff side");
}

void parking_and_mercer_inlet(const TerrainCollider& collider,
                              const city::BuildingAccessResult& entrance) {
    REQUIRE(entrance.connected);
    REQUIRE(entrance.entrance.road_key>>32==21u);
    REQUIRE_NEAR(entrance.width_m,6.f,.001f);
    REQUIRE(std::fabs(entrance.entry_local-2.f)<.01f);

    VehicleTuning tuning;tuning.service_brake_grip_boost=1;
    const auto drive=[&](float from_x,float direction) {
        const auto from=world(from_x,0,entrance.entry_local);
        const glm::vec3 forward{city::kGridCos*direction,0,-city::kGridSin*direction};
        auto car=spawn_vehicle(tuning,collider,from.x,from.z,
            std::atan2(-forward.x,-forward.z));
        for(int tick=0;tick<480;++tick) {
            car.velocity=forward*5.f;
            car=step_vehicle(car,tuning,{},collider,dt);
            REQUIRE(car.impact_count==0);
            REQUIRE(std::isfinite(car.position.y));
            REQUIRE(std::fabs(car.velocity.y)<1.5f);
        }
        REQUIRE(std::fabs(city::access_local(city::kNeighborhoodBarSite,
            {car.position.x,car.position.z}).y-entrance.entry_local)<.25f);
        return city::access_local(city::kNeighborhoodBarSite,{car.position.x,car.position.z}).x;
    };
    REQUIRE(drive(-44.f,1.f)>-26.f);
    REQUIRE(drive(-26.f,-1.f)<-43.f);

    for(float bay:{-31.9f,-28.7f,-25.5f,-22.3f,-19.1f}) {
        const float driver_side=bay-1.35f;
        auto person=spawn_character(collider,world(driver_side,0,13.0f).x,
            world(driver_side,0,13.0f).z);
        person=walk(person,collider,{driver_side,17.5f});
        person=walk(person,collider,{0,17.5f});
        person=walk(person,collider,{0,9});
    }
    apricot_test::pass("cars traverse the real Mercer curb cut both ways; all five bays have a clear walk to the bar");
}
}
int main() {
    const TerrainGround ground{city::kMapSeed};RoadGraph roads;
    roads.build(city::map_spines(),{},ground.sampler());
    auto ribbon=bake_ribbons(roads,ground.sampler());
    const auto lots=city::authored_building_access_lots();
    const auto access=city::bake_building_access(roads,ribbon,ground.sampler(),lots);
    const city::BuildingAccessResult* entrance=nullptr;
    for(const auto& lot:access.lots) if(city::access_same_site(lot.site,city::kNeighborhoodBarSite))
        entrance=&lot;
    REQUIRE(entrance!=nullptr);
    city::append_building_access(ribbon,access);
    auto parts=city::bake_neighborhood_bar();
    city::apply_building_access_layout(city::kNeighborhoodBarSite,parts,access);
    parcel_and_contents(parts,roads);
    const auto collider=bar_collider(parts,build_road_collision(ribbon));
    actual_customer_and_alley_paths(collider);
    parking_and_mercer_inlet(collider,*entrance);
    return apricot_test::done("neighborhood_bar_tests");
}
