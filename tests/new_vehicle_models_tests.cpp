#include <cmath>
#include <cstddef>
#include "app/player_car_catalog.h"
#include "app/vehicle_model_tuning.h"
#include "app/vehicle_driver_door.h"
#include "app/vehicle_driver_pose.h"
#include "app/vehicle_plate_mesh.h"
#include "app/vehicle_registration.h"
#include "app/vehicle_headlight_profile.h"
#include "app/vehicle_snow_mesh.h"
#include "audio/vehicle_sound_profile.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "city/map.h"
#include "physics/terrain_collider.h"
#include "test_assert.h"
using namespace apricot;
namespace {
struct MeshBudget {
    std::size_t body_min=650u;
    std::size_t body_max=1300u;
    std::size_t open_max=1700u;
    std::size_t door_max=140u;
};
void model_contract(PlayerCarId id,float length,float width,float mass,float door_rear,float door_front,
                    MeshBudget budget={}) {
    const auto& def=player_car_definition(id);
    const auto tuning=player_model_tuning(DrivingMechanicsStyle::ClassicGta,id);
    REQUIRE_NEAR(tuning.mass_kg,mass,.001f);
    REQUIRE_NEAR(tuning.half_track/def.wheel_x,1.f,1e-6f);
    REQUIRE_NEAR(2*tuning.half_wheelbase/(def.wheel_front_z+def.wheel_rear_z),1.f,1e-6f);
    REQUIRE_NEAR(tuning.car_collision_half_length,length*.5f,.001f);
    REQUIRE(tuning.body_damage_gain==1.f);
    StaticEmesh body,open,door;
    const std::string body_path=def.mesh_path;
    const auto folder=body_path.substr(0,body_path.find_last_of('/')+1);
    REQUIRE(read_static_emesh(asset_path(body_path),body));
    REQUIRE(read_static_emesh(asset_path(folder+"body_open.emesh"),open));
    REQUIRE(read_static_emesh(asset_path(folder+"driver_door.emesh"),door));
    REQUIRE(body.indices.size()/3>=budget.body_min &&
            body.indices.size()/3<=budget.body_max);
    REQUIRE(open.indices.size()/3<=budget.open_max &&
            door.indices.size()/3<=budget.door_max);
    REQUIRE_NEAR(body.bounds.size().z,length,1e-4f);
    REQUIRE_NEAR(body.bounds.size().x,width,1e-4f);
    REQUIRE_NEAR(body.bounds.max.x,-body.bounds.min.x,1e-4f);
    REQUIRE_NEAR(door.bounds.min.z,door_rear,1e-4f);
    REQUIRE_NEAR(door.bounds.max.z,door_front,1e-4f);
    for(const auto* mesh:{&body,&open,&door})for(const auto& v:mesh->vertices) {
        REQUIRE(std::isfinite(v.px) && std::isfinite(v.py) && std::isfinite(v.pz));
        REQUIRE(v.u>=0 && v.u<=1 && v.v>=0 && v.v<=1);
    }
    const auto head=vehicle_headlight_profile(def.mesh_path);
    const auto brake=vehicle_brakelight_profile(def.mesh_path);
    REQUIRE(head.exposed() && head.id>=17 && brake.id==head.id);
    for(std::size_t side=0;side<2;++side) {
        glm::vec3 point;
        REQUIRE(vehicle_headlight_origin(body,head,side,point));
        REQUIRE(head.regions[0].contains(point));
    }
    REQUIRE(vehicle_sound_profile(def.mesh_path).model_key!="default");
    // Real model dimensions/suspension carry each body under normal controls.
    // This catches the former metadata branch that gave every new car bus mass.
    TerrainCollider ground(city::kMapSeed);
    // An elevated flat test pad isolates braking from the hills north of the airport.
    ground.add_static_ground_rect({150,2046},200.f,{2000,2000},0,Surface::Rock);
    auto car=spawn_vehicle(tuning,ground,150,2046,0);
    car.position.y=200.f+static_ride_height(tuning);
    InputFrame input;input.throttle=1.f;
    for(int i=0;i<960;++i) {
        car=step_vehicle(car,tuning,input,ground,1.f/120.f);
        REQUIRE(std::isfinite(car.position.y));
        REQUIRE(vehicle_up(car).y>.90f);
    }
    REQUIRE(vehicle_speed(car)>5.f);
    input.throttle=0;input.brake=1.f;
    bool stopped=false;
    const float initial_speed=vehicle_speed(car);
    float minimum_speed=std::abs(initial_speed);
    for(int i=0;i<900;++i) {
        car=step_vehicle(car,tuning,input,ground,1.f/120.f);
        minimum_speed=std::min(minimum_speed,std::abs(vehicle_speed(car)));
        if(std::abs(vehicle_speed(car))<.3f){stopped=true;break;}
    }
    std::printf("  %s braking %.3f -> %.3f, minimum %.3f\n",def.model,initial_speed,vehicle_speed(car),minimum_speed);
    std::printf("  position %.2f %.2f %.2f velocity %.2f %.2f %.2f\n",car.position.x,car.position.y,car.position.z,car.velocity.x,car.velocity.y,car.velocity.z);
    REQUIRE(stopped);
}

bool opaque_surface_before(const StaticEmesh& mesh,glm::vec3 origin,
                           glm::vec3 direction,float limit) {
    // Ray/triangle intersection with the same front-face culling as the game.
    // A reversed floor or a distant bumper must not conceal a missing firewall.
    const auto position=[](const auto& v){return glm::vec3{v.px,v.py,v.pz};};
    for(std::size_t i=0;i<mesh.indices.size();i+=3u) {
        const auto a=position(mesh.vertices[mesh.indices[i]]);
        const auto b=position(mesh.vertices[mesh.indices[i+1u]]);
        const auto c=position(mesh.vertices[mesh.indices[i+2u]]);
        const auto e1=b-a,e2=c-a,p=glm::cross(direction,e2);
        const float det=glm::dot(e1,p);
        if(det<=1e-9f) continue;
        const auto t=origin-a;
        const float u=glm::dot(t,p)/det;
        if(u<0.f || u>1.f) continue;
        const auto q=glm::cross(t,e1);
        const float v=glm::dot(direction,q)/det;
        if(v<0.f || u+v>1.f) continue;
        const float distance=glm::dot(e2,q)/det;
        if(distance>0.f && distance<limit) return true;
    }
    return false;
}

void pizaz_cabin_blocks_road_and_wheel_cavities(const StaticEmesh& mesh) {
    for(float x:{-.68f,-.4f,0.f,.4f,.68f})
        for(float y:{.36f,.50f,.63f,.82f})
            REQUIRE_MSG(opaque_surface_before(mesh,{x,y,.81f},{0,0,1},.17f),
                        "front wheel cavity shows under dashboard","PIZAZ firewall");
    for(float x:{-.765f,-.64f,-.4f,.4f,.64f,.765f})
        for(float z:{-.88f,-.35f,.45f,.87f})
            REQUIRE_MSG(opaque_surface_before(mesh,{x,.37f,z},{0,-1,0},.16f),
                        "road shows through cabin","PIZAZ floor and sills");
    for(float x:{-.66f,.66f})
        for(float y:{.34f,.60f,.79f})
            REQUIRE_MSG(opaque_surface_before(mesh,{x,y,-.89f},{0,0,-1},.12f),
                        "rear wheel cavity shows beside bench","PIZAZ rear bulkhead");
}

void pizaz_component_meshes_are_ready_for_the_runtime_loader() {
    const auto& definition=player_car_definition(PlayerCarId::PizazConstant);
    const std::string body_path=definition.mesh_path;
    const auto folder=body_path.substr(0,body_path.find_last_of('/')+1);
    StaticEmesh drive,open,closed;
    REQUIRE(read_static_emesh(asset_path(folder+"body_drive.emesh"),drive));
    REQUIRE(read_static_emesh(asset_path(folder+"body_open.emesh"),open));
    REQUIRE(read_static_emesh(asset_path(body_path),closed));
    REQUIRE(drive.indices.size()>open.indices.size());
    REQUIRE(closed.indices.size()>drive.indices.size());
    for(const auto* shell:{&closed,&open,&drive})
        pizaz_cabin_blocks_road_and_wheel_cavities(*shell);
    for(const char* name:{"driver_door","passenger_door","windshield","rear_glass",
            "driver_glass","passenger_glass","driver_rear_glass","passenger_rear_glass"}) {
        StaticEmesh part;
        REQUIRE(read_static_emesh(asset_path(folder+name+".emesh"),part));
        REQUIRE(part.bounds.valid() && !part.indices.empty());
    }
    StaticEmesh front,rear;
    REQUIRE(read_static_emesh(asset_path(folder+"front_wheel.emesh"),front));
    REQUIRE(read_static_emesh(asset_path(folder+"rear_wheel.emesh"),rear));
    for(const auto* wheel:{&front,&rear}) {
        const auto size=wheel->bounds.size();
        REQUIRE_NEAR(std::max(size.y,size.z)*.5f,.32f,.001f);
        REQUIRE_NEAR(wheel->bounds.min.y+wheel->bounds.max.y,0.f,.001f);
        REQUIRE_NEAR(wheel->bounds.min.z+wheel->bounds.max.z,0.f,.001f);
        REQUIRE_NEAR(wheel->bounds.min.x+wheel->bounds.max.x,0.f,.001f);
        REQUIRE(wheel->indices.size()/3u>2000u);
    }
    REQUIRE_NEAR(definition.physical_half_wheelbase,1.38f,1e-6f);
    REQUIRE_NEAR(definition.physical_half_track,.78f,1e-6f);
    REQUIRE_NEAR(definition.physical_wheel_radius,.32f,1e-6f);
    apricot_test::pass("PIZAZ runtime body, six panes, two doors and custom wheels are present");
}

void rearloader_runtime_contract() {
    const auto id=PlayerCarId::HarrowRearloader;
    const auto& definition=player_car_definition(id);
    const auto tuning=player_model_tuning(DrivingMechanicsStyle::ClassicGta,id);
    // Saved ids are append-only; a municipal body must never shift old saves.
    REQUIRE(static_cast<unsigned>(id)==static_cast<unsigned>(PlayerCarId::PizazConstant)+1u);
    REQUIRE_NEAR(tuning.half_wheelbase,1.90f,1e-6f);
    REQUIRE_NEAR(tuning.half_track,1.f,1e-6f);
    REQUIRE_NEAR(tuning.front_drive_bias,0.f,1e-6f);
    REQUIRE(has_animated_driver(id));
    const auto door=vehicle_driver_door(id);
    REQUIRE_NEAR(door.hinge.z,3.22f,.001f);
    const auto approach=vehicle_driver_approach_point(id);
    REQUIRE(approach.z>definition.wheel_front_z+tuning.wheel_radius);
    StaticEmesh body,front,rear;
    REQUIRE(read_static_emesh(asset_path(definition.mesh_path),body));
    REQUIRE(read_static_emesh(asset_path("models/vehicles/harrow_rearloader/front_wheel.emesh"),front));
    REQUIRE(read_static_emesh(asset_path("models/vehicles/harrow_rearloader/rear_wheel.emesh"),rear));
    REQUIRE(rear.bounds.size().x>front.bounds.size().x*1.6f);
    REQUIRE_NEAR(std::max(front.bounds.size().y,front.bounds.size().z)*.5f,tuning.wheel_radius,.003f);
    REQUIRE_NEAR(std::max(rear.bounds.size().y,rear.bounds.size().z)*.5f,tuning.wheel_radius,.003f);
    const float axle_midpoint=(definition.wheel_front_z-definition.wheel_rear_z)*.5f;
    REQUIRE(tuning.car_collision_half_length>=body.bounds.max.z-axle_midpoint);
    REQUIRE(tuning.car_collision_half_length>=axle_midpoint-body.bounds.min.z);
    REQUIRE(player_plate_use(id)==PlateUse::Commercial);
    const auto mounts=vehicle_plate_mounts(body,definition.mesh_path);
    REQUIRE(mounts.size()==2u);
    // Plate centres must sit just ahead of their dedicated backing, not inside it.
    for(const auto& mount:mounts) {
        float surface=0.f;
        REQUIRE(vehicle_body_surface_z(body,mount.centre.x,mount.centre.y,mount.normal.z>0.f,surface));
        const float clearance=(mount.centre.z-surface)*mount.normal.z;
        REQUIRE(clearance>=.004f && clearance<.012f);
    }
    apricot_test::pass("Rearloader preserves saved ids, heavy rear-drive fit, dual rear tires, access steps and exposed plates");
}

void selected_1991_candidates_are_playable_models() {
    struct Expected { PlayerCarId id; float mass; float radius; int panes; };
    for (const Expected expected : {
             Expected{PlayerCarId::GlmMeridian,1750.f,.365f,6},
             Expected{PlayerCarId::RodeoSwitchback,1680.f,.46f,6},
             Expected{PlayerCarId::HarrowHookline,4200.f,.46f,4},
             Expected{PlayerCarId::HarrowRearloader,10500.f,.60f,4}}) {
        const auto& car=player_car_definition(expected.id);
        const auto tuning=player_model_tuning(DrivingMechanicsStyle::ClassicGta,expected.id);
        REQUIRE_NEAR(tuning.mass_kg,expected.mass,.001f);
        REQUIRE_NEAR(tuning.wheel_radius,expected.radius,.001f);
        REQUIRE(vehicle_sound_profile(car.mesh_path).model_key!="default");
        const std::string folder=std::string(car.mesh_path).substr(0,
            std::string(car.mesh_path).find_last_of('/')+1);
        StaticEmesh body,open,door,front_wheel,rear_wheel;
        REQUIRE(read_static_emesh(asset_path(car.mesh_path),body));
        REQUIRE(read_static_emesh(asset_path(folder+"body_open.emesh"),open));
        REQUIRE(read_static_emesh(asset_path(folder+"driver_door.emesh"),door));
        REQUIRE(read_static_emesh(asset_path(folder+"front_wheel.emesh"),front_wheel));
        REQUIRE(read_static_emesh(asset_path(folder+"rear_wheel.emesh"),rear_wheel));
        REQUIRE(body.indices.size()>open.indices.size());
        REQUIRE(door.indices.size()>100u);
        REQUIRE(body.indices.size()/3u<60000u);
        for (const auto* mesh : {&body,&open,&door,&front_wheel,&rear_wheel}) {
            REQUIRE(mesh->bounds.valid() && !mesh->indices.empty());
            for (const auto& vertex:mesh->vertices) {
                REQUIRE(std::isfinite(vertex.px) && std::isfinite(vertex.py) &&
                        std::isfinite(vertex.pz));
                REQUIRE(vertex.u>=0.f && vertex.u<=1.f && vertex.v>=0.f && vertex.v<=1.f);
            }
        }
        constexpr const char* panes[]{"windshield","rear_glass","passenger_glass",
            "driver_glass","driver_rear_glass","passenger_rear_glass"};
        for (int i=0;i<expected.panes;++i) {
            StaticEmesh glass;
            REQUIRE(read_static_emesh(asset_path(folder+panes[i]+".emesh"),glass));
            REQUIRE(glass.bounds.valid() && !glass.indices.empty());
            if(i==0) {
                REQUIRE(vehicle_windshield_profile(car.mesh_path)!=nullptr);
                const auto tagged=make_windshield_snow_mesh(glass);
                REQUIRE(tagged.indices.size()==glass.indices.size());
                for(const auto& vertex:tagged.vertices)
                    REQUIRE(vertex.material_weights.w==-1.f);
            }
        }
        const auto head=vehicle_headlight_profile(car.mesh_path);
        const auto brake=vehicle_brakelight_profile(car.mesh_path);
        REQUIRE(head.exposed() && brake.id==head.id);
        for(std::size_t side=0;side<2;++side) {
            glm::vec3 point;
            REQUIRE(vehicle_headlight_origin(body,head,side,point));
        }
        TerrainCollider ground(city::kMapSeed);
        ground.add_static_ground_rect({150,2046},200.f,{2000,2000},0,Surface::Rock);
        auto state=spawn_vehicle(tuning,ground,150,2046,0);
        state.position.y=200.f+static_ride_height(tuning);
        InputFrame input;input.throttle=1.f;
        for(int step=0;step<960;++step) {
            state=step_vehicle(state,tuning,input,ground,1.f/120.f);
            REQUIRE(std::isfinite(state.position.y) && vehicle_up(state).y>.8f);
        }
        REQUIRE(vehicle_speed(state)>4.f);
    }
    apricot_test::pass("1991 candidates and Rearloader have complete assets and drivable tuning");
}
}
int main() {
    model_contract(PlayerCarId::VesperScythe,4.65f,2.18f,1320.f,-.54f,.78f);
    model_contract(PlayerCarId::HalcyonSovereign,8.20f,2.26f,2750.f,.18f,1.60f);
    model_contract(PlayerCarId::PizazConstant,4.683288f,2.014016f,1510.f,-.250751f,.837f,
                   {30000u,40000u,32000u,2500u});
    pizaz_component_meshes_are_ready_for_the_runtime_loader();
    rearloader_runtime_contract();
    selected_1991_candidates_are_playable_models();
    REQUIRE(player_model_tuning(DrivingMechanicsStyle::ClassicGta,PlayerCarId::HarrowCityliner).mass_kg==9000.f);
    REQUIRE(player_model_tuning(DrivingMechanicsStyle::ClassicGta,PlayerCarId::AlderPip).mass_kg==1050.f);
    apricot_test::pass("sports car and limousine preserve asset, lamp, door and actual driving contracts");
    return apricot_test::done("new_vehicle_models_tests");
}
