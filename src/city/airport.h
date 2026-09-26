#pragma once

#include <cstddef>
#include <cmath>
#include <vector>

#include "city/airport_development.h"
#include "city/airport_aircraft.h"
#include "city/road_types.h"
#include "city/start_area.h"

namespace apricot {
namespace city {

// Camber Point is already the island's airfield plate. This document turns
// that gameplay promise into a real place: a long east-west runway, terminal,
// concourse, three gates, control tower, hangar, and the escape plane.
inline constexpr StartSite kAirportSite{
    "Pinatty International Airport", {150.0f, 2140.0f}, 1.0f, 0.0f,
    {0.0f, 55.0f}, 1000.0f, 470.0f, 6.0f, 1800.0f};

// Paint pieces use a top-only decal mesh. Their authored top still follows
// BuildingPiece's bottom + height rule: airfield paint sits just above the
// 13 cm paving slabs, while terminal zebras match the road baker's 9 cm
// crosswalk plane (6 cm road drape + 3 cm paint lift).
inline constexpr float kAirportPavingTopM = 0.13f;
inline constexpr float kAirportPaintLiftM = 0.018f;
inline constexpr float kAirportCrosswalkLiftM = 0.03f;

constexpr bool airport_lot_contains(float world_x, float world_z,
                                    float margin_m = 0.0f) {
    const float centre_x = kAirportSite.origin.x + kAirportSite.lot_centre.x;
    const float centre_z = kAirportSite.origin.z + kAirportSite.lot_centre.z;
    return world_x >= centre_x - kAirportSite.lot_width_m * 0.5f - margin_m &&
           world_x <= centre_x + kAirportSite.lot_width_m * 0.5f + margin_m &&
           world_z >= centre_z - kAirportSite.lot_depth_m * 0.5f - margin_m &&
           world_z <= centre_z + kAirportSite.lot_depth_m * 0.5f + margin_m;
}

inline constexpr StartPart kAirportCoreParts[] = {
    // Flight surfaces. Markings are added by bake_airport() so their cadence
    // stays arithmetic instead of becoming a wall of copied coordinates.
    {"runway 09-27", {0.0f, -94.0f}, 0.03f, 900.0f, 0.10f, 48.0f,
     StartFinish::Asphalt, false},
    {"runway north shoulder", {0.0f, -122.0f}, 0.02f, 900.0f, 0.08f, 8.0f,
     StartFinish::Concrete, false},
    {"runway south shoulder", {0.0f, -66.0f}, 0.02f, 900.0f, 0.08f, 8.0f,
     StartFinish::Concrete, false},
    {"airport apron", {-90.0f, 17.0f}, 0.03f, 500.0f, 0.10f, 92.0f,
     StartFinish::Concrete, false},
    // Alpha feeds the maintenance apron; Bravo feeds the passenger apron.
    // Both overlap the runway shoulder and their apron by several metres, so
    // there is no grass seam pretending to be a taxi connection.
    {"taxiway alpha", {315.0f, -40.0f}, 0.03f, 24.0f, 0.10f, 72.0f,
     StartFinish::Asphalt, false},
    {"taxiway bravo", {-300.0f, -40.0f}, 0.03f, 24.0f, 0.10f, 72.0f,
     StartFinish::Asphalt, false},
    // Separate parking pads stop at the road edge. A single raised slab used
    // to cover the internal road, including its two entrance bends.
    {"terminal parking", {-35.0f, 200.0f}, 0.03f, 330.0f, 0.10f, 22.0f,
     StartFinish::Asphalt, false},
    {"terminal parking south pad", {-35.0f, 232.0f}, 0.03f, 230.0f, 0.10f, 16.0f,
     StartFinish::Asphalt, false},
    {"hangar apron", {315.0f, 59.0f}, 0.03f, 150.0f, 0.10f, 128.0f,
     StartFinish::Concrete, false},

    // Terminal and airside concourse. The landside massing is deliberately
    // stepped: one 235 m glass box had no entrance scale and read like an
    // office wall from the pickup lane. These three joined volumes keep the
    // same usable footprint while giving the check-in hall a clear skyline.
    {"terminal facade west wing", {-111.5f, 128.0f}, 0.0f, 73.0f, 10.0f, 36.0f,
     StartFinish::Glass, true},
    {"terminal facade centre hall", {-35.0f, 128.0f}, 0.0f, 80.0f, 16.0f, 40.0f,
     StartFinish::Glass, true},
    {"terminal facade east wing", {44.0f, 128.0f}, 0.0f, 78.0f, 11.5f, 36.0f,
     StartFinish::Glass, true},
    {"terminal roof west wing", {-111.5f, 128.0f}, 10.0f, 77.0f, 0.65f, 40.0f,
     StartFinish::White, false},
    {"terminal roof centre hall", {-35.0f, 128.0f}, 16.0f, 84.0f, 0.75f, 44.0f,
     StartFinish::White, false},
    {"terminal roof east wing", {44.0f, 128.0f}, 11.5f, 82.0f, 0.65f, 40.0f,
     StartFinish::White, false},
    {"terminal roof plant", {-35.0f, 128.0f}, 16.75f, 58.0f, 3.0f, 16.0f,
     StartFinish::Steel, false},

    // A continuous pedestrian plaza meets the road's own north sidewalk at
    // the kerb. Three short canopies mark actual doors; the old uninterrupted
    // 190 m blade gave drivers no clue where to stop.
    {"terminal pedestrian plaza", {-35.0f, 150.0f}, 0.12f, 235.0f, 0.18f, 8.0f,
     StartFinish::Concrete, false},
    {"terminal arrivals canopy west", {-110.0f, 150.0f}, 4.25f, 54.0f, 0.45f, 8.0f,
     StartFinish::White, false},
    {"terminal arrivals canopy centre", {-35.0f, 150.0f}, 4.25f, 56.0f, 0.45f, 8.0f,
     StartFinish::White, false},
    {"terminal arrivals canopy east", {40.0f, 150.0f}, 4.25f, 54.0f, 0.45f, 8.0f,
     StartFinish::White, false},
    {"terminal canopy fascia west", {-110.0f, 153.75f}, 3.82f, 54.0f, 0.72f, 0.50f,
     StartFinish::TealDoor, false},
    {"terminal canopy fascia centre", {-35.0f, 153.75f}, 3.82f, 56.0f, 0.72f, 0.50f,
     StartFinish::TealDoor, false},
    {"terminal canopy fascia east", {40.0f, 153.75f}, 3.82f, 54.0f, 0.72f, 0.50f,
     StartFinish::TealDoor, false},
    {"terminal entrance west", {-110.0f, 146.18f}, 0.08f, 12.0f, 3.35f, 0.36f,
     StartFinish::TealDoor, false},
    {"terminal entrance centre", {-35.0f, 148.18f}, 0.08f, 14.0f, 3.55f, 0.36f,
     StartFinish::TealDoor, false},
    {"terminal entrance east", {40.0f, 146.18f}, 0.08f, 12.0f, 3.35f, 0.36f,
     StartFinish::TealDoor, false},
    {"terminal wayfinding band west", {-110.0f, 146.22f}, 6.2f, 52.0f, 1.15f, 0.30f,
     StartFinish::TealDoor, false},
    {"terminal wayfinding band centre", {-35.0f, 148.22f}, 9.2f, 68.0f, 1.35f, 0.30f,
     StartFinish::TealDoor, false},
    {"terminal wayfinding band east", {40.0f, 146.22f}, 6.9f, 52.0f, 1.15f, 0.30f,
     StartFinish::TealDoor, false},
    {"terminal departures accent", {-35.0f, 148.39f}, 10.65f, 42.0f, 0.28f, 0.18f,
     StartFinish::Yellow, false},
    {"terminal crown band", {-35.0f, 148.42f}, 12.15f, 56.0f, 1.65f, 0.26f,
     StartFinish::TealDoor, false},
    {"terminal crown accent", {-35.0f, 148.57f}, 13.72f, 34.0f, 0.20f, 0.12f,
     StartFinish::Yellow, false},
    {"terminal facade pier west", {-75.0f, 146.28f}, 0.0f, 1.8f, 10.2f, 0.56f,
     StartFinish::White, false},
    {"terminal facade pier east", {5.0f, 146.28f}, 0.0f, 1.8f, 11.7f, 0.56f,
     StartFinish::White, false},
    {"terminal entrance mullion west", {-110.0f, 146.39f}, 0.10f, 0.24f, 3.30f, 0.16f,
     StartFinish::Steel, false},
    {"terminal entrance mullion centre", {-35.0f, 148.39f}, 0.10f, 0.24f, 3.50f, 0.16f,
     StartFinish::Steel, false},
    {"terminal entrance mullion east", {40.0f, 146.39f}, 0.10f, 0.24f, 3.30f, 0.16f,
     StartFinish::Steel, false},
    {"concourse west", {-125.0f, 78.0f}, 0.0f, 44.0f, 8.0f, 74.0f,
     StartFinish::Glass, true},
    {"concourse centre", {-35.0f, 78.0f}, 0.0f, 44.0f, 8.0f, 74.0f,
     StartFinish::Glass, true},
    {"concourse east", {55.0f, 78.0f}, 0.0f, 44.0f, 8.0f, 74.0f,
     StartFinish::Glass, true},
    {"concourse west roof", {-125.0f, 78.0f}, 8.0f, 48.0f, 0.45f, 78.0f,
     StartFinish::White, false},
    {"concourse centre roof", {-35.0f, 78.0f}, 8.0f, 48.0f, 0.45f, 78.0f,
     StartFinish::White, false},
    {"concourse east roof", {55.0f, 78.0f}, 8.0f, 48.0f, 0.45f, 78.0f,
     StartFinish::White, false},
    {"jet bridge gate 1", {-125.0f, 34.0f}, 3.8f, 5.0f, 3.8f, 18.0f,
     StartFinish::White, true},
    {"jet bridge gate 2", {-35.0f, 34.0f}, 3.8f, 5.0f, 3.8f, 18.0f,
     StartFinish::White, true},
    {"jet bridge gate 3", {55.0f, 34.0f}, 3.8f, 5.0f, 3.8f, 18.0f,
     StartFinish::White, true},

    // The tower occupies the clear strip between passenger and maintenance
    // aprons. Its first location at (-40, 30) physically overlapped Gate 2's
    // jet bridge and put a solid building on the passenger stand.
    {"control tower base", {190.0f, 42.0f}, 0.0f, 18.0f, 7.0f, 18.0f,
     StartFinish::Concrete, true},
    {"control tower shaft", {190.0f, 42.0f}, 7.0f, 9.0f, 16.0f, 9.0f,
     StartFinish::White, true},
    {"control tower cab", {190.0f, 42.0f}, 23.0f, 17.0f, 5.0f, 17.0f,
     StartFinish::Glass, true},
    {"control tower roof", {190.0f, 42.0f}, 28.0f, 19.0f, 1.0f, 19.0f,
     StartFinish::DarkRoof, false},
    {"control tower beacon", {190.0f, 42.0f}, 29.0f, 0.8f, 1.2f, 0.8f,
     StartFinish::Yellow, false},

    // Maintenance side of the field.
    {"airport hangar", {315.0f, 112.0f}, 0.0f, 108.0f, 17.0f, 68.0f,
     StartFinish::Steel, true},
    {"airport hangar roof", {315.0f, 112.0f}, 17.0f, 112.0f, 0.8f, 72.0f,
     StartFinish::White, false},
    {"airport hangar door", {315.0f, 77.7f}, 0.2f, 76.0f, 13.0f, 0.8f,
     StartFinish::TealDoor, false},
    {"airport fire station", {210.0f, 130.0f}, 0.0f, 58.0f, 9.0f, 34.0f,
     StartFinish::Brick, true},
    {"airport fire station roof", {210.0f, 130.0f}, 9.0f, 62.0f, 0.65f, 38.0f,
     StartFinish::White, false},
    {"airport fire response apron", {210.0f, 105.0f}, 0.03f,
     60.0f, 0.10f, 16.0f, StartFinish::Concrete, false},
    {"airport fire bay west", {192.0f, 112.82f}, 0.15f,
     14.0f, 5.8f, 0.36f, StartFinish::TealDoor, false},
    {"airport fire bay centre", {210.0f, 112.82f}, 0.15f,
     14.0f, 5.8f, 0.36f, StartFinish::TealDoor, false},
    {"airport fire bay east", {228.0f, 112.82f}, 0.15f,
     14.0f, 5.8f, 0.36f, StartFinish::TealDoor, false},
    {"airport fire beacon", {210.0f, 130.0f}, 9.65f,
     0.8f, 1.0f, 0.8f, StartFinish::RedTrim, false},

    // The parked Aster A-80 is a cooked vehicle, loaded separately by World.
    // No duplicate building-box aircraft or invisible old collision remains.

    // Landside identity and practical barriers. The sign and splitter island
    // sit between the dropoff road and the parking field, not in a random
    // grass triangle outside the circulation path.
    {"airport monument sign", {-142.0f, 178.0f}, 0.0f, 42.0f, 6.0f, 1.2f,
     StartFinish::TealDoor, true},
    {"airport sign cap", {-142.0f, 178.0f}, 6.0f, 44.0f, 0.6f, 1.5f,
     StartFinish::White, false},
    {"terminal curb zone west", {-110.0f, 153.75f}, 0.18f,
     48.0f, 0.12f, 0.45f, StartFinish::TealDoor, false},
    {"terminal curb zone centre", {-35.0f, 153.75f}, 0.18f,
     50.0f, 0.12f, 0.45f, StartFinish::Yellow, false},
    {"terminal curb zone east", {40.0f, 153.75f}, 0.18f,
     48.0f, 0.12f, 0.45f, StartFinish::RedTrim, false},
    {"terminal splitter island west", {-92.0f, 178.0f}, 0.12f, 48.0f, 0.18f, 6.0f,
     StartFinish::Concrete, false},
    {"terminal splitter island centre", {-35.0f, 178.0f}, 0.12f, 48.0f, 0.18f, 6.0f,
     StartFinish::Concrete, false},
    {"terminal splitter island east", {22.0f, 178.0f}, 0.12f, 48.0f, 0.18f, 6.0f,
     StartFinish::Concrete, false},
    {"terminal splitter island taxi", {79.0f, 178.0f}, 0.12f, 48.0f, 0.18f, 6.0f,
     StartFinish::Concrete, false},
    {"terminal zone totem west", {-92.0f, 178.0f}, 0.30f,
     1.2f, 4.5f, 1.2f, StartFinish::TealDoor, true},
    {"terminal zone totem centre", {-48.0f, 178.0f}, 0.30f,
     1.2f, 4.5f, 1.2f, StartFinish::Yellow, true},
    {"terminal zone totem east", {22.0f, 178.0f}, 0.30f,
     1.2f, 4.5f, 1.2f, StartFinish::RedTrim, true},
    {"terminal taxi shelter roof", {79.0f, 178.0f}, 3.25f,
     42.0f, 0.45f, 5.0f, StartFinish::White, false},
    {"terminal taxi shelter fascia", {79.0f, 180.3f}, 2.85f,
     42.0f, 0.70f, 0.40f, StartFinish::TealDoor, false},
    {"terminal taxi shelter column west", {61.0f, 178.0f}, 0.30f,
     0.45f, 2.95f, 0.45f, StartFinish::Steel, true},
    {"terminal taxi shelter column east", {97.0f, 178.0f}, 0.30f,
     0.45f, 2.95f, 0.45f, StartFinish::Steel, true},
    {"terminal taxi shelter bench west", {68.0f, 178.0f}, 0.30f,
     10.0f, 0.55f, 1.2f, StartFinish::Steel, true},
    {"terminal taxi shelter bench east", {90.0f, 178.0f}, 0.30f,
     10.0f, 0.55f, 1.2f, StartFinish::Steel, true},
    {"terminal parking island 1", {-185.0f, 198.0f}, 0.12f, 4.5f, 0.18f, 10.0f,
     StartFinish::Concrete, true},
    {"terminal parking island 2", {-80.0f, 198.0f}, 0.12f, 4.5f, 0.18f, 10.0f,
     StartFinish::Concrete, true},
    {"terminal parking island 3", {-15.0f, 198.0f}, 0.12f, 4.5f, 0.18f, 10.0f,
     StartFinish::Concrete, true},
    {"terminal parking island 4", {70.0f, 198.0f}, 0.12f, 4.5f, 0.18f, 10.0f,
     StartFinish::Concrete, true},
    {"terminal parking island 5", {115.0f, 198.0f}, 0.12f, 4.5f, 0.18f, 10.0f,
     StartFinish::Concrete, true},

    {"terminal canopy column west outer", {-134.0f, 151.0f}, 0.0f, 0.55f, 4.25f, 0.55f,
     StartFinish::Steel, true},
    {"terminal canopy column west inner", {-86.0f, 151.0f}, 0.0f, 0.55f, 4.25f, 0.55f,
     StartFinish::Steel, true},
    {"terminal canopy column centre west", {-60.0f, 151.0f}, 0.0f, 0.55f, 4.25f, 0.55f,
     StartFinish::Steel, true},
    {"terminal canopy column centre east", {-10.0f, 151.0f}, 0.0f, 0.55f, 4.25f, 0.55f,
     StartFinish::Steel, true},
    {"terminal canopy column east inner", {16.0f, 151.0f}, 0.0f, 0.55f, 4.25f, 0.55f,
     StartFinish::Steel, true},
    {"terminal canopy column east outer", {64.0f, 151.0f}, 0.0f, 0.55f, 4.25f, 0.55f,
     StartFinish::Steel, true},
};

inline constexpr std::size_t kAirportCorePartCount =
    sizeof(kAirportCoreParts) / sizeof(kAirportCoreParts[0]);

inline constexpr BuildingPlan kAirportPlan{
    "Pinatty International Airport", nullptr, 0, nullptr, 0,
    kAirportCoreParts, kAirportCorePartCount, nullptr, 0};

// Human-scale details sit against the facade, leaving the outer 3.4 m of
// plaza open and keeping all three five-metre door/crosswalk axes clear.
inline void append_airport_terminal_details(std::vector<StartPart>& parts) {
    // Restrained vertical fins break long glass walls into structural bays.
    // Starting above the entrance heads keeps the doors visually unbroken.
    struct FacadeRun { float first_x; int bays; float z; float height; };
    for (const FacadeRun run : {
             FacadeRun{-142.0f, 9, 146.34f, 5.0f},
             FacadeRun{-69.0f, 9, 148.34f, 10.8f},
             FacadeRun{11.0f, 9, 146.34f, 6.5f}}) {
        for (int i=0;i<run.bays;++i) {
            parts.push_back({"terminal architectural facade fin",
                             {run.first_x+8.0f*static_cast<float>(i),run.z},4.90f,
                             .22f,run.height,.28f,StartFinish::White,false});
        }
    }
    // Small repeated light housings reinforce the three entrances at night
    // without claiming an independent light source or adding road obstacles.
    for (const float x : {-110.0f,-35.0f,40.0f}) {
        for (const float dx : {-18.0f,0.0f,18.0f}) {
            parts.push_back({"terminal canopy downlight housing",{x+dx,150.9f},
                             4.10f,1.6f,.12f,.46f,StartFinish::Steel,false});
            parts.push_back({"terminal canopy downlight lens",{x+dx,150.9f},
                             4.04f,1.25f,.06f,.31f,StartFinish::White,false});
        }
    }
    for (const float x : {-126.0f,-94.0f,-51.0f,-19.0f,24.0f,56.0f}) {
        parts.push_back({"terminal furnishing bench seat",{x,149.55f},
                         .72f,3.6f,.13f,.70f,StartFinish::Steel,true});
        parts.push_back({"terminal furnishing bench back",{x,149.23f},
                         .83f,3.6f,.69f,.10f,StartFinish::Steel,true});
        for (const float dx : {-1.38f,1.38f}) {
            parts.push_back({"terminal bench support",{x+dx,149.55f},
                             .30f,.13f,.42f,.60f,StartFinish::Steel,false});
            parts.push_back({"terminal bench arm",{x+dx,149.55f},
                             .85f,.10f,.25f,.56f,StartFinish::Steel,false});
        }
    }
    for (const float x : {-89.0f,-14.0f,61.0f}) {
        parts.push_back({"terminal furnishing litter bin",{x,149.5f},
                         .30f,.76f,1.00f,.66f,StartFinish::Steel,true});
        parts.push_back({"terminal litter bin aperture",{x,149.843f},
                         1.02f,.52f,.18f,.025f,StartFinish::DarkRoof,false});
        parts.push_back({"terminal litter bin lid",{x,149.5f},
                         1.30f,.84f,.10f,.74f,StartFinish::TealDoor,false});
    }
    for (const float x : {-80.0f,10.0f}) {
        // Open U-shaped luggage corral with three parked trolley frames.
        // The apron-facing mouth remains open; the solid rails stay behind it.
        for (const float dx : {-1.70f,1.70f}) {
            parts.push_back({"terminal furnishing trolley corral rail",{x+dx,149.50f},
                             .70f,.08f,.10f,1.50f,StartFinish::Steel,true});
            parts.push_back({"terminal trolley corral foot",{x+dx,148.85f},
                             .30f,.12f,.70f,.12f,StartFinish::Steel,true});
        }
        parts.push_back({"terminal furnishing trolley corral back",{x,148.80f},
                         .70f,3.4f,.10f,.08f,StartFinish::Steel,true});
        for (const float dx : {-1.05f,0.0f,1.05f}) {
            parts.push_back({"terminal furnishing trolley basket floor",{x+dx,149.30f},
                             .70f,.70f,.055f,.76f,StartFinish::Steel,false});
            for (const float side : {-1.0f,1.0f}) {
                parts.push_back({"terminal furnishing trolley basket side",{x+dx+side*.33f,149.30f},
                                 .755f,.04f,.225f,.76f,StartFinish::Steel,false});
                parts.push_back({"terminal furnishing trolley basket end",{x+dx,149.30f+side*.36f},
                                 .755f,.62f,.225f,.04f,StartFinish::Steel,false});
            }
            parts.push_back({"terminal trolley handle",{x+dx,149.71f},
                             1.07f,.72f,.07f,.08f,StartFinish::TealDoor,false});
            for (const float side : {-1.0f,1.0f}) {
                parts.push_back({"terminal trolley upright",{x+dx+side*.31f,149.65f},
                                 .43f,.055f,.66f,.055f,StartFinish::Steel,false});
                parts.push_back({"terminal trolley caster",{x+dx+side*.27f,149.35f},
                                 .30f,.13f,.16f,.21f,StartFinish::DarkRoof,false});
            }
        }
    }
    // One directory stands between the arrivals and main departure doors.
    // Its generated arrows match the layout: arrivals west, doors forward,
    // taxi/bus waiting east. The sign faces local +Z toward the pickup lane.
    parts.push_back({"terminal directory plinth",{-48.0f,149.45f},
                     .30f,1.95f,.18f,.80f,StartFinish::Concrete,true});
    parts.push_back({"terminal directory backing",{-48.0f,149.45f},
                     .48f,1.76f,2.62f,.24f,StartFinish::TealDoor,true});
    parts.push_back({"terminal directory sign face",{-48.0f,149.583f},
                     .59f,1.60f,2.40f,.02f,StartFinish::White,false});
}

inline std::vector<StartPart> bake_airport() {
    std::vector<StartPart> parts = bake_building(kAirportPlan);
    parts.insert(parts.end(), kAirportDevelopmentParts,
                 kAirportDevelopmentParts + kAirportDevelopmentPartCount);
    parts.reserve(parts.size() + 280u);

    // Seventeen long centre dashes keep the runway readable from the tower and
    // at 200 km/h without turning it into one solid stripe.
    for (int i = 0; i < 17; ++i) {
        parts.push_back({"runway centreline dash", {-360.0f + static_cast<float>(i) * 45.0f, -94.0f},
                         kAirportPavingTopM, 22.0f, kAirportPaintLiftM, 0.72f,
                         StartFinish::White, false});
    }

    // Threshold piano keys and touchdown bars at both ends.
    for (int end = -1; end <= 1; end += 2) {
        for (int row = 0; row < 6; ++row) {
            parts.push_back({"runway threshold bar",
                             {static_cast<float>(end) * 410.0f,
                              -111.5f + static_cast<float>(row) * 7.0f},
                             kAirportPavingTopM, 28.0f, kAirportPaintLiftM, 2.0f,
                             StartFinish::White, false});
        }
        for (int side = -1; side <= 1; side += 2) {
            parts.push_back({"runway touchdown bar",
                             {static_cast<float>(end) * 335.0f,
                              -94.0f + static_cast<float>(side) * 10.0f},
                             kAirportPavingTopM, 28.0f, kAirportPaintLiftM, 1.4f,
                             StartFinish::White, false});
        }
    }

    // Edge lights and a taxiway centreline are modeled, not painted into the
    // generated texture, so their spacing remains crisp and deterministic.
    for (int i = 0; i < 19; ++i) {
        const float x = -432.0f + static_cast<float>(i) * 48.0f;
        parts.push_back({"runway edge light north", {x, -120.0f}, 0.12f,
                         0.38f, 0.28f, 0.38f, StartFinish::White, false});
        parts.push_back({"runway edge light south", {x, -68.0f}, 0.12f,
                         0.38f, 0.28f, 0.38f, StartFinish::White, false});
    }
    for (int i = 0; i < 6; ++i) {
        const float z = -66.0f + static_cast<float>(i) * 24.0f;
        parts.push_back({"taxiway alpha centreline", {315.0f, z},
                         kAirportPavingTopM, 0.32f, kAirportPaintLiftM, 12.0f,
                         StartFinish::Yellow, false});
        parts.push_back({"taxiway bravo centreline", {-300.0f, z},
                         kAirportPavingTopM, 0.32f, kAirportPaintLiftM, 12.0f,
                         StartFinish::Yellow, false});
    }

    // Hold-short pairs and edge lights make Alpha and Bravo read as aircraft
    // routes, not narrow access roads. They sit south of the runway shoulder
    // and leave a full aircraft-length pause before entering 09-27.
    for (const float x : {-300.0f, 315.0f}) {
        parts.push_back({"taxiway hold short outer", {x, -61.0f},
                         kAirportPavingTopM, 20.0f, kAirportPaintLiftM, 0.32f,
                         StartFinish::Yellow, false});
        parts.push_back({"taxiway hold short inner", {x, -58.8f},
                         kAirportPavingTopM, 20.0f, kAirportPaintLiftM, 0.32f,
                         StartFinish::Yellow, false});
        for (const float z : {-52.0f, -28.0f, -4.0f}) {
            parts.push_back({"taxiway edge light west", {x - 11.0f, z},
                             0.12f, 0.34f, 0.30f, 0.34f,
                             StartFinish::Yellow, false});
            parts.push_back({"taxiway edge light east", {x + 11.0f, z},
                             0.12f, 0.34f, 0.30f, 0.34f,
                             StartFinish::Yellow, false});
        }
    }

    // Each bridge now terminates at a marked stand. Gate 1's line remains
    // visible under the parked aircraft; Gates 2 and 3 no longer end on blank
    // concrete after the tower was moved out of their way.
    for (const float x : {-125.0f, -35.0f, 55.0f}) {
        parts.push_back({"gate stand lead line", {x, 4.0f},
                         kAirportPavingTopM, 0.30f, kAirportPaintLiftM, 42.0f,
                         StartFinish::Yellow, false});
        parts.push_back({"gate stand stop bar", {x, 24.0f},
                         kAirportPavingTopM, 8.0f, kAirportPaintLiftM, 0.35f,
                         StartFinish::Yellow, false});
    }

    // One compact ground-service group gives the empty stands operational
    // scale without filling the apron with unique vehicle meshes.
    parts.push_back({"baggage tug body", {8.0f, 4.0f}, 0.35f,
                     4.2f, 1.35f, 2.4f, StartFinish::Yellow, false});
    parts.push_back({"baggage tug cab", {6.8f, 4.0f}, 1.25f,
                     1.8f, 1.35f, 2.2f, StartFinish::White, false});
    for (int i = 0; i < 3; ++i) {
        parts.push_back({"baggage cart", {14.0f + static_cast<float>(i) * 5.0f, 4.0f},
                         0.35f, 3.8f, 1.15f, 2.0f,
                         StartFinish::Steel, false});
    }

    // Sparse solid posts carry the long non-solid perimeter rails. Vehicles
    // read a continuous fence, while collision stays forgiving enough that a
    // glancing chase does not snag on hundreds of tiny boxes.
    for (const float z : {-155.0f, -113.0f, -71.0f}) {
        parts.push_back({"airport perimeter fence post west", {-435.0f, z},
                         0.0f, 0.30f, 2.4f, 0.30f,
                         StartFinish::Steel, true});
    }
    for (const float x : {-350.0f, -250.0f, -150.0f, -50.0f, 50.0f,
                          150.0f, 250.0f, 350.0f, 440.0f}) {
        parts.push_back({"airport perimeter fence post north", {x, -155.0f},
                         0.0f, 0.30f, 2.4f, 0.30f,
                         StartFinish::Steel, true});
    }
    parts.push_back({"airport perimeter fence post corner", {460.0f, -135.0f},
                     0.0f, 0.30f, 2.4f, 0.30f,
                     StartFinish::Steel, true});
    for (const float z : {-115.0f, -70.0f, -20.0f, 30.0f, 80.0f,
                          130.0f, 180.0f, 240.0f}) {
        parts.push_back({"airport perimeter fence post east", {480.0f, z},
                         0.0f, 0.30f, 2.4f, 0.30f,
                         StartFinish::Steel, true});
    }

    // Three zebra crossings line up with the three terminal doors. They cross
    // the full 12 m dropoff carriageway and stop at its sidewalks, so the
    // parking field is connected to the building instead of being an asphalt
    // island pedestrians could never plausibly reach.
    for (const float x : {-110.0f, -35.0f, 40.0f}) {
        for (int stripe = 0; stripe < 7; ++stripe) {
            parts.push_back({"terminal zebra stripe",
                             {x, 154.75f + static_cast<float>(stripe) * 1.75f},
                             DRAPE_EPS_M, 5.2f, kAirportCrosswalkLiftM, 0.78f,
                             StartFinish::White, false});
        }

        // Finish the walk from the road crossing, through the refuge island,
        // and onto the parking slab. These two short slabs close the 10 m and
        // 8 m gaps the zebra alone could not explain.
        parts.push_back({"terminal pedestrian connector north", {x, 170.5f},
                         0.12f, 5.2f, 0.18f, 9.0f,
                         StartFinish::Concrete, false});
        parts.push_back({"terminal pedestrian connector south", {x, 185.0f},
                         0.12f, 5.2f, 0.18f, 8.0f,
                         StartFinish::Concrete, false});

        // Continue to the far parking row. Only paint crosses the internal
        // driving aisle; no raised concrete or bollard blocks a vehicle.
        parts.push_back({"terminal parking walk north", {x, 200.0f},
                         0.12f, 5.2f, 0.18f, 22.0f,
                         StartFinish::Concrete, false});
        parts.push_back({"terminal parking walk south", {x, 229.5f},
                         0.12f, 5.2f, 0.18f, 21.0f,
                         StartFinish::Concrete, false});
        for (int stripe = 0; stripe < 5; ++stripe) {
            parts.push_back({"terminal parking crossing stripe",
                             {x, 211.8f + static_cast<float>(stripe) * 1.6f},
                             DRAPE_EPS_M, 5.2f, kAirportCrosswalkLiftM, 0.70f,
                             StartFinish::White, false});
        }
    }

    // Two marked rows flank a generous central parking aisle. The cadence is
    // intentionally visible from a chase camera; an unmarked 330 m rectangle
    // looked like leftover airport apron rather than public parking.
    for (int row = 0; row < 2; ++row) {
        const float z = row == 0 ? 196.0f : 233.0f;
        const int count = row == 0 ? 50 : 35;
        const float start_x = row == 0 ? -197.0f : -147.0f;
        for (int i = 0; i < count; ++i) {
            const float x = start_x + static_cast<float>(i) * 6.6f;
            bool pedestrian_space = false;
            for (const float crossing : {-110.0f, -35.0f, 40.0f}) {
                // Reserve a whole bay alongside each walking route, so the
                // painted bays do not invite parked cars across a footpath.
                pedestrian_space |= std::fabs(x - crossing) < 6.0f;
            }
            if (row == 0) {
                for (const float island : {-185.0f, -80.0f, -15.0f, 70.0f, 115.0f})
                    pedestrian_space |= std::fabs(x - island) < 5.0f;
            }
            if (pedestrian_space) continue;
            parts.push_back({"terminal parking stripe",
                             {x, z},
                             kAirportPavingTopM, 0.12f, kAirportPaintLiftM, 9.0f,
                             StartFinish::White, false});
        }
    }

    // Bollards protect each entrance without forming an unbroken fence.
    for (const float centre_x : {-110.0f, -35.0f, 40.0f}) {
        for (int i = -2; i <= 2; ++i) {
            if (i == 0) continue; // the crossing is a route, not a bollard bay
            parts.push_back({"terminal dropoff bollard",
                             {centre_x + static_cast<float>(i) * 6.0f, 152.3f},
                             0.0f, 0.55f, 1.05f, 0.55f,
                             StartFinish::Yellow, true});
        }
    }

    // Tall, simple landside light standards give the parking and splitter
    // islands a repeatable scale even before the lighting system grows a full
    // airport fixture rig.
    for (const float x : {-92.0f, -52.0f, 22.0f, 115.0f}) {
        parts.push_back({"terminal light pole", {x, 178.0f}, 0.0f,
                         0.38f, 8.5f, 0.38f, StartFinish::Steel, true});
        parts.push_back({"terminal light head", {x, 178.0f}, 8.35f,
                         2.8f, 0.35f, 0.75f, StartFinish::White, false});
    }

    // Hotel visitor bays sit along the public edge, outside the courtyard's
    // arrival drive. Rental bays face the customer hall and keep stored cars
    // out of the cargo truck lane.
    for (int i = 0; i < 16; ++i) {
        if (std::fabs((-402.0f + static_cast<float>(i) * 6.9f) - (-324.0f)) < 5.0f) continue;
        parts.push_back({"airport hotel parking stripe",
                         {-402.0f + static_cast<float>(i) * 6.9f, 220.0f},
                         kAirportPavingTopM, 0.12f, kAirportPaintLiftM, 9.0f,
                         StartFinish::White, false});
    }
    for (int i = 0; i < 9; ++i) {
        parts.push_back({"rental car parking stripe",
                         {207.0f + static_cast<float>(i) * 6.0f, 238.0f},
                         kAirportPavingTopM, 0.12f, kAirportPaintLiftM, 8.0f,
                         StartFinish::White, false});
    }
    append_airport_terminal_details(parts);
    return parts;
}

static_assert(valid_start_parts(kAirportCoreParts, kAirportCorePartCount),
              "the airport contains an invalid authored part");

}  // namespace city
}  // namespace apricot
