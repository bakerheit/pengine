// Deterministic characterization of extreme driving inputs.
//
// This suite deliberately separates OBSERVATION from EXPECTATION. The outcome
// labels and metrics below describe what the current physics did; they are not
// yet claims that every result is desirable. Hard assertions cover only facts
// that must always hold: finite state, no emergency speed clamp, and exact
// repeatability. Once a handling outcome is accepted, promote its measured
// band to an explicit regression here or in vehicle_tests.cpp.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>

#include <glm/glm.hpp>

#include "app/driving_mechanics.h"
#include "core/fixed_step.h"
#include "core/input_frame.h"
#include "physics/terrain_collider.h"
#include "physics/vehicle.h"
#include "test_assert.h"

using namespace apricot;

namespace {

constexpr uint64_t kSeed = 20260823u;
constexpr float kDt = static_cast<float>(kSimDt);
constexpr float kRadiansToDegrees = 57.29577951f;

enum class ExtremeScenario : uint8_t {
    FullLock30,
    PanicBrakeTurn40,
    LeftRightFlick35,
    HandbrakeEntry28,
    CurbTripTurn30,
    WetBrakeTurn35,
};

struct ScenarioDef {
    ExtremeScenario scenario;
    const char* name;
    float entry_speed_mps;
    int duration_steps;
    bool wet;
    bool stop_when_slow;
};

constexpr std::array<ScenarioDef, 6> kScenarios{{
    {ExtremeScenario::FullLock30, "FULL_LOCK_30", 30.0f, 360, false, false},
    {ExtremeScenario::PanicBrakeTurn40, "BRAKE_TURN_40", 40.0f, 360, false, true},
    {ExtremeScenario::LeftRightFlick35, "FLICK_35", 35.0f, 480, false, false},
    {ExtremeScenario::HandbrakeEntry28, "HANDBRAKE_28", 28.0f, 420, false, false},
    {ExtremeScenario::CurbTripTurn30, "CURB_TRIP_30", 30.0f, 360, false, false},
    {ExtremeScenario::WetBrakeTurn35, "WET_BRAKE_35", 35.0f, 360, true, true},
}};

enum class ObservedOutcome : uint8_t {
    Planted,
    Stopped,
    NoStop,
    Slide,
    Spin,
    WheelLift,
    Airborne,
    Rollover,
    NonFinite,
};

const char* outcome_name(ObservedOutcome outcome) {
    switch (outcome) {
        case ObservedOutcome::Planted:   return "PLANTED";
        case ObservedOutcome::Stopped:   return "STOPPED";
        case ObservedOutcome::NoStop:    return "NO_STOP";
        case ObservedOutcome::Slide:     return "SLIDE";
        case ObservedOutcome::Spin:      return "SPIN";
        case ObservedOutcome::WheelLift: return "WHEEL_LIFT";
        case ObservedOutcome::Airborne:  return "AIRBORNE";
        case ObservedOutcome::Rollover:  return "ROLLOVER";
        case ObservedOutcome::NonFinite: return "NONFINITE";
    }
    return "NONFINITE";
}

struct ExtremeResult {
    float entry_speed_mps = 0.0f;
    float end_speed_mps = 0.0f;
    float distance_m = 0.0f;
    float minimum_up = 1.0f;
    float maximum_roll_degrees = 0.0f;
    float maximum_pitch_degrees = 0.0f;
    float maximum_body_slip_degrees = 0.0f;
    float accumulated_yaw_degrees = 0.0f;
    float maximum_roll_rate = 0.0f;
    float maximum_pitch_rate = 0.0f;
    float stop_time_seconds = -1.0f;
    int minimum_grounded_wheels = kWheelCount;
    int airborne_steps = 0;
    int inversion_events = 0;
    int simulated_steps = 0;
    bool finite = true;
    ObservedOutcome outcome = ObservedOutcome::Planted;
};

float signed_body_slip_degrees(const VehicleState& car) {
    const glm::vec3 forward3 = vehicle_forward(car);
    const glm::vec2 heading{forward3.x, forward3.z};
    const glm::vec2 travel{car.velocity.x, car.velocity.z};
    if (glm::length(heading) < 1e-3f || glm::length(travel) < 1.0f) {
        return 0.0f;
    }
    const glm::vec2 h = glm::normalize(heading);
    const glm::vec2 v = glm::normalize(travel);
    const float angle =
        std::acos(glm::clamp(glm::dot(h, v), -1.0f, 1.0f)) *
        kRadiansToDegrees;
    return (h.x * v.y - h.y * v.x < 0.0f) ? -angle : angle;
}

void find_flat_spawn(const TerrainCollider& collider, float& out_x,
                     float& out_z) {
    VehicleTuning geometry;
    float best_span = 1000000.0f;
    out_x = 0.0f;
    out_z = 0.0f;
    for (int ix = -30; ix <= 30; ++ix) {
        for (int iz = -30; iz <= 30; ++iz) {
            const float x = static_cast<float>(ix) * 5.0f;
            const float z = static_cast<float>(iz) * 5.0f;
            float low = 1000000.0f;
            float high = -1000000.0f;
            for (int wheel = 0; wheel < kWheelCount; ++wheel) {
                const float ox =
                    wheel < 2 ? -geometry.half_track : geometry.half_track;
                const float oz =
                    (wheel % 2) != 0 ? -geometry.half_wheelbase
                                     : geometry.half_wheelbase;
                const float height = collider.height(x + ox, z + oz);
                low = std::min(low, height);
                high = std::max(high, height);
            }
            if (high - low < best_span) {
                best_span = high - low;
                out_x = x;
                out_z = z;
            }
        }
    }
}

VehicleState prepared_car(const VehicleTuning& tuning,
                          const TerrainCollider& collider, float x, float z,
                          float speed_mps) {
    VehicleState car = spawn_vehicle(tuning, collider, x, z, 0.0f);
    InputFrame settle;
    for (int step = 0; step < 120; ++step) {
        car = step_vehicle(car, tuning, settle, collider, kDt);
    }

    glm::vec3 forward = vehicle_forward(car);
    forward.y = 0.0f;
    forward = glm::normalize(forward);
    car.velocity = forward * speed_mps;
    car.angular_velocity = glm::vec3{0.0f};
    car.gear = 4;
    car.engine_rpm = 4200.0f;
    for (WheelState& wheel : car.wheels) {
        wheel.angular_velocity = speed_mps / tuning.wheel_radius;
    }
    return car;
}

InputFrame scenario_input(ExtremeScenario scenario, int step) {
    InputFrame input;
    switch (scenario) {
        case ExtremeScenario::FullLock30:
            input.steer = 1.0f;
            input.throttle = 0.35f;
            break;
        case ExtremeScenario::PanicBrakeTurn40:
        case ExtremeScenario::WetBrakeTurn35:
            input.steer = 1.0f;
            input.brake = 1.0f;
            break;
        case ExtremeScenario::LeftRightFlick35:
            input.steer = ((step / 48) % 2) == 0 ? 1.0f : -1.0f;
            input.throttle = 0.40f;
            break;
        case ExtremeScenario::HandbrakeEntry28:
            if (step < 72) {
                input.steer = 0.75f;
                input.handbrake = 1.0f;
                input.throttle = 0.15f;
            } else if (step < 216) {
                input.steer = -0.65f;
                input.throttle = 0.45f;
            } else {
                input.throttle = 0.25f;
            }
            break;
        case ExtremeScenario::CurbTripTurn30:
            input.steer = 0.80f;
            input.throttle = 0.30f;
            break;
    }
    return input;
}

ObservedOutcome classify(const ExtremeResult& result,
                         const ScenarioDef& scenario) {
    if (!result.finite) return ObservedOutcome::NonFinite;
    if (result.inversion_events > 0 || result.minimum_up < 0.0f) {
        return ObservedOutcome::Rollover;
    }
    if (result.airborne_steps >= 12) return ObservedOutcome::Airborne;
    if (result.minimum_grounded_wheels <= 1 || result.minimum_up < 0.65f) {
        return ObservedOutcome::WheelLift;
    }
    if (result.accumulated_yaw_degrees > 540.0f ||
        result.maximum_body_slip_degrees > 70.0f) {
        return ObservedOutcome::Spin;
    }
    if (result.maximum_body_slip_degrees > 15.0f) {
        return ObservedOutcome::Slide;
    }
    if (result.stop_time_seconds >= 0.0f) return ObservedOutcome::Stopped;
    if (scenario.stop_when_slow) return ObservedOutcome::NoStop;
    return ObservedOutcome::Planted;
}

ExtremeResult run_scenario(DrivingMechanicsStyle style,
                           const ScenarioDef& scenario,
                           const TerrainCollider& collider, float spawn_x,
                           float spawn_z) {
    const VehicleTuning tuning = player_vehicle_tuning(style);
    VehicleState car = prepared_car(tuning, collider, spawn_x, spawn_z,
                                    scenario.entry_speed_mps);
    ExtremeResult result;
    result.entry_speed_mps = vehicle_speed(car);
    glm::vec3 previous_position = car.position;
    glm::vec2 previous_heading{vehicle_forward(car).x,
                               vehicle_forward(car).z};
    previous_heading = glm::normalize(previous_heading);
    float previous_up = vehicle_up(car).y;

    for (int step = 0; step < scenario.duration_steps; ++step) {
        if (scenario.scenario == ExtremeScenario::CurbTripTurn30 &&
            step == 90) {
            car.angular_velocity += vehicle_forward(car) * 20.0f;
        }

        const InputFrame input = scenario_input(scenario.scenario, step);
        car = step_vehicle(car, tuning, input, collider, kDt);
        ++result.simulated_steps;

        const glm::vec3 forward3 = vehicle_forward(car);
        const glm::vec3 right3 = vehicle_right(car);
        const glm::vec3 up3 = vehicle_up(car);
        const glm::vec2 heading = glm::normalize(
            glm::vec2{forward3.x, forward3.z});
        result.distance_m += glm::distance(previous_position, car.position);
        result.accumulated_yaw_degrees +=
            std::acos(glm::clamp(glm::dot(previous_heading, heading),
                                -1.0f, 1.0f)) *
            kRadiansToDegrees;
        result.minimum_up = std::min(result.minimum_up, up3.y);
        result.maximum_roll_degrees = std::max(
            result.maximum_roll_degrees,
            std::asin(glm::clamp(std::fabs(right3.y), 0.0f, 1.0f)) *
                kRadiansToDegrees);
        result.maximum_pitch_degrees = std::max(
            result.maximum_pitch_degrees,
            std::asin(glm::clamp(std::fabs(forward3.y), 0.0f, 1.0f)) *
                kRadiansToDegrees);
        result.maximum_body_slip_degrees = std::max(
            result.maximum_body_slip_degrees,
            std::fabs(signed_body_slip_degrees(car)));
        result.maximum_roll_rate = std::max(
            result.maximum_roll_rate,
            std::fabs(glm::dot(car.angular_velocity, forward3)));
        result.maximum_pitch_rate = std::max(
            result.maximum_pitch_rate,
            std::fabs(glm::dot(car.angular_velocity, right3)));

        int grounded = 0;
        for (const WheelState& wheel : car.wheels) {
            if (wheel.grounded) ++grounded;
        }
        result.minimum_grounded_wheels =
            std::min(result.minimum_grounded_wheels, grounded);
        if (grounded == 0) ++result.airborne_steps;
        if (previous_up >= 0.0f && up3.y < 0.0f) {
            ++result.inversion_events;
        }

        result.finite = result.finite &&
                        std::isfinite(car.position.x) &&
                        std::isfinite(car.position.y) &&
                        std::isfinite(car.position.z) &&
                        std::isfinite(car.velocity.x) &&
                        std::isfinite(car.velocity.y) &&
                        std::isfinite(car.velocity.z) &&
                        std::isfinite(car.orientation.w) &&
                        std::isfinite(car.orientation.x) &&
                        std::isfinite(car.orientation.y) &&
                        std::isfinite(car.orientation.z);

        previous_position = car.position;
        previous_heading = heading;
        previous_up = up3.y;

        if (result.stop_time_seconds < 0.0f &&
            vehicle_forward_speed(car) <= 1.0f) {
            result.stop_time_seconds =
                static_cast<float>(step + 1) * kDt;
            if (scenario.stop_when_slow) break;
        }
    }

    result.end_speed_mps = vehicle_speed(car);
    result.outcome = classify(result, scenario);
    return result;
}

bool exactly_equal(const ExtremeResult& a, const ExtremeResult& b) {
    return a.entry_speed_mps == b.entry_speed_mps &&
           a.end_speed_mps == b.end_speed_mps &&
           a.distance_m == b.distance_m &&
           a.minimum_up == b.minimum_up &&
           a.maximum_roll_degrees == b.maximum_roll_degrees &&
           a.maximum_pitch_degrees == b.maximum_pitch_degrees &&
           a.maximum_body_slip_degrees == b.maximum_body_slip_degrees &&
           a.accumulated_yaw_degrees == b.accumulated_yaw_degrees &&
           a.maximum_roll_rate == b.maximum_roll_rate &&
           a.maximum_pitch_rate == b.maximum_pitch_rate &&
           a.stop_time_seconds == b.stop_time_seconds &&
           a.minimum_grounded_wheels == b.minimum_grounded_wheels &&
           a.airborne_steps == b.airborne_steps &&
           a.inversion_events == b.inversion_events &&
           a.simulated_steps == b.simulated_steps &&
           a.finite == b.finite && a.outcome == b.outcome;
}

void print_result(DrivingMechanicsStyle style, const ScenarioDef& scenario,
                  const ExtremeResult& result) {
    const float air_ms =
        static_cast<float>(result.airborne_steps) * kDt * 1000.0f;
    std::printf(
        "%-11s %-16s %-10s %5.1f %5.1f %5.1f %5.1f %6.1f %6.0f "
        "%4d %6.0f %5.2f\n",
        driving_mechanics_name(style), scenario.name,
        outcome_name(result.outcome),
        static_cast<double>(result.entry_speed_mps),
        static_cast<double>(result.end_speed_mps),
        static_cast<double>(result.maximum_roll_degrees),
        static_cast<double>(result.maximum_pitch_degrees),
        static_cast<double>(result.maximum_body_slip_degrees),
        static_cast<double>(result.accumulated_yaw_degrees),
        result.minimum_grounded_wheels, static_cast<double>(air_ms),
        static_cast<double>(result.stop_time_seconds));
}

}  // namespace

int main() {
    TerrainCollider dry(kSeed);
    TerrainCollider wet(kSeed);
    wet.set_wetness(1.0f);
    float spawn_x = 0.0f;
    float spawn_z = 0.0f;
    find_flat_spawn(dry, spawn_x, spawn_z);

    std::printf("driving_extremes_tests\n");
    std::printf("characterization only: outcome labels are observations, not "
                "accepted behavior\n");
    std::printf("spawn %.1f, %.1f | fixed step %.3f ms\n",
                static_cast<double>(spawn_x),
                static_cast<double>(spawn_z),
                static_cast<double>(kDt * 1000.0f));
    std::printf("PROFILE     SCENARIO         OUTCOME    IN    END   ROLL  PITCH "
                " SLIP    YAW DOWN AIR_MS  STOP\n");

    int rows = 0;
    for (int style_index = 0;
         style_index < static_cast<int>(kDrivingMechanicsStyleCount);
         ++style_index) {
        const DrivingMechanicsStyle style =
            static_cast<DrivingMechanicsStyle>(style_index);
        for (const ScenarioDef& scenario : kScenarios) {
            const TerrainCollider& collider = scenario.wet ? wet : dry;
            const ExtremeResult first =
                run_scenario(style, scenario, collider, spawn_x, spawn_z);
            const ExtremeResult repeat =
                run_scenario(style, scenario, collider, spawn_x, spawn_z);
            REQUIRE_MSG(first.finite,
                        "an extreme scenario produced non-finite vehicle state",
                        scenario.name);
            REQUIRE_MSG(exactly_equal(first, repeat),
                        "an extreme scenario did not repeat exactly",
                        scenario.name);
            REQUIRE_MSG(first.entry_speed_mps <
                            player_vehicle_tuning(style).max_speed,
                        "an extreme fixture reached the emergency speed clamp",
                        scenario.name);
            print_result(style, scenario, first);
            ++rows;
        }
    }

    REQUIRE(rows == static_cast<int>(kDrivingMechanicsStyleCount) *
                        static_cast<int>(kScenarios.size()));
    apricot_test::pass(
        "all extreme scenarios stay finite and repeat exactly; outcomes remain observational");
    return apricot_test::done("driving_extremes_tests");
}
