#pragma once

#include <cmath>
#include <cstddef>
#include <vector>

#include "city/start_area.h"

namespace apricot {
namespace city {

// Florangia Regional sits on the broad shoulder of the state.  Its local +X
// runway keeps the approaches over the wide part of the peninsula; landside
// traffic leaves to the south and meets the freeway at one authored point.
inline constexpr StartSite kFlorangiaAirportSite{
    "Florangia Regional Airport", {4800.0f, 4400.0f}, 1.0f, 0.0f,
    {0.0f, 40.0f}, 1200.0f, 600.0f, 6.5f, 2200.0f};

inline constexpr Vec2 kFlorangiaAirportAccess{4800.0f, 4700.0f};
inline constexpr float kFlorangiaAirportPavingTopM = 0.13f;
inline constexpr float kFlorangiaAirportPaintLiftM = 0.018f;

constexpr bool florangia_airport_lot_contains(float world_x, float world_z,
                                               float margin_m = 0.0f) {
    const float centre_x = kFlorangiaAirportSite.origin.x +
                           kFlorangiaAirportSite.lot_centre.x;
    const float centre_z = kFlorangiaAirportSite.origin.z +
                           kFlorangiaAirportSite.lot_centre.z;
    return world_x >= centre_x - kFlorangiaAirportSite.lot_width_m * 0.5f - margin_m &&
           world_x <= centre_x + kFlorangiaAirportSite.lot_width_m * 0.5f + margin_m &&
           world_z >= centre_z - kFlorangiaAirportSite.lot_depth_m * 0.5f - margin_m &&
           world_z <= centre_z + kFlorangiaAirportSite.lot_depth_m * 0.5f + margin_m;
}

// This first pass is deliberately smaller than Pinatty International, but it
// is a complete readable airport: runway, connected taxiway/apron, terminal,
// tower, hangar, parking, and a straight public road to the freeway junction.
inline constexpr StartPart kFlorangiaAirportCoreParts[] = {
    {"Florangia runway 08-26", {0.0f, -105.0f}, 0.03f,
     1000.0f, 0.10f, 48.0f, StartFinish::Asphalt, false},
    {"Florangia runway north shoulder", {0.0f, -133.0f}, 0.02f,
     1000.0f, 0.08f, 8.0f, StartFinish::Concrete, false},
    {"Florangia runway south shoulder", {0.0f, -77.0f}, 0.02f,
     1000.0f, 0.08f, 8.0f, StartFinish::Concrete, false},
    {"Florangia passenger apron", {-70.0f, 8.0f}, 0.03f,
     360.0f, 0.10f, 92.0f, StartFinish::Concrete, false},
    {"Florangia taxiway alpha", {-350.0f, -45.0f}, 0.03f,
     24.0f, 0.10f, 72.0f, StartFinish::Asphalt, false},
    {"Florangia taxiway bravo", {170.0f, -45.0f}, 0.03f,
     24.0f, 0.10f, 72.0f, StartFinish::Asphalt, false},

    {"Florangia terminal west", {-98.0f, 92.0f}, 0.0f,
     72.0f, 9.0f, 38.0f, StartFinish::Glass, true},
    {"Florangia terminal hall", {-22.0f, 92.0f}, 0.0f,
     80.0f, 14.0f, 42.0f, StartFinish::Glass, true},
    {"Florangia terminal east", {56.0f, 92.0f}, 0.0f,
     72.0f, 10.5f, 38.0f, StartFinish::Glass, true},
    {"Florangia terminal west roof", {-98.0f, 92.0f}, 9.0f,
     76.0f, 0.60f, 42.0f, StartFinish::White, false},
    {"Florangia terminal hall roof", {-22.0f, 92.0f}, 14.0f,
     84.0f, 0.75f, 46.0f, StartFinish::White, false},
    {"Florangia terminal east roof", {56.0f, 92.0f}, 10.5f,
     76.0f, 0.60f, 42.0f, StartFinish::White, false},
    {"Florangia terminal entry canopy", {-22.0f, 116.0f}, 4.1f,
     150.0f, 0.45f, 8.0f, StartFinish::White, false},
    {"Florangia terminal west entrance", {-72.0f, 112.2f}, 0.10f,
     12.0f, 3.4f, 0.35f, StartFinish::TealDoor, false},
    {"Florangia terminal main entrance", {-22.0f, 113.2f}, 0.10f,
     14.0f, 3.6f, 0.35f, StartFinish::TealDoor, false},
    {"Florangia terminal east entrance", {30.0f, 112.2f}, 0.10f,
     12.0f, 3.4f, 0.35f, StartFinish::TealDoor, false},
    {"Florangia terminal pedestrian plaza", {-22.0f, 121.0f}, 0.12f,
     188.0f, 0.18f, 14.0f, StartFinish::Concrete, false},

    {"Florangia dropoff road", {-22.0f, 143.0f}, 0.03f,
     220.0f, 0.10f, 18.0f, StartFinish::Asphalt, false},
    {"Florangia public parking", {-22.0f, 181.0f}, 0.03f,
     250.0f, 0.10f, 44.0f, StartFinish::Asphalt, false},
    {"Florangia airport access road", {0.0f, 245.0f}, 0.03f,
     22.0f, 0.10f, 110.0f, StartFinish::Asphalt, false},

    {"Florangia control tower base", {145.0f, 58.0f}, 0.0f,
     18.0f, 7.0f, 18.0f, StartFinish::Concrete, true},
    {"Florangia control tower shaft", {145.0f, 58.0f}, 7.0f,
     9.0f, 15.0f, 9.0f, StartFinish::White, true},
    {"Florangia control tower cab", {145.0f, 58.0f}, 22.0f,
     17.0f, 5.0f, 17.0f, StartFinish::Glass, true},
    {"Florangia control tower roof", {145.0f, 58.0f}, 27.0f,
     19.0f, 1.0f, 19.0f, StartFinish::DarkRoof, false},
    {"Florangia control tower beacon", {145.0f, 58.0f}, 28.0f,
     0.8f, 1.2f, 0.8f, StartFinish::Yellow, false},

    {"Florangia maintenance apron", {340.0f, 52.0f}, 0.03f,
     150.0f, 0.10f, 108.0f, StartFinish::Concrete, false},
    {"Florangia maintenance hangar", {340.0f, 94.0f}, 0.0f,
     104.0f, 16.0f, 64.0f, StartFinish::Steel, true},
    {"Florangia maintenance hangar roof", {340.0f, 94.0f}, 16.0f,
     108.0f, 0.8f, 68.0f, StartFinish::White, false},
    {"Florangia maintenance hangar door", {340.0f, 61.7f}, 0.2f,
     74.0f, 12.5f, 0.8f, StartFinish::TealDoor, false},

    {"Florangia airport monument sign", {50.0f, 218.0f}, 0.0f,
     38.0f, 5.5f, 1.2f, StartFinish::TealDoor, true},
    {"Florangia airport sign cap", {50.0f, 218.0f}, 5.5f,
     40.0f, 0.6f, 1.5f, StartFinish::White, false},
};

inline constexpr std::size_t kFlorangiaAirportCorePartCount =
    sizeof(kFlorangiaAirportCoreParts) /
    sizeof(kFlorangiaAirportCoreParts[0]);

inline constexpr BuildingPlan kFlorangiaAirportPlan{
    "Florangia Regional Airport", nullptr, 0, nullptr, 0,
    kFlorangiaAirportCoreParts, kFlorangiaAirportCorePartCount, nullptr, 0};

inline std::vector<StartPart> bake_florangia_airport() {
    std::vector<StartPart> parts = bake_building(kFlorangiaAirportPlan);
    parts.reserve(parts.size() + 100u);

    for (int i = 0; i < 19; ++i) {
        parts.push_back({"Florangia runway centreline dash",
                         {-405.0f + static_cast<float>(i) * 45.0f, -105.0f},
                         kFlorangiaAirportPavingTopM, 22.0f,
                         kFlorangiaAirportPaintLiftM, 0.72f,
                         StartFinish::White, false});
    }
    for (int end = -1; end <= 1; end += 2) {
        for (int row = 0; row < 6; ++row) {
            parts.push_back({"Florangia runway threshold bar",
                             {static_cast<float>(end) * 455.0f,
                              -122.5f + static_cast<float>(row) * 7.0f},
                             kFlorangiaAirportPavingTopM, 30.0f,
                             kFlorangiaAirportPaintLiftM, 1.8f,
                             StartFinish::White, false});
        }
    }
    for (int row = 0; row < 2; ++row) {
        const float z = row == 0 ? 169.0f : 193.0f;
        for (int i = 0; i < 35; ++i) {
            const float x = -132.0f + static_cast<float>(i) * 7.0f;
            if (std::fabs(x) < 9.0f) continue;  // access-road throat
            parts.push_back({"Florangia parking stripe", {x, z},
                             kFlorangiaAirportPavingTopM, 0.12f,
                             kFlorangiaAirportPaintLiftM, 8.5f,
                             StartFinish::White, false});
        }
    }
    for (const float x : {-92.0f, -22.0f, 48.0f}) {
        parts.push_back({"Florangia terminal light pole", {x, 138.0f},
                         0.0f, 0.38f, 8.5f, 0.38f,
                         StartFinish::Steel, true});
        parts.push_back({"Florangia terminal light head", {x, 138.0f},
                         8.35f, 2.8f, 0.35f, 0.75f,
                         StartFinish::White, false});
    }
    return parts;
}

static_assert(valid_start_parts(kFlorangiaAirportCoreParts,
                                kFlorangiaAirportCorePartCount),
              "Florangia airport contains an invalid authored part");

}  // namespace city
}  // namespace apricot
