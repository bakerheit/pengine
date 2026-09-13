#include <cmath>
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
    REQUIRE(read_static_emesh(asset_path(std::string("models/vehicles/ember_gt/")+name+".emesh"),mesh));
    REQUIRE(!mesh.indices.empty());
    for (const auto& v:mesh.vertices) {
        REQUIRE(std::isfinite(v.px) && std::isfinite(v.py) && std::isfinite(v.pz));
        REQUIRE(v.u>=0 && v.u<=1 && v.v>=0 && v.v<=1);
    }
    return mesh;
}
}
int main() {
    constexpr auto id=PlayerCarId::EmberGt;
    static_assert(static_cast<unsigned>(id)==31u); // Append-only checkpoint identity.
    REQUIRE(std::string(player_car_definition(id).brand)=="EMBER");
    REQUIRE(has_animated_driver(id) && has_passenger_door(id));
    const auto body=read("body"),open=read("body_open");
    const auto driver=read("driver_door"),passenger=read("passenger_door");
    REQUIRE(body.bounds.size().z>4.5f && body.bounds.size().z<5.f);
    REQUIRE(body.bounds.max.y<1.27f && body.bounds.max.y>1.20f);
    REQUIRE(body.indices.size()/3<15000u);
    for (float side:{-1.f,1.f}) {
        const auto& door=side>0?driver:passenger;
        for (float z:{-.28f,.07f,.41f}) for (float y:{.42f,.58f}) {
            REQUIRE(!covers(open,side,z,y));
            REQUIRE(covers(door,side,z,y));
            REQUIRE(covers(body,side,z,y));
        }
        // Removing a door's glass cannot leave an opaque window backing.
        for (float z:{-.20f,.10f}) {
            REQUIRE(!covers(open,side,z,1.02f));
            REQUIRE(!covers(door,side,z,1.02f));
        }
    }
    for (const char* name:{"windshield","rear_glass","driver_glass","passenger_glass",
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
        REQUIRE_NEAR(wheel.bounds.size().y,.730f,.004f);
    }
    apricot_test::pass("Ember has two clear door apertures, six separate panes and centered wheel assets");
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
    TerrainCollider ground(123);ground.add_static_ground_rect({0,0},200,{6000,6000},0,Surface::Rock);
    const auto tuning=player_model_tuning(DrivingMechanicsStyle::ClassicGta,id);
    REQUIRE_NEAR(tuning.half_wheelbase,1.38f,1e-6f);
    REQUIRE_NEAR(tuning.half_track,.88f,1e-6f);
    auto car=spawn_vehicle(tuning,ground,0,0,0);car.position.y=200+static_ride_height(tuning);
    InputFrame input;input.throttle=1;
    for (int i=0;i<960;++i) {
        car=step_vehicle(car,tuning,input,ground,1.f/120.f);
        REQUIRE(std::isfinite(vehicle_speed(car)));REQUIRE(vehicle_up(car).y>.90f);
    }
    REQUIRE(vehicle_speed(car)>12.f);
    input.throttle=0;input.brake=1;bool stopped=false;
    for(int i=0;i<1200;++i) {
        car=step_vehicle(car,tuning,input,ground,1.f/120.f);
        if(std::abs(vehicle_speed(car))<.3f){stopped=true;break;}
    }
    REQUIRE(stopped);
    apricot_test::pass("Ember accelerates, stays upright and brakes to a stop in the real vehicle simulation");
    return apricot_test::done("ember_gt_tests");
}
