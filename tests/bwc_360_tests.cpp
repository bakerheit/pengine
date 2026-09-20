#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
#include "app/player_car_catalog.h"
#include "app/vehicle_driver_door.h"
#include "app/vehicle_driver_pose.h"
#include "app/vehicle_model_tuning.h"
#include "app/vehicle_headlight_profile.h"
#include "core/asset_root.h"
#include "core/emesh_reader.h"
#include "physics/terrain_collider.h"
#include "test_assert.h"
#include "traffic/ambient.h"
#include "app/traffic_visual_layout.h"
#include "app/vehicle_plate_mesh.h"
using namespace apricot;
namespace {
bool covers(const StaticEmesh& mesh,float side,float z,float y) {
    const auto cross=[](glm::vec2 a,glm::vec2 b){return a.x*b.y-a.y*b.x;};
    for (std::size_t i=0;i<mesh.indices.size();i+=3) {
        const auto& a=mesh.vertices[mesh.indices[i]];
        const auto& b=mesh.vertices[mesh.indices[i+1]];
        const auto& c=mesh.vertices[mesh.indices[i+2]];
        if (side*a.px<.73f || side*b.px<.73f || side*c.px<.73f) continue;
        const glm::vec2 p{z,y},u{a.pz,a.py},v{b.pz,b.py},w{c.pz,c.py};
        const float ab=cross(v-u,p-u),bc=cross(w-v,p-v),ca=cross(u-w,p-w);
        if ((ab>1e-7f && bc>1e-7f && ca>1e-7f) ||
            (ab<-1e-7f && bc<-1e-7f && ca<-1e-7f)) return true;
    }
    return false;
}
StaticEmesh read(const char* name) {
    StaticEmesh mesh;
    REQUIRE(read_static_emesh(asset_path(std::string("models/vehicles/bwc_360/")+name+".emesh"),mesh));
    REQUIRE(!mesh.indices.empty());
    for (const auto& v:mesh.vertices) {
        REQUIRE(std::isfinite(v.px) && std::isfinite(v.py) && std::isfinite(v.pz));
        REQUIRE(v.u>=0 && v.u<=1 && v.v>=0 && v.v<=1);
    }
    return mesh;
}

constexpr float kDt=1.f/120.f;

TerrainCollider flat_ground() {
    TerrainCollider ground(0xB0C360u);
    ground.add_static_ground_rect({0.f,0.f},200.f,{6000.f,6000.f},0.f,Surface::Rock);
    return ground;
}

VehicleState road_speed_car(const VehicleTuning& tuning,const TerrainCollider& ground,float speed) {
    auto car=spawn_vehicle(tuning,ground,0.f,0.f,0.f);
    car.position.y=200.f+static_ride_height(tuning);
    car.velocity=vehicle_forward(car)*speed;
    for(auto& wheel:car.wheels) wheel.angular_velocity=speed/tuning.wheel_radius;
    return car;
}

struct HandlingMetrics {
    float launch_8s=0.f;
    float top_60s=0.f;
    float stop_25=0.f;
    float turn_18=0.f;
};

HandlingMetrics measure_handling() {
    const auto tuning=player_model_tuning(DrivingMechanicsStyle::ClassicGta,PlayerCarId::Bwc360);
    auto ground=flat_ground();
    auto car=road_speed_car(tuning,ground,0.f);
    InputFrame throttle;throttle.throttle=1.f;
    HandlingMetrics result;
    for(int step=0;step<60*120;++step) {
        car=step_vehicle(car,tuning,throttle,ground,kDt);
        REQUIRE(std::isfinite(vehicle_speed(car)));REQUIRE(vehicle_up(car).y>.75f);
        result.top_60s=std::max(result.top_60s,std::abs(vehicle_speed(car)));
        if(step==8*120-1) result.launch_8s=std::abs(vehicle_speed(car));
    }
    car=road_speed_car(tuning,ground,25.f);
    const glm::vec3 brake_start=car.position;
    InputFrame brake;brake.brake=1.f;
    for(int step=0;step<10*120;++step) {
        car=step_vehicle(car,tuning,brake,ground,kDt);
        if(std::abs(vehicle_speed(car))<.3f) {
            result.stop_25=glm::length(glm::vec2(car.position.x-brake_start.x,car.position.z-brake_start.z));
            break;
        }
    }
    car=road_speed_car(tuning,ground,18.f);
    const glm::vec3 start_forward=vehicle_forward(car);
    InputFrame turn;turn.throttle=.25f;turn.steer=1.f;
    for(int step=0;step<2*120;++step) {
        car=step_vehicle(car,tuning,turn,ground,kDt);
        REQUIRE(vehicle_up(car).y>.70f);
    }
    result.turn_18=std::acos(std::clamp(glm::dot(start_forward,vehicle_forward(car)),-1.f,1.f));
    return result;
}
}
int main() {
    constexpr auto id=PlayerCarId::Bwc360;
    static_assert(static_cast<unsigned>(id)==33u); // Append-only checkpoint identity.
    REQUIRE(std::string(player_car_definition(id).brand)=="BWC");
    REQUIRE(has_animated_driver(id) && has_passenger_door(id));
    const auto body=read("body"),open=read("body_drive");
    const auto driver=read("driver_front_door"),passenger=read("passenger_front_door");
    REQUIRE(body.bounds.size().z>4.4f && body.bounds.size().z<4.7f);
    REQUIRE(body.bounds.max.y<1.45f && body.bounds.max.y>1.40f);
    REQUIRE(body.indices.size()/3<19000u);
    for (float side:{-1.f,1.f}) {
        const auto& door=side>0?driver:passenger;
        for (float z:{.20f,.40f,.60f}) for (float y:{.50f,.74f}) {
            REQUIRE(!covers(open,side,z,y));
            REQUIRE(covers(door,side,z,y));
            REQUIRE(covers(body,side,z,y));
        }
        // Removing a door's glass cannot leave an opaque window backing.
        for (float z:{.02f,.20f}) {
            REQUIRE(!covers(open,side,z,1.14f));
            REQUIRE(!covers(door,side,z,1.14f));
        }
    }
    for (const char* name:{"windshield","rear_glass","driver_front_glass","passenger_front_glass",
                           "driver_rear_glass","passenger_rear_glass"}) {
        const auto pane=read(name);
        REQUIRE(pane.indices.size()/3<=48u);
        REQUIRE(pane.bounds.min.y>.75f);
    }
    for (const char* name:{"front_wheel","rear_wheel"}) {
        const auto wheel=read(name);
        REQUIRE_NEAR(wheel.bounds.center().x,0.f,.012f);
        REQUIRE_NEAR(wheel.bounds.center().y,0.f,.012f);
        REQUIRE_NEAR(wheel.bounds.center().z,0.f,.012f);
        REQUIRE_NEAR(wheel.bounds.size().y,.630f,.004f);
    }
    apricot_test::pass("BWC 360 has two clear door apertures, six separate panes and centered wheel assets");
    const auto layout=vehicle_driver_door(id);
    for (float side:{-1.f,1.f}) {
        auto hinge=layout.hinge;hinge.x*=side;
        auto handle=layout.handle;handle.x*=side;
        for (int yaw=0;yaw<360;yaw+=60) {
            Transform root;root.position={12,4,-8};root.set_euler_deg(5,float(yaw),-3);
            for (int i=0;i<=20;++i) {
                const auto door=side>0?vehicle_driver_door_transform(id,root,float(i)/20.f)
                                      :vehicle_passenger_door_transform(id,root,float(i)/20.f);
                REQUIRE(glm::distance(root.transform_point(hinge),door.transform_point(hinge))<1e-4f);
                REQUIRE(glm::dot(door.transform_point(handle)-root.transform_point(handle),root.rotate({side,0,0}))>=-1e-4f);
            }
        }
    }
    const Transform identity;
    REQUIRE(glm::distance(vehicle_passenger_door_transform(id,identity,std::numeric_limits<float>::quiet_NaN()).position,identity.position)<1e-6f);
    const auto profile=vehicle_headlight_profile(player_car_definition(id).mesh_path);
    REQUIRE(profile.exposed());
    for (std::size_t side=0;side<2;++side) {glm::vec3 origin;REQUIRE(vehicle_headlight_origin(body,profile,side,origin));}
    apricot_test::pass("both doors rotate outward on fixed hinges and headlights fit actual surfaces");
    const auto tuning=player_model_tuning(DrivingMechanicsStyle::ClassicGta,id);
    REQUIRE_NEAR(tuning.half_wheelbase,1.285f,1e-6f);
    REQUIRE_NEAR(tuning.half_track,.755f,1e-6f);
    REQUIRE_NEAR(tuning.mass_kg,1430.f,1e-6f);
    REQUIRE_NEAR(tuning.engine_peak_torque,455.7f,.01f);
    REQUIRE_NEAR(tuning.front_drive_bias,0.f,1e-6f);
    REQUIRE(tuning.tyre_tail_grip>=.94f);
    REQUIRE(tuning.tyre_falloff<=.20f);
    REQUIRE(tuning.differential_coupling>=105.f);
    REQUIRE(tuning.max_steer<=.37f);
    for(std::size_t style=0;style<kDrivingMechanicsStyleCount;++style) {
        const auto style_tuning=player_model_tuning(static_cast<DrivingMechanicsStyle>(style),id);
        REQUIRE_NEAR(style_tuning.half_wheelbase,1.285f,1e-6f);
        REQUIRE_NEAR(style_tuning.half_track,.755f,1e-6f);
        REQUIRE_NEAR(style_tuning.wheel_radius,.315f,1e-6f);
        REQUIRE_NEAR(style_tuning.front_drive_bias,0.f,1e-6f);
        for(const int wheel:{kWheelFrontLeft,kWheelFrontRight})
            for(const float direction:{-1.f,1.f})
                REQUIRE(std::abs(wheel_steer_angle(style_tuning,direction*style_tuning.max_steer,wheel))<=.48f);
    }
    REQUIRE(vehicle_driver_layout(id).hip.x>0.f);
    const auto handling=measure_handling();
    std::printf("  BWC 360 launch8=%.2f m/s top60=%.2f m/s stop25=%.2f m turn18=%.2f rad\n",
                handling.launch_8s,handling.top_60s,handling.stop_25,handling.turn_18);
    REQUIRE(handling.launch_8s>37.f);
    REQUIRE(handling.top_60s>69.f);
    REQUIRE(handling.stop_25>14.f && handling.stop_25<18.f);
    REQUIRE(handling.turn_18>1.80f);
    apricot_test::pass("BWC 360 keeps its native geometry and delivers measured rear-drive sedan handling");
    const auto traffic=read("body_traffic");
    const auto fit=make_traffic_visual_layout(body.bounds,.325f,.755f,1.27f,1.30f,body.bounds.size().z,.315f);
    const auto footprint=traffic_vehicle_footprint(TrafficVehicleKind::Bwc360);
    REQUIRE(fit.placed_body_bounds.extents().x<=footprint.half_width_m+.001f);
    REQUIRE(fit.placed_body_bounds.extents().z<=footprint.half_length_m+.001f);
    for(const auto& centre:fit.wheel_centres) REQUIRE_NEAR(centre.y,fit.wheel_radius,1e-5f);
    const auto plates=vehicle_plate_mounts(body,player_car_definition(id).mesh_path);
    REQUIRE(plates.size()==2);REQUIRE_NEAR(plates[1].centre.z,-2.233f,1e-5f);
    int moving=0,parked=0;
    for(uint64_t lane=0;lane<1000;++lane) for(uint32_t slot=0;slot<32;++slot) {
        moving+=traffic_vehicle_kind(lane,slot)==TrafficVehicleKind::Bwc360;
        parked+=parked_vehicle_kind(lane,slot)==TrafficVehicleKind::Bwc360;
    }
    REQUIRE(moving>3000 && moving<5500);REQUIRE(parked>3000 && parked<5500);
    REQUIRE(traffic.indices.size()<body.indices.size());
    apricot_test::pass("BWC appears in moving and parked traffic with grounded wheels, fitted collision and authored plates");
    return apricot_test::done("bwc_360_tests");
}
