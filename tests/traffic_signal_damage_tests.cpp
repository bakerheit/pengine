#include <cstdio>
#include "game/traffic_signal_damage.h"
#include "physics/vehicle.h"
#include "physics/breakaway_contact.h"
#include "physics/terrain_collider.h"
#include "core/fixed_step.h"
#include "test_assert.h"
using namespace apricot;

int main() {
    TerrainCollider world(42);
    VehicleTuning tuning;
    const float x=100, z=100;
    const float y=world.height(x,z);
    const AABB pole{{x-.1f,y,z-.1f},{x+.1f,y+6,z+.1f}};
    const auto slot=world.add_kinematic_box(pole);
    REQUIRE(world.set_kinematic_breakaway(slot,17,kSignalBreakSpeed));
    auto drive = [&](float speed, float dx=0.f, float elevation=0.f) {
        auto car=spawn_vehicle(tuning,world,x+dx,z+tuning.car_collision_half_length+.14f,0);
        car.position.y+=elevation;
        car.velocity={0,0,-speed};
        for(int i=0;i<120;++i) {
            car=step_vehicle(car,tuning,InputFrame{},world,static_cast<float>(kSimDt));
            if(car.breakaway_id!=UINT32_MAX) break;
        }
        return car;
    };
    const auto brush=drive(2.f);
    REQUIRE(brush.breakaway_id==UINT32_MAX);
    REQUIRE(brush.position.z>z+tuning.car_collision_half_length);
    const auto hard=drive(12.f);
    REQUIRE(hard.breakaway_id==17);
    REQUIRE(hard.velocity.z < -7.f);
    REQUIRE(hard.impact_count>0);
    REQUIRE(hard.health<100.f);
    REQUIRE(world.static_boxes()[slot].enabled); // query never mutates world
    const auto replay=drive(12.f);
    REQUIRE(replay.breakaway_id==hard.breakaway_id);
    REQUIRE(replay.velocity==hard.velocity);
    REQUIRE(replay.position==hard.position);
    REQUIRE(drive(12.f,4.f).breakaway_id==UINT32_MAX);
    REQUIRE(drive(12.f,0.f,12.f).breakaway_id==UINT32_MAX);
    REQUIRE(drive(55.f).breakaway_id==17);
    // Side brushes have a side normal, not a centre-to-pole diagonal which
    // would turn longitudinal speed into a false heavy impact.
    const auto side=breakaway_contact({0,0,0},{1,0,0,0},{1,2.2f},{1.05f,0,-1.8f},.1f);
    REQUIRE(side.hit);REQUIRE_NEAR(side.normal.x,-1,1e-5);
    REQUIRE_NEAR(glm::dot(side.normal,glm::vec3{0,0,-15}),0,1e-5);
    const auto nose=breakaway_contact({0,0,0},{1,0,0,0},{1,2.2f},{0,0,-2.25f},.1f);
    REQUIRE(nose.hit);REQUIRE_NEAR(nose.normal.z,1,1e-5);

    TrafficSignalDamage damage;
    damage.lane_key=123456;
    REQUIRE(!damage.hit({0,0,2}));
    REQUIRE(damage.hit(hard.breakaway_velocity));
    REQUIRE(!damage.hit({12,0,0}));
    REQUIRE(world.set_kinematic_enabled(slot,false));
    auto through=hard;
    for(int i=0;i<100;++i)
        through=step_vehicle(through,tuning,InputFrame{},world,static_cast<float>(kSimDt));
    REQUIRE(through.position.z<z-2.f);
    REQUIRE(through.breakaway_id==UINT32_MAX);
    for(int i=0;i<240;++i) damage.step(static_cast<float>(kSimDt));
    REQUIRE(damage.fall_seconds==kSignalFallSeconds);
    const glm::vec3 base{x,y,z};
    const auto fallen=damage.pose(base);
    REQUIRE_NEAR(glm::length(fallen.transform_point(base)-base),0,1e-4);
    const auto top=fallen.transform_point(base+glm::vec3{0,6,0});
    REQUIRE_NEAR(top.z,z-6,1e-4);
    REQUIRE_NEAR(top.y,y,1e-4);
    for(const glm::vec3 direction : {glm::vec3{1,0,0},{-1,0,0},{0,0,1},{1,0,1}}) {
        TrafficSignalDamage other;
        REQUIRE(other.hit(direction*12.f));
        other.step(kSignalFallSeconds);
        const auto tip=other.pose(base).transform_point(base+glm::vec3{0,6,0});
        REQUIRE_NEAR(glm::distance(tip,base+glm::normalize(direction)*6.f),0,1e-4);
    }
    Transform housing;housing.position=base+glm::vec3{3,5,0};
    Transform lens;lens.position=housing.position+glm::vec3{0,0,.2f};
    REQUIRE_NEAR(glm::length((fallen*housing).position-(fallen*lens).position),.2,1e-4);
    const auto saved=damage;
    // Stream activity never owns or clears session state. A restored copy
    // reproduces the exact pose and a new session starts standing.
    for(int i=0;i<2000;++i)damage.step(static_cast<float>(kSimDt));
    REQUIRE(damage.pose(base).position==saved.pose(base).position);
    damage=TrafficSignalDamage{};
    REQUIRE(!damage.broken);
    REQUIRE(world.set_kinematic_enabled(slot,true));
    REQUIRE(drive(2.f).position.z>z+.5f);
    apricot_test::pass("solid brushes, real hard contact, pass-through, height, deterministic release, attached pose and reset");
    return apricot_test::done("traffic_signal_damage_tests");
}
