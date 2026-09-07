#include <cstdio>
#include <cmath>
#include "game/vehicle_interaction.h"
#include "game/character.h"
#include "physics/terrain_collider.h"
#include "app/driving_mechanics.h"
#include "test_assert.h"
using namespace apricot;
int main() {
    const glm::quat q{1,0,0,0};
    const auto distance=[&](glm::vec3 p,float speed=0.f) {
        return vehicle_entry_distance(p,{0,.6f,0},q,1,2.5f,0,speed);
    };
    REQUIRE(distance({-1.8f,0,-.55f})<kVehicleEntryReach);
    REQUIRE(distance({1.8f,0,-.55f})<kVehicleEntryReach);
    REQUIRE(distance({0,0,-4})>kVehicleEntryReach);
    REQUIRE(distance({0,0,4})>kVehicleEntryReach);
    REQUIRE(!std::isfinite(distance({-1.8f,4,-.55f})));
    REQUIRE(!std::isfinite(distance({-1.8f,1.1f,-.55f})));
    REQUIRE(!std::isfinite(distance({0,0,-.55f})));
    REQUIRE(!std::isfinite(distance({-1.8f,-2,-.55f})));
    REQUIRE(!std::isfinite(distance({-1.8f,0,-.55f},8)));
    REQUIRE(!std::isfinite(vehicle_entry_distance({1.8f,0,0},{0,0,0},
        glm::angleAxis(3.14159265f,glm::vec3{0,0,1}),1,2.5f,0,0)));
    const auto rotated=glm::angleAxis(1.1f,glm::vec3{0,1,0});
    REQUIRE_NEAR(vehicle_entry_distance(rotated*glm::vec3{-1.8f,0,-.55f},
        {0,.6f,0},rotated,1,2.5f,0,0),distance({-1.8f,0,-.55f}),1e-5f);
    TerrainCollider collider{42};
    const auto id=collider.add_kinematic_oriented_box({0,200,0},{1,1,2.5f},0);
    REQUIRE(collider.raycast({-4,200,0},{1,0,0},8).hit);
    REQUIRE(!character_position_clear(collider,{0,199.5f,0},CharacterTuning{}));
    REQUIRE(collider.set_kinematic_enabled(id,false));
    REQUIRE(!collider.raycast({-4,200,0},{1,0,0},8).hit);
    REQUIRE(character_position_clear(collider,{0,199.5f,0},CharacterTuning{}));
    REQUIRE(collider.set_kinematic_enabled(id,true));
    REQUIRE(collider.raycast({-4,200,0},{1,0,0},8).hit);
    REQUIRE(!collider.set_kinematic_enabled(9999,false));
    // Exercise the SAME unattended step used by the app, with the actual
    // arcade profile. Physical brakes must not become reverse throttle,
    // even when the driver left the gearbox in reverse before getting out.
    TerrainCollider parking_lot{42};
    parking_lot.add_static_ground_rect({0,0},200,{40,40},0,Surface::Rock);
    const auto parking_tuning=player_vehicle_tuning(DrivingMechanicsStyle::ClassicGta);
    for (const int gear : {1,kGearReverse}) {
        auto car=spawn_vehicle(parking_tuning,parking_lot,0,0,0);
        car.gear=gear;
        const auto origin=car.position;
        for (int step=0;step<1200;++step) {
            car=step_unoccupied_vehicle(car,parking_tuning,parking_lot,1.0f/120.0f);
            REQUIRE(car.gear==gear);
            REQUIRE(glm::length(glm::vec2{car.position.x-origin.x,car.position.z-origin.z})<.05f);
        }
        REQUIRE(glm::length(car.velocity)<.05f);
        REQUIRE(parking_tuning.arcade_reverse);
        // Returning to occupied controls still allows intentional reverse.
        InputFrame reverse{};
        reverse.brake=1.0f;
        car=step_vehicle(car,parking_tuning,reverse,parking_lot,1.0f/120.0f);
        REQUIRE(car.gear==kGearReverse);
    }
    apricot_test::pass("unattended physical brakes hold in drive and reverse without changing player controls");
    apricot_test::pass("door reach, speed, roof, rollover and collider ownership gates");
    return apricot_test::done("vehicle_interaction_tests");
}
