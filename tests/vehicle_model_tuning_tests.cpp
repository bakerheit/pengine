#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

#include <glm/geometric.hpp>

#include "app/player_car_catalog.h"
#include "app/vehicle_model_tuning.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr float kDt = 1.f / 120.f;

TerrainCollider flat_ground() {
    TerrainCollider ground(0xC4A5u);
    ground.add_static_ground_rect({0.f, 0.f}, 200.f, {6000.f, 6000.f},
                                  0.f, Surface::Rock);
    return ground;
}

VehicleState road_speed_car(const VehicleTuning& tuning,
                            const TerrainCollider& ground, float speed) {
    VehicleState car = spawn_vehicle(tuning, ground, 0.f, 0.f, 0.f);
    car.position.y = 200.f + static_ride_height(tuning);
    car.velocity = vehicle_forward(car) * speed;
    for (auto& wheel : car.wheels) wheel.angular_velocity = speed / tuning.wheel_radius;
    return car;
}

struct LongitudinalMetrics {
    float launch_speed_8s = 0.f;
    float top_speed_60s = 0.f;
};

LongitudinalMetrics measure_longitudinal(PlayerCarId id) {
    auto ground = flat_ground();
    const auto tuning = player_model_tuning(DrivingMechanicsStyle::ClassicGta, id);
    auto car = spawn_vehicle(tuning, ground, 0.f, 0.f, 0.f);
    car.position.y = 200.f + static_ride_height(tuning);
    InputFrame input;
    input.throttle = 1.f;
    LongitudinalMetrics result;
    for (int step = 0; step < 60 * 120; ++step) {
        car = step_vehicle(car, tuning, input, ground, kDt);
        REQUIRE(std::isfinite(vehicle_speed(car)));
        REQUIRE(vehicle_up(car).y > .75f);
        result.top_speed_60s = std::max(result.top_speed_60s,
                                        std::abs(vehicle_speed(car)));
        if (step == 8 * 120 - 1) {
            result.launch_speed_8s = std::abs(vehicle_speed(car));
        }
    }
    return result;
}

float measure_stop_distance(PlayerCarId id, float entry_speed) {
    auto ground = flat_ground();
    const auto tuning = player_model_tuning(DrivingMechanicsStyle::ClassicGta, id);
    auto car = road_speed_car(tuning, ground, entry_speed);
    const glm::vec3 start = car.position;
    InputFrame input;
    input.brake = 1.f;
    for (int step = 0; step < 10 * 120; ++step) {
        car = step_vehicle(car, tuning, input, ground, kDt);
        if (std::abs(vehicle_speed(car)) < .3f) {
            return glm::length(glm::vec2(car.position.x - start.x,
                                         car.position.z - start.z));
        }
    }
    return 10000.f;
}

float measure_turn_heading(PlayerCarId id, float entry_speed) {
    auto ground = flat_ground();
    const auto tuning = player_model_tuning(DrivingMechanicsStyle::ClassicGta, id);
    auto car = road_speed_car(tuning, ground, entry_speed);
    const glm::vec3 start_forward = vehicle_forward(car);
    InputFrame input;
    input.throttle = .25f;
    input.steer = 1.f;
    for (int step = 0; step < 2 * 120; ++step) {
        car = step_vehicle(car, tuning, input, ground, kDt);
        REQUIRE(vehicle_up(car).y > .70f);
    }
    const glm::vec3 finish_forward = vehicle_forward(car);
    return std::acos(std::clamp(glm::dot(start_forward, finish_forward),
                                -1.f, 1.f));
}

bool materially_same(const VehicleTuning& a, const VehicleTuning& b) {
    return a.mass_kg == b.mass_kg &&
           a.engine_peak_torque == b.engine_peak_torque &&
           a.final_drive == b.final_drive &&
           a.brake_torque == b.brake_torque &&
           a.max_steer == b.max_steer &&
           a.steer_rate == b.steer_rate &&
           a.steer_speed_falloff == b.steer_speed_falloff &&
           a.lateral_grip_scale == b.lateral_grip_scale &&
           a.spring_k == b.spring_k &&
           a.anti_roll_front == b.anti_roll_front &&
           a.com_height_above_mount == b.com_height_above_mount &&
           a.drag == b.drag &&
           a.front_drive_bias == b.front_drive_bias;
}

void catalog_has_one_distinct_tune_per_model() {
    std::array<bool, kPlayerCarCount> seen{};
    for (const auto& model : kPlayerCars) {
        const auto raw = static_cast<std::size_t>(model.id);
        REQUIRE(raw < kPlayerCarCount);
        REQUIRE(!seen[raw]);
        seen[raw] = true;

        const auto profile = player_car_performance_profile(model.id);
        REQUIRE(profile.mass_kg >= (is_motorbike(model.id)?200.f:900.f) &&
                profile.mass_kg <= (model.id==PlayerCarId::HarrowRearloader?10500.f:10000.f));
        REQUIRE(profile.engine_torque_scale >= (is_motorbike(model.id)?.50f:.60f) &&
                profile.engine_torque_scale <= 4.5f);
        REQUIRE(profile.top_speed_scale >= (model.id==PlayerCarId::HarrowRearloader?.23f:.35f) &&
                profile.top_speed_scale <= 1.25f);
        REQUIRE(profile.brake_scale >= .70f && profile.brake_scale <= 1.20f);
        // apply_vehicle_impact clamps this to [0, 1], so a row above 1 is data
        // that reads as tuning and does nothing. FangVenom shipped 1.18 and
        // dented exactly like 1.f. Catch the next one here rather than in a
        // catalog-wide invariant somewhere else that goes red months later.
        REQUIRE_MSG(profile.body_damage_gain > 0.f &&
                    profile.body_damage_gain <= 1.f,
                    "body_damage_gain must sit in (0, 1]; above 1 is discarded",
                    model.model);
    }
    for (std::size_t i=0;i<seen.size();++i) {
        const auto id=static_cast<PlayerCarId>(i);
        REQUIRE(seen[i] == (id != PlayerCarId::LegacyCruiser91CSlot));
    }

    for (std::size_t first = 0; first < kPlayerCars.size(); ++first) {
        const auto a = player_model_tuning(DrivingMechanicsStyle::ClassicGta,
                                           kPlayerCars[first].id);
        for (std::size_t second = first + 1; second < kPlayerCars.size(); ++second) {
            const auto b = player_model_tuning(DrivingMechanicsStyle::ClassicGta,
                                               kPlayerCars[second].id);
            REQUIRE(!materially_same(a, b));
        }
    }
    apricot_test::pass("all selectable models have explicit distinct tuning");
}

void measured_class_differences_are_real() {
    struct Sample {
        PlayerCarId id;
        LongitudinalMetrics longitudinal;
        float stop_25;
        float turn_18;
    };
    std::array<Sample, 6> samples{{
        {PlayerCarId::AlderPip, {}, 0.f, 0.f},
        {PlayerCarId::VesperScythe, {}, 0.f, 0.f},
        {PlayerCarId::AlderWayfarer, {}, 0.f, 0.f},
        {PlayerCarId::MontroseRegentEight, {}, 0.f, 0.f},
        {PlayerCarId::MunicipalCruiser91C, {}, 0.f, 0.f},
        {PlayerCarId::HarrowCityliner, {}, 0.f, 0.f},
    }};
    for (auto& sample : samples) {
        sample.longitudinal = measure_longitudinal(sample.id);
        sample.stop_25 = measure_stop_distance(sample.id, 25.f);
        sample.turn_18 = measure_turn_heading(sample.id, 18.f);
        const auto& model = player_car_definition(sample.id);
        std::printf("  %-18s launch8=%5.1f m/s top60=%5.1f m/s "
                    "stop25=%5.1f m turn18=%4.2f rad\n",
                    model.model, sample.longitudinal.launch_speed_8s,
                    sample.longitudinal.top_speed_60s, sample.stop_25,
                    sample.turn_18);
        REQUIRE(sample.longitudinal.launch_speed_8s > 3.f);
        REQUIRE(sample.longitudinal.top_speed_60s >
                sample.longitudinal.launch_speed_8s);
        REQUIRE(sample.stop_25 < 100.f);
        REQUIRE(sample.turn_18 > .05f);
    }

    const auto& pip = samples[0];
    const auto& scythe = samples[1];
    const auto& wayfarer = samples[2];
    const auto& regent = samples[3];
    const auto& cruiser91c = samples[4];
    const auto& bus = samples[5];
    REQUIRE(scythe.longitudinal.launch_speed_8s >
            wayfarer.longitudinal.launch_speed_8s + 5.f);
    REQUIRE(scythe.longitudinal.top_speed_60s >
            regent.longitudinal.top_speed_60s + 12.f);
    REQUIRE(scythe.longitudinal.top_speed_60s >
            bus.longitudinal.top_speed_60s + 10.f);
    REQUIRE(cruiser91c.stop_25 < regent.stop_25);
    REQUIRE(pip.turn_18 > bus.turn_18 + .10f);
    REQUIRE(scythe.turn_18 > wayfarer.turn_18);
    apricot_test::pass("measured launch, road speed, braking and turning separate vehicle classes");
}

void cinder_native_fit_and_driving() {
    const auto id=PlayerCarId::OrisonCinderGt;
    const auto& def=player_car_definition(id);
    for (std::size_t style=0;style<kDrivingMechanicsStyleCount;++style) {
        const auto tuning=player_model_tuning(static_cast<DrivingMechanicsStyle>(style),id);
        REQUIRE_NEAR(tuning.half_track/def.wheel_x,1.f,1e-6f);
        REQUIRE_NEAR(2.f*tuning.half_wheelbase/
                     (def.wheel_front_z+def.wheel_rear_z),1.f,1e-6f);
        REQUIRE_NEAR(tuning.wheel_radius,.326f,1e-6f);
        REQUIRE_NEAR(tuning.mass_kg,1340.f,1e-6f);
        REQUIRE(tuning.car_collision_half_width>=.95f);
        REQUIRE(tuning.car_collision_half_length>=2.325f);
        const float vertical_offset=def.arch_centre_y+
            static_suspension_length(tuning)+tuning.com_height_above_mount;
        REQUIRE_NEAR(tuning.chassis_roof+vertical_offset,1.245f,1e-5f);
        REQUIRE_NEAR(tuning.chassis_floor+vertical_offset,.15f,1e-5f);
        for (const int wheel:{kWheelFrontLeft,kWheelFrontRight})
            for (const float direction:{-1.f,1.f})
                REQUIRE(std::abs(wheel_steer_angle(tuning,
                    direction*tuning.max_steer,wheel))<=.82f);
    }
    const auto acceleration=measure_longitudinal(id);
    const float stop=measure_stop_distance(id,25.f);
    const float turn=measure_turn_heading(id,18.f);
    std::printf("  CINDER GT launch8=%.2f m/s top60=%.2f m/s stop25=%.2f m turn18=%.2f rad\n",
        acceleration.launch_speed_8s,acceleration.top_speed_60s,stop,turn);
    REQUIRE(acceleration.launch_speed_8s>12.f);
    REQUIRE(acceleration.top_speed_60s>acceleration.launch_speed_8s);
    REQUIRE(stop<70.f);
    REQUIRE(turn>.10f);
    apricot_test::pass("Cinder native dimensions, Ackermann clearance, acceleration, braking and steering");
}

void rearloader_has_heavy_truck_performance() {
    auto ground=flat_ground();
    const auto tuning=player_model_tuning(DrivingMechanicsStyle::ClassicGta,PlayerCarId::HarrowRearloader);
    auto car=road_speed_car(tuning,ground,0.f);
    InputFrame input;input.brake=1.f;
    for(int step=0;step<240;++step) car=step_vehicle(car,tuning,input,ground,kDt);
    input.brake=0.f;input.throttle=1.f;
    float zero_to_sixty=0.f,top=0.f;
    for(int step=0;step<60*120;++step) {
        car=step_vehicle(car,tuning,input,ground,kDt);
        const float mph=vehicle_forward_speed(car)/.44704f;
        if(zero_to_sixty==0.f && mph>=60.f) zero_to_sixty=static_cast<float>(step+1)*kDt;
        top=std::max(top,mph);
        REQUIRE(vehicle_up(car).y>.85f);
    }
    std::printf("  REARLOADER peak=%.2f mph 0-60=%.2f s\n",top,zero_to_sixty);
    // The first integration accelerated like a sports sedan. Pin the intended
    // municipal truck range with actual simulation rather than profile values.
    REQUIRE(top>=65.f && top<=75.f);
    REQUIRE(zero_to_sixty>=18.f && zero_to_sixty<=30.f);
    REQUIRE(measure_stop_distance(PlayerCarId::HarrowRearloader,25.f)<100.f);
    apricot_test::pass("Rearloader accelerates, brakes and remains upright as a heavy municipal truck");
}

}  // namespace

int main() {
    catalog_has_one_distinct_tune_per_model();
    measured_class_differences_are_real();
    cinder_native_fit_and_driving();
    rearloader_has_heavy_truck_performance();
    return apricot_test::done("vehicle_model_tuning_tests");
}
