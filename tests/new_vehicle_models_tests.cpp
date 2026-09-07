#include <cmath>
#include "app/player_car_catalog.h"
#include "app/vehicle_model_tuning.h"
#include "app/vehicle_headlight_profile.h"
#include "audio/vehicle_sound_profile.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "city/map.h"
#include "physics/terrain_collider.h"
#include "test_assert.h"
using namespace apricot;
namespace {
void model_contract(PlayerCarId id,float length,float width,float mass,float door_rear,float door_front) {
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
    REQUIRE(body.indices.size()/3>=650u && body.indices.size()/3<=1300u);
    REQUIRE(open.indices.size()/3<=1700u && door.indices.size()/3<=140u);
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
}
int main() {
    model_contract(PlayerCarId::VesperScythe,4.65f,2.18f,1320.f,-.54f,.78f);
    model_contract(PlayerCarId::HalcyonSovereign,8.20f,2.26f,2750.f,.18f,1.60f);
    REQUIRE(player_model_tuning(DrivingMechanicsStyle::ClassicGta,PlayerCarId::HarrowCityliner).mass_kg==9000.f);
    REQUIRE(player_model_tuning(DrivingMechanicsStyle::ClassicGta,PlayerCarId::AlderPip).mass_kg==1050.f);
    apricot_test::pass("sports car and limousine preserve asset, lamp, door and actual driving contracts");
    return apricot_test::done("new_vehicle_models_tests");
}
