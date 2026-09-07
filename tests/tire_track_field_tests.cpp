#include <cstdio>

#include <glm/gtc/quaternion.hpp>

#include "gfx/tire_track_field.h"
#include "test_assert.h"

using namespace apricot;

namespace {

VehicleState moving_car(float speed_mps) {
    VehicleState car;
    car.position = {0.0f, 1.0f, 0.0f};
    car.orientation = glm::quat{1.0f, 0.0f, 0.0f, 0.0f};
    car.velocity = {0.0f, 0.0f, -speed_mps};
    for (int i = 0; i < kWheelCount; ++i) {
        WheelState& wheel = car.wheels[static_cast<std::size_t>(i)];
        const bool left = i == kWheelFrontLeft || i == kWheelRearLeft;
        const bool front = i == kWheelFrontLeft || i == kWheelFrontRight;
        wheel.grounded = true;
        wheel.contact_point = {left ? -0.8f : 0.8f, 0.0f,
                               front ? -1.3f : 1.3f};
        wheel.contact_normal = {0.0f, 1.0f, 0.0f};
        wheel.slip = 0.25f;
    }
    return car;
}

void ordinary_road_driving_leaves_no_rubber_marks() {
    const VehicleState car = moving_car(14.0f);
    for (int i = 0; i < kWheelCount; ++i) {
        const TireTrackEmission event = tire_track_emission(
            car, i, 0.0f, 0.0f);
        REQUIRE(!event.emit);
    }
    apricot_test::pass("ordinary road driving leaves no rubber marks");
}

void a_handbrake_drift_marks_both_rear_tyres() {
    VehicleState car = moving_car(17.0f);
    car.velocity.x = 5.0f;
    car.wheels[kWheelRearLeft].slip = 1.8f;
    car.wheels[kWheelRearRight].slip = 1.7f;

    const TireTrackEmission left = tire_track_emission(
        car, kWheelRearLeft, 1.0f, 0.0f);
    const TireTrackEmission right = tire_track_emission(
        car, kWheelRearRight, 1.0f, 0.0f);
    REQUIRE(left.emit && left.sliding);
    REQUIRE(right.emit && right.sliding);
    REQUIRE(left.surface == TireTrackSurface::Rubber);
    REQUIRE(left.intensity > 0.4f);
    REQUIRE_NEAR(glm::length(left.direction), 1.0, 1e-5);
    apricot_test::pass("a handbrake drift marks both rear tyres");
}

void snow_compresses_under_a_normally_rolling_tyre() {
    const VehicleState car = moving_car(5.0f);
    const TireTrackEmission event = tire_track_emission(
        car, kWheelFrontLeft, 0.0f, 0.65f);
    REQUIRE(event.emit);
    REQUIRE(!event.sliding);
    REQUIRE(event.surface == TireTrackSurface::Snow);
    REQUIRE(event.intensity > 0.45f);
    apricot_test::pass("snow compresses under a normally rolling tyre");
}

void airborne_and_stationary_tyres_never_mark() {
    VehicleState car = moving_car(0.4f);
    car.wheels[kWheelRearLeft].slip = 8.0f;
    car.wheels[kWheelRearLeft].contact_material = Surface::Grass;
    REQUIRE(!tire_track_emission(
        car, kWheelRearLeft, 1.0f, 1.0f).emit);

    car = moving_car(18.0f);
    car.wheels[kWheelRearLeft].grounded = false;
    car.wheels[kWheelRearLeft].slip = 8.0f;
    car.wheels[kWheelRearLeft].contact_material = Surface::Grass;
    REQUIRE(!tire_track_emission(
        car, kWheelRearLeft, 1.0f, 1.0f).emit);
    apricot_test::pass("airborne and stationary tyres never mark");
}

void dry_loose_ground_uses_a_dirt_track() {
    VehicleState car = moving_car(9.0f);
    car.velocity.x = 2.0f;
    car.wheels[kWheelFrontLeft].slip = 1.1f;
    car.wheels[kWheelFrontLeft].contact_material = Surface::Grass;
    const TireTrackEmission event = tire_track_emission(
        car, kWheelFrontLeft, 0.0f, 0.0f);
    REQUIRE(event.emit && event.sliding);
    REQUIRE(event.surface == TireTrackSurface::Dirt);
    apricot_test::pass("loose ground uses a dirt track");
}

void paved_overlays_and_loose_terrain_choose_different_tracks() {
    REQUIRE(tire_track_surface(Surface::Rock, 0.0f) ==
            TireTrackSurface::Rubber);
    REQUIRE(tire_track_surface(Surface::Grass, 0.0f) ==
            TireTrackSurface::Dirt);
    REQUIRE(tire_track_surface(Surface::Gravel, 0.0f) ==
            TireTrackSurface::Dirt);
    REQUIRE(tire_track_surface(Surface::Sand, 0.0f) ==
            TireTrackSurface::Dirt);
    apricot_test::pass(
        "paved roads and overlays use rubber while loose terrain uses dirt");
}

}  // namespace

int main() {
    std::puts("tire track field tests");
    ordinary_road_driving_leaves_no_rubber_marks();
    a_handbrake_drift_marks_both_rear_tyres();
    snow_compresses_under_a_normally_rolling_tyre();
    airborne_and_stationary_tyres_never_mark();
    dry_loose_ground_uses_a_dirt_track();
    paved_overlays_and_loose_terrain_choose_different_tracks();
    return 0;
}
