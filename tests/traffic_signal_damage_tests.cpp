#include <cstdio>
#include "app/road_sign_mesh.h"
#include "game/roadside_fixture_debris.h"
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
    const glm::vec3 base{x,y,z};
    std::array<Transform,2> standing{};
    standing[0].position=base+glm::vec3{0,3,0};
    standing[0].scale={.2f,6,.2f};
    standing[1].position=base+glm::vec3{0,6,.4f};
    standing[1].scale={.8f,.5f,.5f};
    const AABB unit{{-.5f,-.5f,-.5f},{.5f,.5f,.5f}};
    const std::array<AABB,2> bounds{unit,unit};
    RoadsideDebrisState debris;
    start_roadside_debris(debris,standing,bounds,hard.breakaway_velocity);
    const auto deterministic_start=debris;
    const glm::vec3 high_start=debris.pieces[1].pose.position;
    for(int i=0;i<102;++i)
        step_roadside_debris(debris,world,static_cast<float>(kSimDt));
    REQUIRE(glm::distance(debris.pieces[1].pose.position,high_start)>1.f);
    REQUIRE(!debris.pieces[1].touched_ground); // still airborne at old freeze time
    const glm::vec3 after_old_freeze=debris.pieces[1].pose.position;
    for(int i=0;i<24;++i)
        step_roadside_debris(debris,world,static_cast<float>(kSimDt));
    REQUIRE(glm::distance(debris.pieces[1].pose.position,after_old_freeze)>.05f);
    const auto debris_at_126=debris;
    RoadsideDebrisState replay_debris=deterministic_start;
    for(int i=0;i<126;++i)
        step_roadside_debris(replay_debris,world,static_cast<float>(kSimDt));
    REQUIRE(replay_debris.pieces[0].pose.position==
            debris_at_126.pieces[0].pose.position);
    REQUIRE(replay_debris.pieces[1].pose.position==
            debris_at_126.pieces[1].pose.position);
    REQUIRE(replay_debris.pieces[1].pose.rotation==
            debris_at_126.pieces[1].pose.rotation);
    for(int i=0;i<720;++i)
        step_roadside_debris(debris,world,static_cast<float>(kSimDt));
    REQUIRE(roadside_debris_resting_orientation(
        debris.pieces[0],glm::vec3{0,1,0}));
    for(std::size_t i=0;i<debris.piece_count;++i) {
        REQUIRE(debris.pieces[i].touched_ground);
        REQUIRE(debris.pieces[i].sleeping);
        for(int corner=0;corner<8;++corner) {
            const auto& b=debris.pieces[i].local_bounds;
            const glm::vec3 local{
                (corner&1)?b.max.x:b.min.x,
                (corner&2)?b.max.y:b.min.y,
                (corner&4)?b.max.z:b.min.z};
            const glm::vec3 p=debris.pieces[i].pose.transform_point(local);
            REQUIRE(p.y>=world.height(p.x,p.z)-.01f);
        }
    }

    // A detached sign face has its authored origin down at the pole foot and
    // its geometry several metres above it. It must rotate around its own
    // centre and land broad-side-down instead of sleeping on an edge in midair.
    std::array<Transform,1> plate_standing{};
    plate_standing[0].position=base;
    const std::array<AABB,1> plate_bounds{AABB{
        {-0.65f,1.85f,0.05f},{0.65f,3.15f,0.07f}}};
    RoadsideDebrisState plate_debris;
    start_roadside_debris(plate_debris,plate_standing,plate_bounds,
                          hard.breakaway_velocity);
    for(int i=0;i<720;++i)
        step_roadside_debris(plate_debris,world,static_cast<float>(kSimDt));
    REQUIRE(plate_debris.pieces[0].touched_ground);
    REQUIRE(plate_debris.pieces[0].sleeping);
    REQUIRE(roadside_debris_resting_orientation(
        plate_debris.pieces[0],glm::vec3{0,1,0}));
    const glm::vec3 plate_centre=plate_debris.pieces[0].pose.transform_point(
        plate_bounds[0].center());
    REQUIRE(plate_centre.y<y+.12f);

    std::array<Transform,kRoadSignPartCount> sign_standing{};
    std::array<AABB,kRoadSignPartCount> sign_bounds{};
    for(std::size_t i=0;i<sign_standing.size();++i) {
        sign_standing[i].position=base;
        sign_bounds[i]=plate_bounds[0];
    }
    RoadsideDebrisState sign_debris;
    start_roadside_debris(sign_debris,sign_standing,sign_bounds,
                          hard.breakaway_velocity);
    REQUIRE(weld_roadside_debris_piece(
        sign_debris,kRoadSignWhitePart,kRoadSignBackingPart));
    REQUIRE(weld_roadside_debris_piece(
        sign_debris,kRoadSignRedPart,kRoadSignBackingPart));
    for(int i=0;i<240;++i)
        step_roadside_debris(sign_debris,world,static_cast<float>(kSimDt));
    REQUIRE(sign_debris.pieces[kRoadSignWhitePart].pose.position==
            sign_debris.pieces[kRoadSignBackingPart].pose.position);
    REQUIRE(sign_debris.pieces[kRoadSignWhitePart].pose.rotation==
            sign_debris.pieces[kRoadSignBackingPart].pose.rotation);
    REQUIRE(sign_debris.pieces[kRoadSignRedPart].pose.position==
            sign_debris.pieces[kRoadSignBackingPart].pose.position);
    REQUIRE(sign_debris.pieces[kRoadSignRedPart].pose.rotation==
            sign_debris.pieces[kRoadSignBackingPart].pose.rotation);
    while(!debris.expired)
        step_roadside_debris(debris,world,static_cast<float>(kSimDt));
    REQUIRE(!debris.active);
    REQUIRE(debris.age_seconds>=kRoadsideDebrisLifetimeSeconds);

    TrafficSignalDamage lamp_damage;
    REQUIRE(!lamp_damage.hit({kStreetLampBreakSpeed - .01f, 0, 0},
                             kStreetLampBreakSpeed));
    REQUIRE(lamp_damage.hit({kStreetLampBreakSpeed, 0, 0},
                            kStreetLampBreakSpeed));
    TrafficSignalDamage stop_damage;
    REQUIRE(!stop_damage.hit({0, 0, kStopSignBreakSpeed - .01f},
                             kStopSignBreakSpeed));
    REQUIRE(stop_damage.hit({0, 0, kStopSignBreakSpeed},
                            kStopSignBreakSpeed));
    const auto saved=damage;
    // Stream activity never owns or clears session state. A restored copy
    // keeps the broken state and a new session starts standing.
    REQUIRE(saved.broken);
    damage=TrafficSignalDamage{};
    REQUIRE(!damage.broken);
    REQUIRE(world.set_kinematic_enabled(slot,true));
    REQUIRE(drive(2.f).position.z>z+.5f);
    apricot_test::pass("signals, street lamps and stop signs fall under physics, settle, expire and reset");
    return apricot_test::done("traffic_signal_damage_tests");
}
