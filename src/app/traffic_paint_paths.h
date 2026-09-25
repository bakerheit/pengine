#pragma once
// The atlases each traffic kind wears, and the player car a stolen one becomes.
// No GL: the headless suites include it.
//
// The Sedan and BoxTruck lists are also a SAVED ID. A stolen Car 5 or Car 8
// keeps the livery it was driving in, and APRICOT_SAVE 4 stores that livery as
// an index into these lists (paint_base, app/vehicle_paint_catalog.h). Traffic
// picks from them by hash modulo their length, so appending is the natural
// edit, and appending is safe. Reordering is not: every saved car in that model
// would load in another car's paint. vehicle_paint_profiles_tests pins all
// eight entries so a reorder fails ctest instead of a save.
#include <cstddef>
#include <iterator>

#include "app/player_car_catalog.h"
#include "traffic/ambient.h"

namespace apricot {

// Saved as paint_base by APRICOT_SAVE 4; append only, never reorder.
inline constexpr const char* kCar5Paints[] = {
    "textures/vehicles/car5/body.png",
    "textures/vehicles/car5/green.png",
    "textures/vehicles/car5/grey.png",
    "textures/vehicles/car5/taxi.png",
};
// Saved as paint_base by APRICOT_SAVE 4; append only, never reorder.
inline constexpr const char* kCar8Paints[] = {
    "textures/vehicles/car8/body.png",
    "textures/vehicles/car8/grey.png",
    "textures/vehicles/car8/purple.png",
    "textures/vehicles/car8/mail.png",
};
inline constexpr const char* kAmbulancePaints[] = {
    "textures/vehicles/ambulance/body.png",
};
inline constexpr const char* kFiretruckPaints[] = {
    "textures/vehicles/firetruck/body_surface.png",
};
inline constexpr const char* kHalcyonSixPaints[] = {
    "textures/vehicles/halcyon_six/body_surface.png",
};
inline constexpr const char* kMontroseRegentEightPaints[] = {
    "textures/vehicles/montrose_regent_eight/body_surface.png",
};
inline constexpr const char* kVesperVx91Paints[] = {
    "textures/vehicles/vesper_vx91/body_surface.png",
};
inline constexpr const char* kPolicePaints[] = {
    "textures/vehicles/municipal_cruiser_91c/body.png",
};
inline constexpr const char* kBwc360Paints[] = {
    "textures/vehicles/bwc_360/body.png",
};

struct TrafficPaintPaths {
    const char* const* paths;
    std::size_t count;
};

// The snowplow is procedural and draws flat white: it has no atlas.
inline constexpr TrafficPaintPaths traffic_paint_paths(TrafficVehicleKind kind) {
    switch (kind) {
    case TrafficVehicleKind::Sedan: return {kCar5Paints, std::size(kCar5Paints)};
    case TrafficVehicleKind::BoxTruck: return {kCar8Paints, std::size(kCar8Paints)};
    case TrafficVehicleKind::Ambulance: return {kAmbulancePaints, std::size(kAmbulancePaints)};
    case TrafficVehicleKind::Firetruck: return {kFiretruckPaints, std::size(kFiretruckPaints)};
    case TrafficVehicleKind::HalcyonSix: return {kHalcyonSixPaints, std::size(kHalcyonSixPaints)};
    case TrafficVehicleKind::MontroseRegentEight:
        return {kMontroseRegentEightPaints, std::size(kMontroseRegentEightPaints)};
    case TrafficVehicleKind::VesperVx91: return {kVesperVx91Paints, std::size(kVesperVx91Paints)};
    case TrafficVehicleKind::Police: return {kPolicePaints, std::size(kPolicePaints)};
    case TrafficVehicleKind::Bwc360: return {kBwc360Paints, std::size(kBwc360Paints)};
    case TrafficVehicleKind::Snowplow: return {nullptr, 0};
    }
    return {nullptr, 0};
}

// The player car a taken traffic vehicle becomes.
inline constexpr PlayerCarId traffic_player_car(TrafficVehicleKind kind) {
    switch (kind) {
    case TrafficVehicleKind::Sedan: return PlayerCarId::LegacyCar5;
    case TrafficVehicleKind::BoxTruck:
    case TrafficVehicleKind::Snowplow: return PlayerCarId::LegacyCar8;
    case TrafficVehicleKind::Ambulance: return PlayerCarId::MunicipalAmbulance;
    case TrafficVehicleKind::Firetruck: return PlayerCarId::MunicipalFiretruck;
    case TrafficVehicleKind::HalcyonSix: return PlayerCarId::HalcyonSix;
    case TrafficVehicleKind::MontroseRegentEight: return PlayerCarId::MontroseRegentEight;
    case TrafficVehicleKind::VesperVx91: return PlayerCarId::VesperVx91;
    case TrafficVehicleKind::Police: return PlayerCarId::MunicipalCruiser91C;
    case TrafficVehicleKind::Bwc360: return PlayerCarId::Bwc360;
    }
    return PlayerCarId::LegacyCar5;
}

}  // namespace apricot
