#include <cstdio>
#include "game/aircraft.h"
#include "test_assert.h"
using namespace apricot;

int main() {
    TerrainCollider field{42};
    field.add_static_ground_rect({0,0},200,{8000,8000},0,Surface::Rock);
    AircraftState parked;
    parked.position = {0,200,0};
    REQUIRE(aircraft_can_exit(parked));
    REQUIRE(aircraft_in_boarding_range(parked, {-2.7f,200,10.3f},200));
    REQUIRE(!aircraft_in_boarding_range(parked, {2.7f,200,10.3f},200));
    REQUIRE(!aircraft_in_boarding_range(parked, {-2.7f,204,10.3f},200));
    REQUIRE(!aircraft_in_boarding_range(parked, {-2.7f,200,-10.3f},200));
    auto rotated = parked; rotated.yaw = 1.3f;
    REQUIRE(aircraft_in_boarding_range(rotated,
        aircraft_point(rotated, {-2.7f,0,10.3f}),200));
    auto idle = parked;
    for (int i=0;i<1200;++i) idle=step_aircraft(idle,{},field,1.f/120);
    REQUIRE(glm::length(idle.position-parked.position)<.0001f);
    REQUIRE(idle.grounded && !idle.crashed);
    InputFrame takeoff{}; takeoff.throttle=1; takeoff.held=kBtnShiftUp;
    auto a=parked, b=parked;
    for (int i=0;i<1200;++i) {
        a=step_aircraft(a,takeoff,field,1.f/120);
        b=step_aircraft(b,takeoff,field,1.f/120);
        REQUIRE(a.position==b.position && a.velocity==b.velocity);
    }
    REQUIRE(!a.grounded && !a.crashed);
    REQUIRE(a.position.y>215);
    REQUIRE(!aircraft_can_exit(a));
    REQUIRE(!aircraft_in_boarding_range(a,aircraft_point(a,{-2.7f,0,10.3f}),a.position.y));
    const float start_yaw=a.yaw;
    InputFrame bank{}; bank.steer=1;
    for(int i=0;i<240;++i) a=step_aircraft(a,bank,field,1.f/120);
    REQUIRE(a.yaw<start_yaw && a.roll>.1f);
    // +Z is forward, so a right turn points toward local -X.
    REQUIRE(aircraft_forward(a).x<0);
    // A normal powered approach touches down, levels, and brakes to a stop.
    AircraftState landing; landing.position={0,201,0}; landing.grounded=false;
    landing.speed=40; landing.velocity={0,-2,40}; landing.pitch=-.05f;
    landing.throttle=.3f;
    InputFrame descend{}; descend.held=kBtnShiftDown;
    for(int i=0;i<1000 && !landing.grounded;++i)
        landing=step_aircraft(landing,descend,field,1.f/120);
    REQUIRE(landing.grounded && !landing.crashed);
    InputFrame stop{}; stop.brake=1; stop.handbrake=1;
    for(int i=0;i<1200;++i) landing=step_aircraft(landing,stop,field,1.f/120);
    REQUIRE(aircraft_can_exit(landing));
    auto stall=parked; stall.position.y+=40; stall.grounded=false;
    for(int i=0;i<180;++i) stall=step_aircraft(stall,{},field,1.f/120);
    REQUIRE(stall.position.y<230);
    auto crash=parked; crash.position.y+=.02f; crash.grounded=false;
    crash.velocity={0,-15,35}; crash.speed=35;
    crash=step_aircraft(crash,{},field,1.f/120);
    REQUIRE(crash.crashed && crash.speed==0);
    // A wing tip collision must be caught even with a clear fuselage path.
    TerrainCollider wall{42};
    wall.add_static_ground_rect({0,0},200,{8000,8000},0,Surface::Rock);
    wall.add_static_oriented_box({13.8f,203.3f,-4},{.3f,1,.1f},0);
    auto taxi=parked; taxi.speed=20; taxi.velocity={0,0,20};
    for(int i=0;i<30 && !taxi.crashed;++i) taxi=step_aircraft(taxi,{},wall,1.f/120);
    REQUIRE(taxi.crashed);
    apricot_test::pass("aircraft boarding, parking, deterministic takeoff, bank, landing, stall and wing collision");
    return apricot_test::done("aircraft_tests");
}
