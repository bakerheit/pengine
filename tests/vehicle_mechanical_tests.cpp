#include <cmath>
#include <limits>
#include <set>
#include "city/map.h"
#include "physics/vehicle.h"
#include "test_assert.h"

using namespace apricot;
namespace {
VehicleDamageState severe_oil() {
    VehicleDamageState damage;damage.zones[kDamageFrontCenter]=1.0f;return damage;
}
VehicleDamageState severe_fuel() {
    VehicleDamageState damage;damage.zones[kDamageRearCenter]=1.0f;return damage;
}
void leak_lifetimes_are_sampled_once_and_replayable() {
    std::set<float> oil_times,fuel_times;
    for(uint64_t key=0;key<32;++key) {
        VehicleMechanicalState oil,fuel,again;
        step_vehicle_mechanical(oil,severe_oil(),1.f,key);
        step_vehicle_mechanical(fuel,severe_fuel(),1.f,key);
        step_vehicle_mechanical(again,severe_oil(),1.f,key);
        REQUIRE(oil.oil_lifetime_s>=45.f && oil.oil_lifetime_s<=150.f);
        REQUIRE(fuel.fuel_lifetime_s>=90.f && fuel.fuel_lifetime_s<=240.f);
        REQUIRE(oil.oil_lifetime_s==again.oil_lifetime_s);
        REQUIRE(oil.oil_remaining==again.oil_remaining);
        REQUIRE(oil.fuel_remaining==1.f && oil.fuel_lifetime_s==0.f);
        REQUIRE(fuel.oil_remaining==1.f && fuel.oil_lifetime_s==0.f);
        const float lifetime=oil.oil_lifetime_s;
        const float remaining=oil.oil_remaining;
        // Moving/transferring the caller must not re-roll an existing leak.
        step_vehicle_mechanical(oil,severe_oil(),1.f,key+1000u);
        REQUIRE(oil.oil_lifetime_s==lifetime);
        REQUIRE_NEAR(oil.oil_remaining,remaining-1.f/lifetime,1e-6f);
        oil_times.insert(lifetime);fuel_times.insert(fuel.fuel_lifetime_s);
    }
    REQUIRE(oil_times.size()>24u && fuel_times.size()>24u);
    apricot_test::pass("leak lifetimes vary by identity and are sampled once per fluid");
}
void depletion_retains_history_and_responds_to_severity() {
    VehicleDamageState mild=severe_oil();mild.zones[kDamageFrontCenter]=.83f;
    VehicleMechanicalState full,slow;
    step_vehicle_mechanical(full,severe_oil(),20.f,42u);
    step_vehicle_mechanical(slow,mild,20.f,42u);
    REQUIRE_NEAR(slow.oil_lifetime_s,full.oil_lifetime_s,1e-6f);
    REQUIRE(slow.oil_remaining>full.oil_remaining);
    REQUIRE_NEAR(1.f-slow.oil_remaining,(1.f-full.oil_remaining)*.5f,1e-5f);
    const auto paused=slow;
    step_vehicle_mechanical(slow,{},1000.f,42u);
    REQUIRE(slow.oil_remaining==paused.oil_remaining);
    REQUIRE(slow.oil_lifetime_s==paused.oil_lifetime_s);
    step_vehicle_mechanical(slow,severe_oil(),1.f,42u);
    REQUIRE(slow.oil_remaining<paused.oil_remaining);
    REQUIRE(slow.oil_lifetime_s==paused.oil_lifetime_s);
    step_vehicle_mechanical(full,severe_oil(),full.oil_lifetime_s,42u);
    REQUIRE(full.oil_remaining==0.f && vehicle_engine_failed(full));
    step_vehicle_mechanical(full,{},100.f,42u);
    REQUIRE(full.engine_failed && full.oil_remaining==0.f);

    VehicleMechanicalState fuel;
    step_vehicle_mechanical(fuel,severe_fuel(),1.f,91u);
    step_vehicle_mechanical(fuel,severe_fuel(),fuel.fuel_lifetime_s,91u);
    REQUIRE(fuel.engine_failed && fuel.fuel_remaining==0.f);
    VehicleMechanicalState coolant;
    VehicleDamageState cooling;cooling.zones[kDamageFrontCenter]=.6f;
    step_vehicle_mechanical(coolant,cooling,1000.f,1u);
    REQUIRE(!coolant.engine_failed && coolant.oil_remaining==1.f && coolant.fuel_remaining==1.f);
    const auto before=slow;
    for(float dt:{0.f,-1.f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()})
        step_vehicle_mechanical(slow,severe_oil(),dt,42u);
    REQUIRE(slow.oil_remaining==before.oil_remaining);
    apricot_test::pass("mild leaks last longer, depletion persists, either oil or fuel can stop the engine");
}
void fixed_step_and_checkpoint_replay_preserve_failure() {
    VehicleMechanicalState a,b;
    for(int tick=0;tick<12000;++tick) {
        step_vehicle_mechanical(a,severe_oil(),1.f/120.f,123u);
        step_vehicle_mechanical(b,severe_oil(),1.f/120.f,123u);
        REQUIRE(a.oil_remaining==b.oil_remaining);
        REQUIRE(a.engine_failed==b.engine_failed);
    }
    b=a;
    for(int tick=0;tick<12000;++tick) {
        step_vehicle_mechanical(a,severe_oil(),1.f/120.f,123u);
        step_vehicle_mechanical(b,severe_oil(),1.f/120.f,123u);
    }
    REQUIRE(a.engine_failed && b.engine_failed);
    REQUIRE(a.oil_remaining==b.oil_remaining);
    apricot_test::pass("fixed-step leak countdown and checkpoint continuation replay exactly");
}
void failed_engine_cannot_drive_but_car_still_coasts_brakes_and_steers() {
    TerrainCollider ground(city::kMapSeed);
    VehicleTuning tuning;
    tuning.arcade_reverse=true;
    auto car=spawn_vehicle(tuning,ground,0.f,0.f,0.f);
    const uint64_t key=car.mechanical_key;
    car.body_damage=severe_oil();
    car.mechanical.oil_lifetime_s=45.f;
    car.mechanical.oil_remaining=.000001f;
    InputFrame gas;gas.throttle=1.f;
    car=step_vehicle(car,tuning,gas,ground,1.f/120.f);
    REQUIRE(car.mechanical.engine_failed && car.engine_rpm==0.f);
    for(int tick=0;tick<240;++tick) car=step_vehicle(car,tuning,gas,ground,1.f/120.f);
    REQUIRE(std::fabs(vehicle_speed(car))<.3f);
    InputFrame reverse;reverse.brake=1.f;
    for(int tick=0;tick<240;++tick) car=step_vehicle(car,tuning,reverse,ground,1.f/120.f);
    REQUIRE(car.gear==kGearReverse);
    REQUIRE(car.engine_rpm==0.f && std::fabs(vehicle_speed(car))<.3f);

    auto coasting=spawn_vehicle(tuning,ground,0.f,0.f,0.f);
    coasting.mechanical.engine_failed=true;
    coasting.velocity={0.f,0.f,-12.f};
    for(auto& wheel:coasting.wheels) wheel.angular_velocity=12.f/tuning.wheel_radius;
    auto braking=coasting;
    InputFrame brake; brake.brake=1.f;
    for(int tick=0;tick<120;++tick) {
        coasting=step_vehicle(coasting,tuning,{},ground,1.f/120.f);
        braking=step_vehicle(braking,tuning,brake,ground,1.f/120.f);
    }
    REQUIRE(vehicle_speed(coasting)>8.f);
    REQUIRE(vehicle_speed(braking)<vehicle_speed(coasting)-2.f);
    InputFrame turn;turn.steer=.5f;
    const auto steering=step_vehicle(coasting,tuning,turn,ground,1.f/120.f);
    REQUIRE(steering.steer_angle>0.f && steering.engine_rpm==0.f);
    repair_vehicle(car);
    REQUIRE(!vehicle_engine_failed(car.mechanical));
    REQUIRE(car.mechanical.oil_remaining==1.f && car.mechanical.fuel_remaining==1.f);
    REQUIRE(car.mechanical.oil_lifetime_s==0.f && car.mechanical.fuel_lifetime_s==0.f);
    REQUIRE(car.mechanical_key==key);
    for(int tick=0;tick<240;++tick) car=step_vehicle(car,tuning,gas,ground,1.f/120.f);
    REQUIRE(car.engine_rpm>0.f && vehicle_speed(car)>1.f);
    apricot_test::pass("failed engine cuts forward/reverse torque, preserves control, and repair restores it");
}
}
int main() {
    leak_lifetimes_are_sampled_once_and_replayable();
    depletion_retains_history_and_responds_to_severity();
    fixed_step_and_checkpoint_replay_preserve_failure();
    failed_engine_cannot_drive_but_car_still_coasts_brakes_and_steers();
    return apricot_test::done("vehicle_mechanical_tests");
}
