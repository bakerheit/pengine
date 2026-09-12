#pragma once

#include "city/airport_aircraft.h"
#include "city/north_airbase.h"

namespace apricot::city {

// The gunship parked on Halberd Field's east apron. Unlike the Aster A-80 at
// Camber Point, this one is boardable and flyable the moment you reach it --
// see game/helicopter.h for the flight model and the control mapping.
//
// AircraftPoint and AircraftCollisionBox are reused from the airliner's header
// rather than redeclared. They are two plain float triples with no airliner in
// them, and a second copy of the same struct is how two locator tables quietly
// drift into different conventions.
//
// WHERE IT STANDS, and why not somewhere more obvious:
//
//   * NOT in a revetment, though that is where a real one would live and the
//     silhouette is right there. A revetment is 31.6 m of clear width between
//     blast walls with 5 m sides, and this rotor is 14 m across -- it fits,
//     but the first thing a new pilot does is ease forward, and forward is a
//     wall. A player's first flight should not end two seconds in.
//   * NOT on the runway or taxiway: neither is a parking space, and the
//     runway carries ground collision precisely so the player can use it.
//   * ON the east apron at local (330, 16), which is open concrete BETWEEN the
//     first two revetment hardstandings (they occupy x 263..297 and 363..397)
//     and clear of the three lead-in lines at x 280, 380 and 480. The rotor
//     disc has its full 7 m radius of flat apron on every side, and the walk
//     in from the gate road passes the hangars and the tower on the way.
//
// It faces the taxiway, which is north (-Z): lift off and the runway, the
// shore and the sea are laid out ahead of you rather than behind.
inline constexpr const char* kHalberdHelicopterId = "halberd-east-apron-gunship";
inline constexpr const char* kHalberdHelicopterName = "Halberd Gunship";
inline constexpr Vec2 kHalberdHelicopterStand{330.0f, 16.0f};
inline constexpr float kHalberdHelicopterYaw = 3.14159265358979323846f;

// Metres in the helicopter's own frame: +Z nose, +Y up, origin at the bottom
// of the gear. Measured off assets/models/vehicles/psx_helicopter/body.emesh
// by tools/cook_psx_helicopter.py, not guessed.
inline constexpr AircraftPoint kHalberdHelicopterEntry{-2.3f, 0.95f, 4.0f};
inline constexpr AircraftPoint kHalberdHelicopterPilot{0.0f, 2.45f, 3.6f};

inline constexpr const char* kHalberdHelicopterBody =
    "models/vehicles/psx_helicopter/body.emesh";
inline constexpr const char* kHalberdHelicopterRotor =
    "models/vehicles/psx_helicopter/rotor.emesh";
inline constexpr const char* kHalberdHelicopterTexture =
    "textures/vehicles/psx_helicopter/body.png";
inline constexpr const char* kHalberdHelicopterRotorTexture =
    "textures/vehicles/psx_helicopter/rotor.png";

// The rotor mesh is cooked recentred on its own hub so the host can spin it
// about its node's Y axis; this is where that hub sits in body space. The cook
// prints these three numbers and halberd_helicopter_tests checks the mesh and
// this constant still agree. The disc is a single alpha-cut quad, so it needs
// an alpha-blended material -- an opaque one is a 14 m black square.
inline constexpr AircraftPoint kHalberdHelicopterRotorHub{0.025f, 4.573f, 2.16f};
inline constexpr float kHalberdHelicopterRotorSpan = 14.0f;

// Tight compound collision. The 14 m rotor footprint is deliberately NOT in
// here: the disc sits at 4.57 m, well over anything that walks or drives under
// it, and a box the size of the disc would fence off 150 m2 of apron with
// invisible air. Flight clearance for the disc is the rim probe set in
// step_helicopter(), which is a different problem solved in a different place.
inline constexpr AircraftCollisionBox kHalberdHelicopterCollision[] = {
    {{0, 1.81f, 6.23f}, {.75f, 1.08f, 1.41f}},      // nose and chin turret
    {{0, 2.10f, 3.48f}, {1.23f, 1.80f, .79f}},      // cockpit
    {{0, 2.84f, 1.21f}, {1.43f, 2.11f, 1.10f}},     // fuselage and engine deck
    {{-2.50f, 1.73f, 1.55f}, {.51f, .63f, .86f}},   // port stub wing
    {{2.50f, 1.73f, 1.55f}, {.51f, .63f, .86f}},    // starboard stub wing
    {{0, 4.58f, 1.68f}, {.55f, .37f, 1.14f}},       // rotor mast head
    {{0, 2.60f, -1.70f}, {1.20f, 1.00f, 1.00f}},    // rear fuselage
    {{0, 1.80f, -4.60f}, {.40f, .70f, 2.10f}},      // tail boom
    {{0, 2.40f, -7.10f}, {1.75f, 1.75f, .70f}},     // tailplane and fin
    {{0, .36f, -.38f}, {1.23f, .37f, 6.70f}},       // skids
};

// Plan-view clearance envelope only, NOT a ground-to-roof collision box. Sized
// to the rotor disc rather than the airframe, because the disc is what the
// site has to keep clear.
inline constexpr StartPart kHalberdHelicopterFootprint{
    "Halberd Gunship clearance", kHalberdHelicopterStand, 0,
    kHalberdHelicopterRotorSpan, 5.2f, kHalberdHelicopterRotorSpan,
    StartFinish::White, false};

// World position of the stand. Halberd's site basis is unrotated, so local and
// world axes agree, but go through the site rather than assuming that here.
inline constexpr float kHalberdHelicopterWorldX =
    kHalberdFieldSite.origin.x +
    kHalberdFieldSite.cos_yaw * kHalberdHelicopterStand.x +
    kHalberdFieldSite.sin_yaw * kHalberdHelicopterStand.z;
inline constexpr float kHalberdHelicopterWorldZ =
    kHalberdFieldSite.origin.z -
    kHalberdFieldSite.sin_yaw * kHalberdHelicopterStand.x +
    kHalberdFieldSite.cos_yaw * kHalberdHelicopterStand.z;
// The east apron is a Ground-layer plane, so the gear stands on the plate plus
// that layer's top. Naming the layer keeps this from drifting if paving moves.
inline constexpr float kHalberdHelicopterWorldY =
    kHalberdFieldSite.ground_m + halberd_layer_top(HalberdLayer::Ground);

} // namespace apricot::city
