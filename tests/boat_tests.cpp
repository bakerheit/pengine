#include <cstdio>
#include <limits>
#include "game/boat.h"
#include "city/marina.h"
#include "test_assert.h"
using namespace apricot;
int main() {
    TerrainCollider water{city::kMapSeed};
    BoatState home;home.position={-2200,0,-700};
    REQUIRE(boat_clear(home,home,water));
    REQUIRE(boat_in_boarding_range(home,boat_point(home,{2.6f,.66f,-1.55f})));
    REQUIRE(!boat_in_boarding_range(home,boat_point(home,{0,.66f,2})));
    REQUIRE(!boat_in_boarding_range(home,boat_point(home,{2.6f,-2,-1.55f})));
    auto turned=home;turned.yaw=1.4f;
    REQUIRE(boat_in_boarding_range(turned,boat_point(turned,{-2.6f,.66f,-1.55f})));
    auto a=home,b=home;
    InputFrame gas{};gas.throttle=1;
    for(int i=0;i<600;++i) {
        a=step_boat(a,gas,water,1.f/120);b=step_boat(b,gas,water,1.f/120);
        REQUIRE(a.position==b.position && a.velocity==b.velocity);
        REQUIRE(!a.blocked && a.position.y==0);
    }
    REQUIRE(a.speed>12 && a.position.z>home.position.z+25);
    REQUIRE(!boat_stopped(a));
    const float speed=a.speed;
    for(int i=0;i<120;++i) a=step_boat(a,{},water,1.f/120);
    REQUIRE(a.speed<speed && a.speed>0);
    InputFrame turn=gas;turn.steer=1;
    for(int i=0;i<120;++i) a=step_boat(a,turn,water,1.f/120);
    REQUIRE(a.yaw<-.3f && boat_forward(a).x<0);
    InputFrame stop{};stop.handbrake=1;
    for(int i=0;i<600;++i) a=step_boat(a,stop,water,1.f/120);
    REQUIRE(boat_stopped(a));
    auto reverse=home;InputFrame back{};back.brake=1;
    for(int i=0;i<360;++i) reverse=step_boat(reverse,back,water,1.f/120);
    REQUIRE(reverse.speed< -1 && reverse.position.z<home.position.z-2);
    back.steer=1;reverse=step_boat(reverse,back,water,1.f/120);REQUIRE(reverse.yaw>0);
    auto idle=home;
    for(int i=0;i<120;++i) idle=step_boat(idle,{},water,1.f/120);
    REQUIRE(idle.position==home.position);
    REQUIRE(step_boat(home,gas,water,0).position==home.position);
    REQUIRE(step_boat(home,gas,water,std::numeric_limits<float>::quiet_NaN()).position==home.position);
    TerrainCollider obstacle{city::kMapSeed};
    obstacle.add_static_oriented_box(home.position+glm::vec3{0,.3f,8},{3,1,.08f},.2f);
    auto hit=home;
    for(int i=0;i<600 && !hit.blocked;++i) hit=step_boat(hit,gas,obstacle,1.f/120);
    REQUIRE(hit.blocked && boat_stopped(hit));REQUIRE(hit.position.z<home.position.z+5);
    // A slender piling midway between longitudinal samples must still hit.
    TerrainCollider piling{city::kMapSeed};
    piling.add_static_oriented_box(home.position+glm::vec3{1.16f,0,-1.1f},{.1f,2,.1f},0);
    REQUIRE(!boat_clear(home,home,piling));
    REQUIRE(!boat_landing_support(piling,home.position+glm::vec3{1.16f,0,-1.1f}).hit);
    TerrainCollider dock{city::kMapSeed};
    dock.add_static_ground_rect({home.position.x,home.position.z},.66f,{2,2},0);
    REQUIRE(boat_landing_support(dock,home.position).hit);
    REQUIRE(!boat_landing_support(dock,home.position+glm::vec3{1.9f,0,0}).hit);
    REQUIRE(!boat_landing_support(water,home.position).hit);
    BoatState ashore;ashore.position={0,0,0};
    REQUIRE(!boat_clear(ashore,ashore,water));
    apricot_test::pass("boat throttle, reverse, drag, steering, stop, determinism, boarding and hull collision");
    return apricot_test::done("boat_tests");
}
