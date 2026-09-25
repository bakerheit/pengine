#pragma once
// A car bomb from Rook's Auto Repair: fitted in a bay, set off from anywhere
// that is not Rook's lot.
//
// The rules are here, stepped inside the fixed step from state the step owns,
// so a headless suite can hold them. The host layer owns the key, the blast,
// the fire, the sound and the wreck: it hands this step one trigger request
// and acts on the event that comes back.
//
// ONE KEY, AND WHAT IT MEANS IS DECIDED HERE, not at the key handler. Stopped
// in a bay in a car that is not the rigged one, K fits a bomb to it. Anywhere
// else, K sets off the bomb you have. Splitting that decision between the host
// and the sim is how a press in the bay ends up doing both.
//
// ONE BOMB AT A TIME. Fitting a second car moves the bomb: the first car is
// quietly made safe. Two remote bombs on one key would need a rule for which
// one goes, and "both" is not a rule a player can predict.
//
// NEVER ON ROOK'S LOT. The trigger is refused while the rigged car stands
// anywhere on the lot, bay or forecourt. That is the whole of the arming rule,
// and it earns its place twice: the tap that fitted the bomb cannot also set it
// off a step later, and Rook's garage is not a thing the player can destroy.
// The cost is that a player who drives the rigged car back into Rook's and
// wants it gone there cannot; they drive it out first.
//
// "Rigged car still exists" is the host's to say: a developer car swap or a
// reset can remove it from the world, and the bomb goes with it.
#include <algorithm>
#include <cmath>
#include <cstdint>

#include "city/body_damage.h"
#include "game/police_combat.h"
#include "physics/vehicle.h"

namespace apricot {

struct CarBomb {
    uint64_t vehicle = 0;  // the host's identity for the rigged car
    bool rigged = false;
};

struct CarBombInput {
    bool trigger = false;           // K, on the one step the press enters
    uint64_t driven_vehicle = 0;    // identity of the car being driven; 0 on foot
    bool bay_ready = false;         // the respray visit arrived and the car is stopped in a bay
    bool rigged_exists = true;      // the rigged car is still in the world
    bool rigged_on_lot = false;     // the rigged car stands on Rook's lot
};

// Fitted:      a bomb went on `vehicle`. `moved_from` is the car it came off,
//              or 0 when there was no bomb before.
// OnLot:       the trigger was pressed while the rigged car is on Rook's lot.
// Detonated:   `vehicle` blows up now. The bomb is spent.
// Lost:        the rigged car left the world; the bomb went with it.
// NoBomb:      the trigger was pressed with nothing to fit and nothing rigged.
enum class CarBombEvent : uint8_t { None, Fitted, OnLot, Detonated, Lost, NoBomb };

struct CarBombResult {
    CarBombEvent event = CarBombEvent::None;
    uint64_t vehicle = 0;
    uint64_t moved_from = 0;
};

inline CarBombResult step_car_bomb(CarBomb& bomb, const CarBombInput& in) {
    CarBombResult result;
    if (bomb.rigged && !in.rigged_exists) {
        result.event = CarBombEvent::Lost;
        result.vehicle = bomb.vehicle;
        bomb = CarBomb{};
        // A press on the same step still means something: fall through so a
        // car in the bay can take a fresh bomb.
        if (!in.trigger) return result;
    }
    if (!in.trigger) return result;
    const bool can_fit = in.driven_vehicle != 0 && in.bay_ready &&
                         !(bomb.rigged && bomb.vehicle == in.driven_vehicle);
    if (can_fit) {
        result.event = CarBombEvent::Fitted;
        result.vehicle = in.driven_vehicle;
        result.moved_from = bomb.rigged ? bomb.vehicle : 0u;
        bomb.vehicle = in.driven_vehicle;
        bomb.rigged = true;
        return result;
    }
    if (!bomb.rigged) {
        result.event = CarBombEvent::NoBomb;
        return result;
    }
    result.vehicle = bomb.vehicle;
    if (in.rigged_on_lot) {
        result.event = CarBombEvent::OnLot;
        return result;
    }
    result.event = CarBombEvent::Detonated;
    bomb = CarBomb{};
    return result;
}

// THE BLAST. Inside the lethal radius it kills any body in this game outright,
// whoever's health pool it is; past it the damage falls away linearly to
// nothing at the reach. Ten metres of reach is about a two-lane street and its
// pavements, which is the size of explosion that reads as "a car went up"
// rather than as a grenade or an air strike.
inline constexpr float kCarBombLethalRadiusM = 3.5f;
inline constexpr float kCarBombReachM = 10.0f;
inline constexpr float kCarBombLethalDamage = kBodyHealth * 1.5f;
// Past the lethal radius the first metre already drops below a kill: someone
// who steps back from the car survives it hurt.
inline constexpr float kCarBombEdgeDamage = kBodyHealth * 0.85f;

inline float car_bomb_damage(float distance_m) {
    if (!std::isfinite(distance_m) || distance_m < 0.0f) return 0.0f;
    if (distance_m <= kCarBombLethalRadiusM) return kCarBombLethalDamage;
    if (distance_m >= kCarBombReachM) return 0.0f;
    const float t = (distance_m - kCarBombLethalRadiusM) /
                    (kCarBombReachM - kCarBombLethalRadiusM);
    return kCarBombEdgeDamage * (1.0f - t);
}

// WHAT IS LEFT OF THE CAR. Written into the authoritative VehicleState rather
// than kept as a flag beside it, so every system that already reads the state
// agrees without being told: the shell draws crumpled from its damage zones,
// the lamps read dead from the same zones, and the engine will not start
// because the mechanical state says it has failed and has nothing to burn.
inline void wreck_car_bomb_vehicle(VehicleState& car) {
    car.health = 0.0f;
    for (float& zone : car.body_damage.zones) zone = std::max(zone, 0.92f);
    // Two broad dents from underneath the middle of the car, where the bomb
    // was, rather than from any one side.
    car.body_damage.stamps[0] = {{0.0f, -0.25f}, 1.0f, 0.0f, 1.0f, 0.15f, 0.0f};
    car.body_damage.stamps[1] = {{0.0f, 0.3f}, 0.9f, 3.14159265f, 1.0f, 0.2f, 0.0f};
    car.mechanical.engine_failed = true;
    car.mechanical.fuel_remaining = 0.0f;
    car.mechanical.oil_remaining = 0.0f;
}

// The hop. A car that goes up and comes down reads as a car bomb; one that
// only catches fire reads as a molotov that landed on a car. The spin is keyed
// to the car's own identity so a replayed detonation tumbles the same way.
//
// THE CAR IS LIFTED CLEAR OF ITS WHEELS, not only given speed. While a tyre
// still touches, the suspension damper holds the body down at about nine g, so
// a pure velocity kick of 6.5 m/s bought five centimetres (car_bomb_tests
// pins the height). Lifting the body past the suspension's travel in the
// blast's own step puts the wheels in the air and leaves the rest to gravity.
inline constexpr float kCarBombLiftMps = 5.0f;
inline void launch_car_bomb_vehicle(VehicleState& car, const VehicleTuning& tuning,
                                    uint64_t identity) {
    const float roll = (identity & 1u) ? 1.0f : -1.0f;
    const float pitch = (identity & 2u) ? 0.5f : -0.5f;
    car.position.y += std::max(tuning.suspension_travel, 0.0f) + 0.05f;
    car.velocity.y = std::max(car.velocity.y, 0.0f) + kCarBombLiftMps;
    car.angular_velocity += vehicle_forward(car) * (1.2f * roll) +
                            vehicle_right(car) * pitch;
}

// A bomb is a fire the city notices whether or not anybody watched it, for the
// reason kArsonHeat gives, and a little more than a bottle: the street does not
// just burn, a car comes apart. Bodies in the blast are charged separately, at
// the same prices a shot or a punch pays, so killing somebody with it costs
// what killing somebody costs.
inline constexpr float kCarBombHeat = kArsonHeat + 0.5f;

// The bomb line of the in-car prompt, and the reminder on foot.
enum class CarBombHint : uint8_t { None, CanFit, FittedOnLot, Armed };

struct CarBombHintInput {
    bool driving = false;           // in a road car, alive, not mid-transition
    bool bay_ready = false;         // arrived and stopped in a bay
    bool in_rigged_car = false;     // the car being driven is the rigged one
    bool rigged = false;
    bool rigged_on_lot = false;
};

// The reminder shows wherever the player is, because the detonator is in their
// pocket: a key that does something from anywhere needs a line that says so.
constexpr CarBombHint car_bomb_hint(const CarBombHintInput& in) {
    if (in.driving && in.bay_ready && !in.in_rigged_car) return CarBombHint::CanFit;
    if (!in.rigged) return CarBombHint::None;
    return in.rigged_on_lot ? CarBombHint::FittedOnLot : CarBombHint::Armed;
}

constexpr const char* car_bomb_hint_text(CarBombHint h) {
    switch (h) {
    case CarBombHint::None: return "";
    case CarBombHint::CanFit: return "K - FIT A CAR BOMB";
    case CarBombHint::FittedOnLot: return "CAR BOMB FITTED - DRIVE OFF THE LOT TO ARM IT";
    case CarBombHint::Armed: return "CAR BOMB ARMED - K TO DETONATE";
    }
    return "";
}

}  // namespace apricot
