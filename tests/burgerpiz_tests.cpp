#include <filesystem>
#include <cstring>
#include "city/burgerpiz_asset.h"
#include "city/building_access.h"
#include "city/spines.h"
#include "city/terrain_ops.h"
#include "core/emesh_reader.h"
#include "game/character.h"
#include "test_assert.h"
#include "gfx/street_lamp_light.h"

using namespace apricot;
namespace {
PlayerCharacterState walk(PlayerCharacterState s,const TerrainCollider& collider,glm::vec3 goal) {
    const CharacterTuning tuning;
    constexpr float dt=1.f/120;
    for(int i=0;i<2200;++i) {
        glm::vec2 d{goal.x-s.position.x,goal.z-s.position.z};
        if(glm::length(d)<.025f)break;
        InputFrame input;
        input.look_dx=std::atan2(d.x,-d.y)-s.view_yaw;
        input.throttle=std::min(1.f,glm::length(d)/(tuning.walk_speed_mps*dt));
        s=step_character(s,tuning,input,collider,dt);
        REQUIRE(character_position_clear(collider,s.position,tuning));
    }
    return s;
}
}
void check_variant(const city::StartSite& site,const char* root,uint32_t frontage,bool require_assets) {
    const auto world=[&](glm::vec3 p){return city::burgerpiz_world(p,site);};
    const auto path=[&](const std::string& file){return city::burgerpiz_path(file,root);};
    std::printf("Checking %s\n",site.name);
    TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    roads.build(city::map_spines(),{},ground.sampler());
    auto ribbon=bake_ribbons(roads,ground.sampler());
    const auto access=city::bake_building_access(roads,ribbon,ground.sampler());
    bool connected=false;
    for(const auto& lot:access.lots)if(city::access_same_site(lot.site,site)) {
        REQUIRE(lot.connected);
        REQUIRE((lot.entrance.road_key>>32)==frontage);
        REQUIRE(lot.width_m>=6.f);
        connected=true;
    }
    REQUIRE(connected);
    for(float x:{-30.f,0.f,30.f})for(float z:{-19.f,0.f,19.f}) {
        const auto p=world({x,0,z});
        REQUIRE_NEAR(mesh_height_at(city::kMapSeed,p.x,p.z),12.f,.02f);
        REQUIRE(city::authored_site_clearance_weight(p.x,p.z)>.99f);
    }
    // Check the retained parcel against actual ground-level street segments.
    for(const auto& road:city::kRoads) {
        if(road.district!=city::DistrictId::PinattyRow || road.structure!=city::RoadStructure::Ground)continue;
        for(int i=1;i<road.count;++i)for(float x:{-30.f,30.f})for(float z:{-19.f,19.f}) {
            const auto p=world({x,0,z});
            const glm::vec2 a{road.path[i-1].x,road.path[i-1].z},b{road.path[i].x,road.path[i].z};
            const auto v=b-a,delta=glm::vec2{p.x,p.z}-a;
            const float t=glm::clamp(glm::dot(delta,v)/glm::dot(v,v),0.f,1.f);
            REQUIRE(glm::length(delta-t*v)>city::road_ribbon_half_m(road.cls));
        }
    }
    apricot_test::pass("restaurant parcel clears streets and connects to its assigned frontage with level ground");
    if(!std::filesystem::exists(path("materials.txt")) && !require_assets) {
        std::puts("SKIP private BurgerPiz geometry; cook supplied GLB to run asset traversal checks");
        return;
    }
    city::BurgerPizAsset asset;
    REQUIRE(city::load_burgerpiz_asset(asset,root));
    REQUIRE(asset.materials.size()>=50);
    REQUIRE(asset.lights.size()>=16);
    REQUIRE(asset.parking_lights.size()==2);
    REQUIRE(glm::distance(asset.parking_lights[0],asset.parking_lights[1])>3.f);
    for(const auto p:asset.parking_lights) {
        REQUIRE(p.y>8 && p.y<9);
        REQUIRE(p.x<-15 && p.x>-21);
        const auto position=world(p);
        REQUIRE_NEAR(position.y,site.ground_m+p.y,.001f);
        REQUIRE(!parking_lamp_light(position,position,0.f));
        REQUIRE(!parking_lamp_light(position,position+glm::vec3{261,0,0},1.f));
        const auto night=parking_lamp_light(position,position,1.f);
        const auto dusk=parking_lamp_light(position,position,.5f);
        REQUIRE(night.has_value() && dusk.has_value());
        REQUIRE_NEAR(night->position_range.w,22.f,.001f);
        REQUIRE_NEAR(night->direction_power.w,10.f,.001f);
        REQUIRE_NEAR(dusk->direction_power.w,5.f,.001f);
        REQUIRE(night->direction_power.y==-1.f);
        // A point seven metres off-axis was outside the old narrow road beam.
        const float off_axis_cos=p.y/glm::length(glm::vec2{7.f,p.y});
        REQUIRE(off_axis_cos>night->color_outer.w);
        const auto road=street_lamp_light(position,position,1.f);
        REQUIRE(off_axis_cos<road->color_outer.w);
        REQUIRE(night->direction_power.w>road->direction_power.w);
        REQUIRE(night->position_range.w>p.y); // beam actually reaches the lot
    }
    StaticEmesh lens;
    REQUIRE(read_static_emesh(path("parking_lens.emesh"),lens));
    for(const auto p:asset.parking_lights) {
        REQUIRE(p.x>=lens.bounds.min.x && p.x<=lens.bounds.max.x);
        REQUIRE(p.z>=lens.bounds.min.z && p.z<=lens.bounds.max.z);
        REQUIRE_NEAR(p.y+.04f,lens.bounds.min.y,.01f);
    }
    REQUIRE(street_lamp_lens_tint(0).a==1.f);
    REQUIRE_NEAR(street_lamp_lens_tint(1).a,3.3f,.001f);
    apricot_test::pass("both parking lamp heads have lens-aligned street-light beams, dusk power and distance cutoff");
    std::size_t triangles=0;
    bool glass=false;
    for(const auto& material:asset.materials) {
        StaticEmesh mesh;
        REQUIRE(read_static_emesh(path(material.mesh),mesh));
        triangles+=mesh.indices.size()/3;
        REQUIRE(mesh.bounds.min.x>=-30 && mesh.bounds.max.x<=30);
        REQUIRE(mesh.bounds.min.z>=-19 && mesh.bounds.max.z<=19);
        REQUIRE(mesh.bounds.min.y>=.0f);
        if(material.texture!="-")REQUIRE(std::filesystem::exists(path(material.texture)));
        glass|=material.glass && material.tint.a<.5f;
    }
    REQUIRE(triangles>30000);
    REQUIRE(glass);
    apricot_test::pass("original multi-material restaurant loads, with translucent glazing and no demo-city meshes");
    city::append_building_access(ribbon,access);
    TerrainCollider collider{city::kMapSeed};
    collider.set_road_collision(build_road_collision(ribbon));
    const float yaw=std::atan2(site.sin_yaw,site.cos_yaw);
    collider.add_static_ground_rect({site.origin.x,site.origin.z},12.1f,{30,19},yaw,Surface::Rock);
    city::add_burgerpiz_collision(collider,asset,site);
    auto start=world({-2.9f,0,27});
    auto state=spawn_character(collider,start.x,start.z);
    for(const auto p:std::vector<glm::vec2>{{-2.9f,4},{-2.9f,-2},{-6,-2.5f},{-13,-2.5f},
                                          {-6,-2.5f},{-2.9f,-2},{-2.9f,-6},{-1,-8},{-2.9f,-6},{-2.9f,-2},{-2.9f,4},{-2.9f,27}}) {
        const auto goal=world({p.x,0,p.y});
        state=walk(state,collider,goal);
        const auto reached=city::access_local(site,{state.position.x,state.position.z});
        std::printf("  walk %.2f %.2f -> %.2f %.2f height %.2f\n",p.x,p.y,reached.x,reached.y,state.position.y);
        REQUIRE_NEAR(state.position.x,goal.x,.05f);
        REQUIRE_NEAR(state.position.z,goal.z,.05f);
        REQUIRE(state.position.y>=12.f && state.position.y<12.5f);
    }
    apricot_test::pass("real character walks from its street through the original open doors and dining room and returns");
    // Negative controls: a closed front wall and the original service counter
    // must block the same character, proving this is collision-backed movement.
    start=world({-1,0,-8});
    state=spawn_character(collider,start.x,start.z);
    state=walk(state,collider,world({1,0,-8}));
    const auto counter=city::access_local(site,{state.position.x,state.position.z});
    REQUIRE(counter.x<.1f);
    const auto entrance=world({-2.9f,1.5f,.9f});
    collider.add_static_oriented_box(entrance,{1.2f,1.5f,.1f},yaw);
    start=world({-2.9f,0,4});
    state=spawn_character(collider,start.x,start.z);
    state=walk(state,collider,world({-2.9f,0,-2}));
    REQUIRE(city::access_local(site,{state.position.x,state.position.z}).y>1.3f);
    apricot_test::pass("counter and sealed doorway prevent movement");
}
int main(int argc,char** argv) {
    const bool required=argc>1 && std::strcmp(argv[1],"--require-assets")==0;
    check_variant(city::kBurgerPizSite,city::kBurgerPizAssetRoot,39,required);
    check_variant(city::kFreakyFranksSite,city::kFreakyFranksAssetRoot,36,required);
    check_variant(city::kTacomacoSite,city::kTacomacoAssetRoot,33,required);
    city::BurgerPizAsset original;
    if(city::load_burgerpiz_asset(original))for(const auto& item: {
        std::pair{city::kFreakyFranksAssetRoot,"Freaky Franks sign"},
        std::pair{city::kTacomacoAssetRoot,"TacoMaco sign"}}) {
        city::BurgerPizAsset variant;
        if(!city::load_burgerpiz_asset(variant,item.first)) {REQUIRE(!required);continue;}
        REQUIRE(original.boxes.size()==variant.boxes.size());
        for(std::size_t i=0;i<original.boxes.size();++i) {
            REQUIRE(original.boxes[i].centre==variant.boxes[i].centre);
            REQUIRE(original.boxes[i].half==variant.boxes[i].half);
        }
        REQUIRE(original.grounds.size()==variant.grounds.size());
        REQUIRE(original.lights==variant.lights);
        bool menu=false;
        for(const auto& m:variant.materials)menu|=m.texture=="menu_atlas.png";
        REQUIRE(menu);
        std::ifstream manifest(city::burgerpiz_path("manifest.json",item.first));
        const std::string content((std::istreambuf_iterator<char>(manifest)),{});
        REQUIRE(content.find(item.second)!=std::string::npos);
        REQUIRE(content.find("BurgerPiz_L")==std::string::npos);
        REQUIRE(content.find("TacoTaco") == std::string::npos);
        apricot_test::pass("new brand lettering and menu atlas retain the original furnished-interior collision and lights");
    }
    return apricot_test::done("burgerpiz_tests");
}
