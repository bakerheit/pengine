#pragma once

#include "city/start_area.h"

namespace apricot::city {

// Parked actor definition, not road traffic or a selectable car. Coordinates
// are metres in the aircraft's own frame (+Z nose, +Y up). These locators do
// not enable boarding or flight; those need the later vehicle interaction work.
struct AircraftPoint { float x, y, z; };
struct AircraftCollisionBox { AircraftPoint centre, half; };
inline constexpr const char* kAirportAircraftId = "pinatty-gate-1-aster-a80";
inline constexpr const char* kAirportAircraftName = "Aster A-80";
inline constexpr Vec2 kAirportAircraftStand{-126.0f, 4.0f};
inline constexpr float kAirportAircraftYaw = 3.14159265358979323846f;
inline constexpr AircraftPoint kAirportAircraftEntry{-1.65f, 2.30f, 10.3f};
inline constexpr AircraftPoint kAirportAircraftPilot{-0.55f, 3.55f, 12.0f};
inline constexpr const char* kAirportAircraftBody = "models/vehicles/aster_a80/body.emesh";
inline constexpr const char* kAirportAircraftGear = "models/vehicles/aster_a80/gear.emesh";
inline constexpr const char* kAirportAircraftTexture = "textures/vehicles/aster_a80/body.png";

// Tight compound collision: never block the entire 28 x 32 m wing footprint
// down to ground level. Cars/people may pass through the open space beneath.
inline constexpr AircraftCollisionBox kAirportAircraftCollision[] = {
    {{0,3.7f,0},{1.50f,1.50f,8.0f}},
    {{0,3.7f,9.5f},{1.40f,1.35f,1.5f}},
    {{0,3.45f,12.0f},{1.10f,1.10f,1.0f}},
    {{0,3.15f,14.3f},{.55f,.55f,1.3f}},
    {{0,3.9f,-10.5f},{.85f,.90f,2.5f}},
    {{0,4.3f,-14.0f},{.35f,.40f,1.5f}},
    {{-3.0f,2.83f,0},{1.8f,.18f,3.8f}},
    {{3.0f,2.83f,0},{1.8f,.18f,3.8f}},
    {{-7.4f,3.03f,-1.8f},{2.6f,.22f,2.2f}},
    {{7.4f,3.03f,-1.8f},{2.6f,.22f,2.2f}},
    {{-12.0f,3.35f,-4.5f},{2.0f,.25f,.9f}},
    {{12.0f,3.35f,-4.5f},{2.0f,.25f,.9f}},
    {{-4.4f,1.92f,1.5f},{.85f,.85f,2.0f}},
    {{4.4f,1.92f,1.5f},{.85f,.85f,2.0f}},
    {{0,1.40f,11.1f},{.37f,1.40f,.42f}},
    {{-2.3f,1.35f,-1.5f},{.55f,1.35f,.58f}},
    {{2.3f,1.35f,-1.5f},{.55f,1.35f,.58f}},
    {{0,6.70f,-12.7f},{.12f,2.50f,1.25f}},
    {{-3.6f,5.0f,-12.8f},{2.6f,.28f,1.0f}},
    {{3.6f,5.0f,-12.8f},{2.6f,.28f,1.0f}},
};
// Plan-view clearance envelope only, NOT a ground-to-roof collision box.
inline constexpr StartPart kAirportAircraftFootprint{
    "Aster A-80 clearance", kAirportAircraftStand, 0, 28, 9.2f, 32,
    StartFinish::White, false};

} // namespace apricot::city
