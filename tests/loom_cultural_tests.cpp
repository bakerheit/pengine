#include <algorithm>
#include <cmath>
#include <cstring>
#include "city/loom_cultural.h"
#include "city/building_access.h"
#include "city/spines.h"
#include "city/terrain_ops.h"
#include "game/character.h"
#include "gfx/loom_museum_meshes.h"
#include "test_assert.h"

using namespace apricot;
namespace {
glm::vec3 world(const city::StartSite& s,float x,float z,float y=0) {
    const auto p=city::access_world(s,{x,z});return {p.x,s.ground_m+y,p.y};
}
struct Fixture {
    TerrainGround ground{city::kMapSeed};
    RoadGraph roads;
    RibbonBake ribbon;
    city::BuildingAccessBake access;
    TerrainCollider collider{city::kMapSeed};
    Fixture() {
        roads.build(city::map_spines(),{},ground.sampler());
        ribbon=bake_ribbons(roads,ground.sampler());
        access=city::bake_building_access(roads,ribbon,ground.sampler());
        city::append_building_access(ribbon,access);
        collider.set_road_collision(build_road_collision(ribbon));
        add(city::kLoomMuseumSite,city::bake_loom_museum());
        add(city::kLoomParkSite,city::bake_loom_park());
    }
    void add(const city::StartSite& site,std::vector<city::StartPart> parts) {
        city::apply_building_access_layout(site,parts,access);
        for(const auto& p:parts) {
            if(city::building_access_replaces_pavement(site,p))continue;
            const auto centre=world(site,p.centre.x,p.centre.z,p.bottom_m+p.height_m*.5f);
            const float yaw=std::atan2(site.sin_yaw,site.cos_yaw)+glm::radians(p.yaw_deg);
            if(p.solid && p.pitch_deg==0 && p.roll_deg==0)
                collider.add_static_oriented_box(centre,{p.width_m*.5f,p.height_m*.5f,p.depth_m*.5f},yaw);
            // The creator emits the pitched gazebo roof as non-solid panels,
            // matching the runtime; its timber posts and rails carry collision.
            if(city::loom_ground_piece(p) || std::strcmp(p.name,"museum lot")==0)
                collider.add_static_ground_rect({centre.x,centre.z},site.ground_m+p.bottom_m+p.height_m,
                    {p.width_m*.5f,p.depth_m*.5f},yaw,Surface::Rock);
        }
    }
};
PlayerCharacterState walk(PlayerCharacterState state,const TerrainCollider& collider,
                          glm::vec3 goal,int ticks=2000) {
    constexpr float dt=1.f/120.f;
    const CharacterTuning tuning;
    for(int i=0;i<ticks;++i) {
        const glm::vec2 d{goal.x-state.position.x,goal.z-state.position.z};
        if(glm::length(d)<.025f)break;
        InputFrame input;
        input.look_dx=std::atan2(d.x,-d.y)-state.view_yaw;
        input.throttle=std::min(1.f,glm::length(d)/(tuning.walk_speed_mps*dt));
        state=step_character(state,tuning,input,collider,dt);
        REQUIRE(character_position_clear(collider,state.position,tuning));
        REQUIRE(std::isfinite(state.position.y));
    }
    return state;
}
void tour(Fixture& f,const city::StartSite& site,const std::vector<glm::vec2>& stops) {
    const auto start=world(site,stops.front().x,stops.front().y);
    auto state=spawn_character(f.collider,start.x,start.z);
    for(const auto p:stops) {
        const auto goal=world(site,p.x,p.y);
        state=walk(state,f.collider,goal);
        REQUIRE_NEAR(state.position.x,goal.x,.05f);
        REQUIRE_NEAR(state.position.z,goal.z,.05f);
        const float top=&site==&city::kLoomMuseumSite ? 13.85f : 13.3f;
        REQUIRE(state.position.y>=12.7f && state.position.y<=top);
    }
}
// Every exhibit mesh must stay inside the unit volume the authored placement
// convention assumes, and must be wound so the face normal agrees with the
// vertex normal. A mesh that leaves the box is scaled wrong in game by exactly
// the amount it overhangs, which reads as a modelling mistake, not a bug.
void bounded_mesh(const char* name,const MeshData& mesh,std::size_t triangle_cap) {
    REQUIRE_MSG(!mesh.indices.empty(),"exhibit mesh has geometry",name);
    REQUIRE_MSG(mesh.indices.size()/3u<=triangle_cap,"exhibit mesh stays inside its triangle budget",name);
    for(std::size_t i=0;i<mesh.indices.size();i+=3) {
        const auto& a=mesh.vertices.at(mesh.indices[i]);
        const auto& b=mesh.vertices.at(mesh.indices[i+1]);
        const auto& c=mesh.vertices.at(mesh.indices[i+2]);
        const auto normal=glm::cross(b.position-a.position,c.position-a.position);
        REQUIRE_MSG(glm::length(normal)>1e-7f,"no degenerate triangles",name);
        REQUIRE_MSG(glm::dot(glm::normalize(normal),a.normal)>.99f,"winding agrees with the vertex normal",name);
        for(const auto* v:{&a,&b,&c})for(int axis=0;axis<3;++axis) {
            REQUIRE_MSG(std::isfinite(v->position[axis]),"vertex positions are finite",name);
            REQUIRE_MSG(std::abs(v->position[axis])<.51f,"mesh stays inside the unit volume",name);
        }
    }
}
void museum_geometry() {
    bounded_mesh("amphora",make_loom_amphora(),1400u);
    bounded_mesh("sphere",make_loom_sphere(),700u);
    bounded_mesh("cone",make_loom_cone(),300u);
    bounded_mesh("spoked wheel",make_loom_spoked_wheel(),800u);
    bounded_mesh("skull",make_loom_skull(),800u);
    bounded_mesh("ring",make_loom_ring(),600u);
    const auto parts=city::bake_loom_museum();
    float floor_area=0,top=0;
    int paintings=0;
    for(const auto& part:parts) {
        if(std::strcmp(part.name,"museum interior floor")==0)
            floor_area+=part.width_m*part.depth_m;
        if(std::strstr(part.name,"museum painting "))++paintings;
        top=std::max(top,part.bottom_m+part.height_m);
    }
    REQUIRE(floor_area>3800.f);
    REQUIRE(top>15.f);
    REQUIRE(paintings==8);
    apricot_test::pass("large museum galleries and bounded, correctly wound exhibit meshes");
}

// The interior is finished, signed and lit. These are the three things whose
// absence reads instantly as an unfinished room, and all three are emitted by
// name, so a rename that silently drops them is worth failing over.
void interior_finish() {
    const auto parts=city::bake_loom_museum();
    int lining[city::kLoomGalleryCount+1]{};
    int plaques=0,panels=0,labels=0,parquet=0,gallery_lights=0,art_lights=0,cove_lights=0;
    int directory=0,pendants=0,vitrine_glass=0,benches=0;
    bool label_seen[city::kLoomLabelCells]{};
    for(const auto& part:parts) {
        for(std::size_t room=0;room<city::kLoomGalleryCount;++room)
            if(std::strcmp(part.name,city::loom_room(room).wall)==0)++lining[room];
        if(std::strcmp(part.name,"museum wall hall")==0)++lining[city::kLoomGalleryCount];
        for(std::size_t cell=0;cell<city::kLoomGalleryCount;++cell) {
            if(std::strcmp(part.name,city::loom_plaque_name(cell))==0)++plaques;
            if(std::strcmp(part.name,city::loom_panel_name(cell))==0)++panels;
        }
        for(std::size_t cell=0;cell<city::kLoomLabelCells;++cell)
            if(std::strcmp(part.name,city::loom_label_name(cell))==0) {++labels;label_seen[cell]=true;}
        if(std::strcmp(part.name,"museum gallery parquet")==0)++parquet;
        if(std::strcmp(part.name,"museum gallery light lens")==0)++gallery_lights;
        if(std::strcmp(part.name,"museum art light lens")==0)++art_lights;
        if(std::strcmp(part.name,"museum cove light lens")==0)++cove_lights;
        if(std::strcmp(part.name,"museum directory")==0)++directory;
        if(std::strcmp(part.name,"museum gallery light housing")==0)++pendants;
        if(std::strcmp(part.name,"museum vitrine glass")==0)++vitrine_glass;
        if(std::strcmp(part.name,"museum gallery bench")==0)++benches;
    }
    for(std::size_t room=0;room<=city::kLoomGalleryCount;++room)
        REQUIRE_MSG(lining[room]>=6,"every gallery and the hall carry lined wall panels","wall lining");
    REQUIRE(plaques==static_cast<int>(city::kLoomGalleryCount));
    REQUIRE(panels==static_cast<int>(city::kLoomGalleryCount));
    // Every atlas cell drawn by tools/make_loom_museum_textures.py is used. An
    // unused cell means a label was dropped from a room without being noticed.
    for(std::size_t cell=0;cell<city::kLoomLabelCells;++cell)
        REQUIRE_MSG(label_seen[cell],"every object label cell is placed somewhere","label atlas");
    REQUIRE(labels>=static_cast<int>(city::kLoomLabelCells));
    REQUIRE(directory==1);
    REQUIRE(parquet==static_cast<int>(city::kLoomGalleryCount));
    REQUIRE(pendants>=6*static_cast<int>(city::kLoomGalleryCount));
    REQUIRE(art_lights==8);
    REQUIRE(cove_lights>=static_cast<int>(city::kLoomGalleryCount));
    REQUIRE(vitrine_glass>=20);
    REQUIRE(benches>=16);
    // The light budget is a real cost: the tiled grid has no occlusion and
    // spans scale with range/outer, so an unbounded fixture count is paid for
    // every frame the player is anywhere near Loom Way.
    const int sources=gallery_lights+art_lights+cove_lights;
    REQUIRE_MSG(sources<=88,"interior light sources stay inside the stated budget","light budget");
    REQUIRE(sources>=48);
    apricot_test::pass("interior is lined, signed, labelled and lit within budget");
}

// Exhibits are sized against a 1.8 m visitor. The vessels in particular were
// 2.4 m tall once, which made the antiquities room read as a giant's kitchen.
void exhibit_scale() {
    const auto parts=city::bake_loom_museum();
    int vessels=0,wheels=0;
    for(const auto& part:parts) {
        if(std::strcmp(part.name,"museum sculpture amphora")==0) {
            ++vessels;
            REQUIRE_MSG(part.height_m<=1.3f,"a wheel-thrown vessel is about a metre tall","vessel scale");
            REQUIRE(part.width_m<=.75f);
        }
        if(std::strcmp(part.name,"museum exhibit wheel train")==0) {
            ++wheels;
            REQUIRE(part.width_m<=1.6f && part.width_m>=.9f);
            REQUIRE_NEAR(part.width_m,part.height_m,.01f);   // a wheel is round
        }
        if(std::strstr(part.name,"museum exhibit round rocket body")) {
            // Fineness ratio: the old model was as wide as it was tall and
            // read as a fire hydrant standing on a plinth.
            REQUIRE_MSG(part.height_m>2.5f*part.width_m,"the rocket is taller than it is wide","rocket fineness");
        }
        REQUIRE(std::isfinite(part.bottom_m) && std::isfinite(part.height_m));
        REQUIRE_MSG(part.bottom_m+part.height_m<16.f,"nothing pokes through the roof","exhibit headroom");
    }
    REQUIRE(vessels>=10);
    REQUIRE(wheels==10);
    apricot_test::pass("exhibits are sized against the visitor, not the room");
}
void entrances_and_ground(Fixture& f) {
    REQUIRE(city::valid_building_plan(city::kLoomMuseumPlan));
    for(const auto* site:{&city::kLoomMuseumSite,&city::kLoomParkSite}) {
        bool connected=false;
        for(const auto& a:f.access.lots) if(city::access_same_site(a.site,*site)) {
            REQUIRE(a.connected);
            REQUIRE((a.entrance.road_key>>32)==209u);
            REQUIRE(!a.driveway.empty());
            connected=true;
        }
        REQUIRE(connected);
        for(float x:{-site->lot_width_m*.45f,0.f,site->lot_width_m*.45f})
            for(float z:{-site->lot_depth_m*.45f,0.f,site->lot_depth_m*.45f}) {
                const auto p=world(*site,x+site->lot_centre.x,z+site->lot_centre.z);
                const float y=mesh_height_at(city::kMapSeed,p.x,p.z);
                REQUIRE(y<=site->ground_m+.02f);
                REQUIRE(y>=site->ground_m-.65f);
                REQUIRE(city::authored_site_clearance_weight(p.x,p.z)>.99f);
            }
    }
    apricot_test::pass("both cultural sites connect to Loom Way with supported, scatter-free grounds");
}
void character_tours(Fixture& f) {
    // Ground-floor portals lead to all four distinct rooms without crossing
    // any exhibit plinth or the stair solid. The same route returns to Loom.
    // The art room's hanging spine is solid and its gap lines up with the
    // doorway, so the walk in continues straight through to the far wall. If
    // the two ever drift apart this walk dead-ends against the spine.
    tour(f,city::kLoomMuseumSite,{{0,36},{0,21},{0,15},{0,6},{4,1},{4,-8},
        {12,-8},{4,-8},{4,-18},{-4,-18},{-4,-8},{-12,-8},{-24,-8},{-12,-8},
        {-4,-8},{-5,-20},
        {-5,-44},{-12,-44},{-5,-44},{-5,-56},{5,-56},{5,-44},{12,-44},
        {5,-44},{5,-20},{4,1},{0,6},{0,21},{0,36}});
    tour(f,city::kLoomParkSite,{{0,-27},{0,-10},{0,-4},{0,0},{2,0},{2,5},
        {7,5},{7,1},{10,1},{7,1},{7,5},{2,5},{2,1},{0,0},{0,-10},{0,-27}});
    apricot_test::pass("real character walks from Loom through the museum and gazebo park and returns");
}
void upstairs_tour(Fixture& f) {
    const auto& site=city::kLoomMuseumSite;
    const auto start=world(site,0,-21);
    auto state=spawn_character(f.collider,start.x,start.z);
    REQUIRE_NEAR(state.position.y,13.8f,.05f);
    const auto visit=[&](float x,float z,float height) {
        const auto goal=world(site,x,z);
        state=walk(state,f.collider,goal);
        REQUIRE_NEAR(state.position.x,goal.x,.05f);
        REQUIRE_NEAR(state.position.z,goal.z,.05f);
        REQUIRE_NEAR(state.position.y,height,.06f);
    };
    visit(0,-46,19.8f); // Ascend all forty real risers.
    for(float side:{-1.f,1.f}) {
        visit(side*5,-46,19.8f);visit(side*5,-44,19.8f);
        visit(side*12,-44,19.8f);visit(side*5,-44,19.8f);
        visit(side*5,-20,19.8f);visit(side*6,-20,19.8f);
        visit(side*6,-8,19.8f);visit(side*12,-8,19.8f);
        visit(side*6,-8,19.8f);visit(side*6,-20,19.8f);
        visit(side*5,-20,19.8f);visit(side*5,-46,19.8f);visit(0,-46,19.8f);
    }
    // Walking directly toward the atrium must meet the solid balcony guard.
    visit(5,-46,19.8f);visit(5,-20,19.8f);visit(6,-20,19.8f);visit(6,-8,19.8f);
    state=walk(state,f.collider,world(site,0,-8),300);
    REQUIRE(city::access_local(site,{state.position.x,state.position.z}).x>5.3f);
    REQUIRE_NEAR(state.position.y,19.8f,.06f);
    visit(6,-8,19.8f);visit(6,-20,19.8f);visit(5,-20,19.8f);
    visit(5,-46,19.8f);visit(0,-46,19.8f);
    visit(0,-21,13.8f); // Descend without falling through or landing upstairs.
    apricot_test::pass("real character climbs two floors, visits four upper galleries, meets balcony guard and descends");
}
void solid_controls(Fixture& f) {
    const auto& site=city::kLoomMuseumSite;
    const auto start=world(site,-4.7f,8);
    auto state=spawn_character(f.collider,start.x,start.z);
    state=walk(state,f.collider,world(site,-4.7f,3),400);
    REQUIRE(city::access_local(site,{state.position.x,state.position.z}).y>6.0f);
    const auto door=world(site,0,10,3.8f);
    f.collider.add_static_oriented_box(door,{3.f,3,.32f},std::atan2(site.sin_yaw,site.cos_yaw));
    const auto outside=world(site,0,13);
    state=spawn_character(f.collider,outside.x,outside.z);
    state=walk(state,f.collider,world(site,0,5),400);
    REQUIRE(city::access_local(site,{state.position.x,state.position.z}).y>10.4f);
    apricot_test::pass("museum desk blocks movement and sealing the real entrance blocks the entry route");
}
}
int main() {
    museum_geometry();
    interior_finish();
    exhibit_scale();
    Fixture f;
    entrances_and_ground(f);character_tours(f);upstairs_tour(f);solid_controls(f);
    return apricot_test::done("loom_cultural_tests");
}
