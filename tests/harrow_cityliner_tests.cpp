#include <cmath>
#include <cstring>

#include "app/player_car_catalog.h"
#include "app/vehicle_model_tuning.h"
#include "app/dev_menu.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "test_assert.h"
#include "physics/terrain_collider.h"
#include "city/map.h"
#include "road/lane_graph.h"
#include "traffic/crowd.h"

using namespace apricot;

namespace {
float peak_damage(const VehicleDamageState& damage) {
    return *std::max_element(damage.zones.begin(), damage.zones.end());
}

VehicleState wall_hit(const VehicleTuning& tuning, float speed,
                      const VehicleState& previous = {}) {
    TerrainCollider ground(city::kMapSeed);
    const float y=ground.height(0.f,0.f);
    ground.add_static_box({{-4.f,y-2.f,-10.f},{-1.2f,y+5.f,10.f}},Surface::Rock);
    auto car=spawn_vehicle(tuning,ground,0.f,0.f,0.f);
    car.health=previous.health;
    car.body_damage=previous.body_damage;
    car.impact_count=previous.impact_count;
    car.velocity={-speed,0.f,0.f};
    return step_vehicle(car,tuning,{},ground,1.f/120.f);
}

struct TrafficHit { VehicleState player; VehicleDamageState victim; };
TrafficHit traffic_hit(const VehicleTuning& tuning, float speed,
                       const VehicleState& previous = {}) {
    RoadSpine road;
    road.id=1;road.cls=RoadClass::Street;
    road.points={{0.f,-500.f},{0.f,500.f}};
    RoadGraph graph;graph.build({road},{},GroundSampler{});
    LaneGraph lanes;lanes.build(graph,GroundSampler{});
    CrowdTuning ct;ct.max_peds=0;
    Crowd crowd;crowd.build(lanes,0xC17B05ull,{},ct);
    crowd.refresh(0,{0.f,0.f});
    REQUIRE(!crowd.vehicles().empty());
    const auto& victim=crowd.vehicles().front();
    const glm::vec3 forward=glm::normalize(glm::vec3{victim.fwd.x,0.f,victim.fwd.z});
    const glm::vec3 right{-forward.z,0.f,forward.x};
    auto player=previous;
    player.position=victim.pos+right;
    player.orientation=glm::angleAxis(std::atan2(-forward.x,-forward.z),glm::vec3{0,1,0});
    player.velocity=forward*victim.speed_mps-right*speed;
    REQUIRE(crowd.resolve_player_collision(player,tuning.car_collision_half_width,
        tuning.car_collision_half_length,tuning.mass_kg,tuning.body_damage_gain));
    return {player,victim.body_damage};
}

void bus_dents_build_gradually_through_real_collisions() {
    const auto bus=player_model_tuning(DrivingMechanicsStyle::ClassicGta,PlayerCarId::HarrowCityliner);
    auto previous_bus=bus;previous_bus.body_damage_gain=1.f;
    REQUIRE_NEAR(bus.body_damage_gain,.25f,1e-6f);
    for (const auto& model:kPlayerCars) if (model.id!=PlayerCarId::HarrowCityliner)
        REQUIRE_NEAR(player_model_tuning(DrivingMechanicsStyle::ClassicGta,model.id).body_damage_gain,1.f,1e-6f);

    const auto tap=wall_hit(bus,2.f);
    REQUIRE(tap.impact_count==0u);
    REQUIRE(peak_damage(tap.body_damage)==0.f);
    const auto light=wall_hit(bus,8.f);
    const auto heavy=wall_hit(bus,25.f);
    const auto old_heavy=wall_hit(previous_bus,25.f);
    REQUIRE(light.impact_count==1u && heavy.impact_count==1u);
    REQUIRE(peak_damage(light.body_damage)>0.f);
    REQUIRE(peak_damage(heavy.body_damage)>peak_damage(light.body_damage));
    REQUIRE_NEAR(peak_damage(heavy.body_damage),peak_damage(old_heavy.body_damage)*.25f,1e-6f);
    REQUIRE_NEAR(heavy.body_damage.stamps[0].severity,old_heavy.body_damage.stamps[0].severity*.25f,1e-6f);
    REQUIRE_NEAR(heavy.health,old_heavy.health,1e-6f);

    VehicleState repeated;
    for(int i=0;i<14;++i) repeated=wall_hit(bus,25.f,repeated);
    REQUIRE(repeated.health==0.f);
    REQUIRE(repeated.impact_count==14u);
    REQUIRE_NEAR(peak_damage(repeated.body_damage),1.f,1e-6f);
    REQUIRE_NEAR(repeated.body_damage.stamps[0].severity,1.f,1e-6f);

    const auto traffic=traffic_hit(bus,20.f);
    const auto old_traffic=traffic_hit(previous_bus,20.f);
    REQUIRE(peak_damage(old_traffic.player.body_damage)>0.f);
    REQUIRE_NEAR(peak_damage(traffic.player.body_damage),peak_damage(old_traffic.player.body_damage)*.25f,1e-6f);
    REQUIRE_NEAR(traffic.player.health,old_traffic.player.health,1e-6f);
    REQUIRE_NEAR(peak_damage(traffic.victim),peak_damage(old_traffic.victim),1e-6f);
    VehicleState repeated_traffic;
    for(int i=0;i<20;++i) repeated_traffic=traffic_hit(bus,20.f,repeated_traffic).player;
    REQUIRE(repeated_traffic.health==0.f);
    REQUIRE_NEAR(peak_damage(repeated_traffic.body_damage),1.f,1e-6f);
    REQUIRE_NEAR(repeated_traffic.body_damage.stamps[0].severity,1.f,1e-6f);
    std::printf("  bus wall dent: %.3f -> %.3f; traffic dent: %.3f -> %.3f; repeated hits still reach full wreck\n",
        peak_damage(old_heavy.body_damage),peak_damage(heavy.body_damage),
        peak_damage(old_traffic.player.body_damage),peak_damage(traffic.player.body_damage));
}
}

int main() {
    bus_dents_build_gradually_through_real_collisions();
    const auto& bus=player_car_definition(PlayerCarId::HarrowCityliner);
    REQUIRE(std::strcmp(bus.brand,"HARROW")==0);
    REQUIRE(bus.physical_half_wheelbase>2.7f);
    REQUIRE(bus.physical_half_track>1.1f);
    REQUIRE(bus.physical_wheel_radius>.47f);
    REQUIRE_NEAR(bus.wheel_front_z+bus.wheel_rear_z,2*bus.physical_half_wheelbase,1e-5f);
    REQUIRE_NEAR(bus.wheel_x,bus.physical_half_track,1e-5f);
    const auto& brand=kPlayerCarBrands[static_cast<std::size_t>(player_car_brand_index(bus.id))];
    REQUIRE(brand.car_count==5u);
    REQUIRE(kPlayerCars[brand.first_car].id==bus.id);

    const auto tuning=player_model_tuning(DrivingMechanicsStyle::ClassicGta,bus.id);
    REQUIRE_NEAR(tuning.half_wheelbase,2.8f,1e-5f);
    REQUIRE_NEAR(tuning.wheel_radius,.48f,1e-5f);
    REQUIRE(tuning.mass_kg>=8000.f);
    REQUIRE(static_suspension_length(tuning)>0.f);
    REQUIRE(tuning.car_collision_half_length>=5.5f);
    const auto restored=player_model_tuning(DrivingMechanicsStyle::ClassicGta,PlayerCarId::LegacyCar5);
    REQUIRE(restored.mass_kg<2000.f);
    REQUIRE_NEAR(restored.half_wheelbase,1.35f,1e-5f);
    // Full-size suspension must carry the heavier body on the actual airport
    // terrain and still accelerate/brake; a catalog-only test misses this.
    TerrainCollider ground(city::kMapSeed);
    auto state=spawn_vehicle(tuning,ground,150.f,2046.f,-1.57079632679f);
    InputFrame input;input.throttle=1.f;
    for (int tick=0;tick<1200;++tick) {
        state=step_vehicle(state,tuning,input,ground,1.f/120.f);
        REQUIRE(vehicle_up(state).y>.9f);
        REQUIRE(std::isfinite(state.position.y));
    }
    REQUIRE(vehicle_speed(state)>4.f);
    input.throttle=0;input.brake=1.f;
    bool stopped=false;
    for (int tick=0;tick<600;++tick) {
        state=step_vehicle(state,tuning,input,ground,1.f/120.f);
        if (std::abs(vehicle_speed(state))<.3f) { stopped=true;break; }
    }
    // Holding the shared S input beyond this point intentionally reverses.
    REQUIRE(stopped);
    StaticEmesh body;
    REQUIRE(read_static_emesh(asset_path(bus.mesh_path),body));
    REQUIRE(body.indices.size()/3>=650u && body.indices.size()/3<=1300u);
    REQUIRE_NEAR(body.bounds.size().z,11.f,1e-4f);
    REQUIRE_NEAR(body.bounds.size().x,2.88f,1e-4f);
    REQUIRE_NEAR(body.bounds.max.y,3.21f,1e-4f);
    REQUIRE_NEAR(body.bounds.max.x,-body.bounds.min.x,1e-4f);
    for (const auto& v:body.vertices) {
        REQUIRE(std::isfinite(v.px) && std::isfinite(v.py) && std::isfinite(v.pz));
        REQUIRE(v.u>=0.f && v.u<=1.f && v.v>=0.f && v.v<=1.f);
    }
    // Source dimensions must fit one-to-one to the bus chassis, avoiding a
    // tall, short toy bus when the shared passenger-car visual is selected.
    REQUIRE_NEAR(bus.physical_half_track/bus.wheel_x,1.f,1e-5f);
    REQUIRE_NEAR(2*bus.physical_half_wheelbase/(bus.wheel_front_z+bus.wheel_rear_z),1.f,1e-5f);
    return apricot_test::done("harrow_cityliner_tests");
}
