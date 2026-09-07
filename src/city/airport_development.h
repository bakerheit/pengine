#pragma once

#include <cstddef>

#include "city/start_area.h"

namespace apricot {
namespace city {

// The airport's landside edge is a small district, not leftover grass. These
// parcels keep public, ground-transport, and freight uses separate while the
// terminal loop and airside remain clear. Coordinates are local to
// kAirportSite, just like the airport proper.
struct AirportDevelopmentParcel {
    const char* name = nullptr;
    Vec2 centre{};
    float width_m = 0.0f;
    float depth_m = 0.0f;
    float max_height_m = 0.0f;
};

inline constexpr AirportDevelopmentParcel kAirportDevelopmentParcels[] = {
    {"Camber Gateway hotel parcel", {-350.0f, 163.0f}, 150.0f, 130.0f, 14.0f},
    {"O'Haven rental centre parcel", {260.0f, 216.0f}, 136.0f, 80.0f, 10.0f},
    {"Camber air cargo parcel", {370.0f, 193.0f}, 82.0f, 72.0f, 10.0f},
};

inline constexpr std::size_t kAirportDevelopmentParcelCount =
    sizeof(kAirportDevelopmentParcels) /
    sizeof(kAirportDevelopmentParcels[0]);

inline constexpr StartPart kAirportDevelopmentParts[] = {
    // West/public precinct. A U-shaped hotel opens toward its arrival drive.
    // Its west edge remains east of runway 09's threshold, and the low wings
    // step down toward the field rather than forming another terminal wall.
    {"airport hotel lot", {-350.0f, 163.0f}, 0.03f, 150.0f, 0.10f, 125.0f,
     StartFinish::Asphalt, false},
    {"airport hotel facade main wing", {-350.0f, 123.0f}, 0.0f,
     110.0f, 14.0f, 24.0f, StartFinish::Glass, true},
    {"airport hotel facade west wing", {-393.0f, 164.0f}, 0.0f,
     24.0f, 11.0f, 82.0f, StartFinish::Glass, true},
    {"airport hotel facade east wing", {-307.0f, 164.0f}, 0.0f,
     24.0f, 11.0f, 82.0f, StartFinish::Glass, true},
    {"airport hotel roof main wing", {-350.0f, 123.0f}, 14.0f,
     114.0f, 0.65f, 28.0f, StartFinish::White, false},
    {"airport hotel roof west wing", {-393.0f, 164.0f}, 11.0f,
     28.0f, 0.55f, 86.0f, StartFinish::White, false},
    {"airport hotel roof east wing", {-307.0f, 164.0f}, 11.0f,
     28.0f, 0.55f, 86.0f, StartFinish::White, false},
    {"airport hotel courtyard", {-350.0f, 164.0f}, 0.12f,
     58.0f, 0.18f, 50.0f, StartFinish::Concrete, false},
    {"airport hotel entrance walk", {-350.0f, 137.0f}, 0.12f,
     8.0f, 0.18f, 4.0f, StartFinish::Concrete, false},
    // The hotel approach ends at its parcel boundary. A separate public
    // connector continues across the verge to the frontage sidewalk; neither
    // segment crosses the driveway or either solid accommodation wing.
    {"airport hotel pedestrian approach", {-324.0f, 208.5f}, 0.12f,
     3.0f, 0.18f, 39.0f, StartFinish::Concrete, false},
    {"airport frontage pedestrian connector", {-324.0f, 234.5f}, 0.12f,
     3.0f, 0.18f, 13.0f, StartFinish::Concrete, false},
    {"airport hotel entrance", {-350.0f, 135.18f}, 0.08f,
     14.0f, 3.6f, 0.36f, StartFinish::TealDoor, false},
    {"airport hotel arrival canopy", {-350.0f, 143.0f}, 4.25f,
     42.0f, 0.45f, 15.0f, StartFinish::White, false},
    {"airport hotel canopy fascia", {-350.0f, 150.25f}, 3.82f,
     42.0f, 0.72f, 0.50f, StartFinish::TealDoor, false},
    {"airport hotel canopy column west", {-368.0f, 147.0f}, 0.0f,
     0.55f, 4.25f, 0.55f, StartFinish::Steel, true},
    {"airport hotel canopy column east", {-332.0f, 147.0f}, 0.0f,
     0.55f, 4.25f, 0.55f, StartFinish::Steel, true},
    {"airport hotel sign band", {-350.0f, 135.28f}, 9.4f,
     48.0f, 1.35f, 0.28f, StartFinish::TealDoor, false},
    {"airport hotel crown accent", {-350.0f, 135.43f}, 11.0f,
     30.0f, 0.24f, 0.14f, StartFinish::Yellow, false},
    {"airport hotel service court", {-412.0f, 216.0f}, 0.12f,
     24.0f, 0.18f, 24.0f, StartFinish::Concrete, false},

    // South-east/ground-transport precinct. The customer hall faces the
    // terminal, while the taller parking deck sits behind it by the ring road.
    {"rental car lot", {260.0f, 216.0f}, 0.03f,
     130.0f, 0.10f, 70.0f, StartFinish::Asphalt, false},
    {"rental centre customer hall", {225.0f, 190.0f}, 0.0f,
     62.0f, 8.0f, 24.0f, StartFinish::WarmWall, true},
    {"rental centre glass frontage", {225.0f, 202.18f}, 0.75f,
     42.0f, 4.8f, 0.36f, StartFinish::Glass, false},
    {"rental centre entrance", {225.0f, 202.39f}, 0.08f,
     9.0f, 3.4f, 0.18f, StartFinish::TealDoor, false},
    {"rental centre customer roof", {225.0f, 190.0f}, 8.0f,
     66.0f, 0.55f, 28.0f, StartFinish::White, false},
    {"rental centre parking deck", {298.0f, 216.0f}, 0.0f,
     48.0f, 10.0f, 48.0f, StartFinish::Steel, true},
    {"rental centre deck roof", {298.0f, 216.0f}, 10.0f,
     52.0f, 0.55f, 52.0f, StartFinish::White, false},
    {"rental garage door west", {282.0f, 240.18f}, 0.15f,
     12.0f, 4.5f, 0.36f, StartFinish::TealDoor, false},
    {"rental garage door centre", {298.0f, 240.18f}, 0.15f,
     12.0f, 4.5f, 0.36f, StartFinish::TealDoor, false},
    {"rental garage door east", {314.0f, 240.18f}, 0.15f,
     12.0f, 4.5f, 0.36f, StartFinish::TealDoor, false},
    // Rental Row runs at x=265 here. Keep the forecourt west of its 10 m
    // carriageway, with a short walk meeting the actual customer entrance.
    {"rental forecourt", {237.0f, 216.0f}, 0.12f,
     36.0f, 0.18f, 22.0f, StartFinish::Concrete, false},
    {"rental entrance walk", {225.0f, 203.5f}, 0.12f,
     6.0f, 0.18f, 3.0f, StartFinish::Concrete, false},

    // A taxi passenger can reach the east terminal crossing on a continuous
    // path behind the shelter, clear of its columns and waiting benches.
    {"airport taxi pedestrian link", {59.5f, 184.0f}, 0.12f,
     42.0f, 0.18f, 3.0f, StartFinish::Concrete, false},
    {"airport taxi shelter approach", {79.0f, 182.25f}, 0.12f,
     3.0f, 0.18f, 3.5f, StartFinish::Concrete, false},
    {"rental centre sign", {199.0f, 224.0f}, 0.0f,
     1.2f, 5.4f, 8.0f, StartFinish::TealDoor, true},
    {"rental centre sign cap", {199.0f, 224.0f}, 5.4f,
     1.5f, 0.5f, 8.4f, StartFinish::White, false},

    // East/service precinct. Freight gets its own alley and yard beside the
    // hangar, so trucks do not need to use the passenger pickup loop.
    {"air cargo yard", {370.0f, 193.0f}, 0.03f,
     80.0f, 0.10f, 70.0f, StartFinish::Asphalt, false},
    {"air cargo warehouse", {370.0f, 178.0f}, 0.0f,
     58.0f, 10.0f, 28.0f, StartFinish::Steel, true},
    {"air cargo warehouse roof", {370.0f, 178.0f}, 10.0f,
     62.0f, 0.65f, 32.0f, StartFinish::White, false},
    {"air cargo loading door west", {355.0f, 192.18f}, 0.15f,
     12.0f, 5.5f, 0.36f, StartFinish::TealDoor, false},
    {"air cargo loading door east", {375.0f, 192.18f}, 0.15f,
     12.0f, 5.5f, 0.36f, StartFinish::TealDoor, false},
    {"air cargo loading canopy", {365.0f, 198.0f}, 5.0f,
     42.0f, 0.45f, 12.0f, StartFinish::White, false},
    {"air cargo canopy column west", {347.0f, 202.0f}, 0.0f,
     0.55f, 5.0f, 0.55f, StartFinish::Steel, true},
    {"air cargo canopy column east", {383.0f, 202.0f}, 0.0f,
     0.55f, 5.0f, 0.55f, StartFinish::Steel, true},
    {"air cargo office", {342.0f, 219.0f}, 0.0f,
     22.0f, 6.0f, 16.0f, StartFinish::Brick, true},
    {"air cargo office glass", {342.0f, 227.18f}, 0.75f,
     12.0f, 3.2f, 0.36f, StartFinish::Glass, false},
    {"air cargo office roof", {342.0f, 219.0f}, 6.0f,
     25.0f, 0.5f, 19.0f, StartFinish::White, false},
    {"air cargo container red", {395.0f, 216.0f}, 0.0f,
     12.0f, 2.8f, 2.6f, StartFinish::RedTrim, true},
    {"air cargo container blue", {395.0f, 220.0f}, 0.0f,
     12.0f, 2.8f, 2.6f, StartFinish::TealDoor, true},
    {"air cargo container white", {395.0f, 224.0f}, 0.0f,
     12.0f, 2.8f, 2.6f, StartFinish::White, true},

    // The parkway arrival pylon sits north of the frontage road and outside
    // the cargo parcel. It is the first airport-scale object drivers see after
    // rounding the east perimeter, before the terminal crown takes over. Its
    // broad face points east into the arriving driver's sightline; the first
    // version faced north and read as a skinny green pole from the Parkway.
    {"airport arrival pylon base", {430.0f, 242.0f}, 0.0f,
     3.0f, 1.0f, 10.0f, StartFinish::Concrete, true},
    {"airport arrival pylon body", {430.0f, 242.0f}, 1.0f,
     2.0f, 14.0f, 7.0f, StartFinish::TealDoor, true},
    {"airport arrival pylon face", {431.15f, 242.0f}, 5.0f,
     0.30f, 7.0f, 6.4f, StartFinish::White, false},
    {"airport arrival pylon accent", {431.17f, 242.0f}, 12.5f,
     0.34f, 0.55f, 6.8f, StartFinish::Yellow, false},
    {"airport arrival pylon cap", {430.0f, 242.0f}, 15.0f,
     3.0f, 0.70f, 9.0f, StartFinish::White, false},

    // West service checkpoint. The visitor-return split is west of this
    // canopy; only vehicles continuing east reach the guardhouse, lane arms,
    // and the short fence wings that visually close the airside boundary.
    {"airport security gatehouse", {-455.0f, 24.0f}, 0.0f,
     12.0f, 4.8f, 8.0f, StartFinish::WarmWall, true},
    {"airport security gatehouse roof", {-455.0f, 24.0f}, 4.8f,
     14.0f, 0.45f, 10.0f, StartFinish::White, false},
    {"airport security window", {-455.0f, 28.18f}, 1.05f,
     7.0f, 2.2f, 0.36f, StartFinish::Glass, false},
    {"airport security door", {-450.4f, 28.39f}, 0.08f,
     2.2f, 3.1f, 0.18f, StartFinish::TealDoor, false},
    {"airport security canopy", {-450.0f, 33.0f}, 4.65f,
     40.0f, 0.50f, 18.0f, StartFinish::White, false},
    {"airport security canopy fascia", {-450.0f, 41.75f}, 4.18f,
     40.0f, 0.78f, 0.50f, StartFinish::TealDoor, false},
    {"airport security canopy column nw", {-466.0f, 25.5f}, 0.0f,
     0.55f, 4.65f, 0.55f, StartFinish::Steel, true},
    {"airport security canopy column ne", {-438.0f, 25.5f}, 0.0f,
     0.55f, 4.65f, 0.55f, StartFinish::Steel, true},
    {"airport security canopy column sw", {-466.0f, 40.5f}, 0.0f,
     0.55f, 4.65f, 0.55f, StartFinish::Steel, true},
    {"airport security canopy column se", {-438.0f, 40.5f}, 0.0f,
     0.55f, 4.65f, 0.55f, StartFinish::Steel, true},
    {"airport security lane island", {-450.0f, 33.0f}, 0.12f,
     30.0f, 0.18f, 0.80f, StartFinish::Concrete, false},
    {"airport security bollard inbound", {-435.0f, 28.7f}, 0.0f,
     0.45f, 1.25f, 0.45f, StartFinish::Yellow, true},
    {"airport security barrier inbound", {-435.0f, 31.0f}, 1.05f,
     0.32f, 0.24f, 4.0f, StartFinish::RedTrim, false},
    {"airport security bollard outbound", {-435.0f, 37.3f}, 0.0f,
     0.45f, 1.25f, 0.45f, StartFinish::Yellow, true},
    {"airport security barrier outbound", {-435.0f, 35.0f}, 1.05f,
     0.32f, 0.24f, 4.0f, StartFinish::RedTrim, false},
    {"airport security authorised sign", {-455.0f, 28.31f}, 3.55f,
     9.0f, 0.80f, 0.24f, StartFinish::TealDoor, false},
    {"airport visitor return sign", {-484.0f, 21.0f}, 0.0f,
     0.55f, 4.4f, 7.5f, StartFinish::TealDoor, true},
    {"airport visitor return sign cap", {-484.0f, 21.0f}, 4.4f,
     0.80f, 0.45f, 8.0f, StartFinish::White, false},

    // Sparse post-and-rail wings read as airport fencing without putting an
    // opaque wall beside the runway approach.
    {"airport security fence north lower", {-435.0f, 0.0f}, 0.65f,
     0.18f, 0.10f, 58.0f, StartFinish::Steel, false},
    {"airport security fence north upper", {-435.0f, 0.0f}, 1.75f,
     0.18f, 0.10f, 58.0f, StartFinish::Steel, false},
    {"airport security fence south lower", {-435.0f, 78.0f}, 0.65f,
     0.18f, 0.10f, 76.0f, StartFinish::Steel, false},
    {"airport security fence south upper", {-435.0f, 78.0f}, 1.75f,
     0.18f, 0.10f, 76.0f, StartFinish::Steel, false},
    {"airport security fence post north outer", {-435.0f, -29.0f}, 0.0f,
     0.28f, 2.4f, 0.28f, StartFinish::Steel, true},
    {"airport security fence post north centre", {-435.0f, 0.0f}, 0.0f,
     0.28f, 2.4f, 0.28f, StartFinish::Steel, true},
    {"airport security fence post south gate", {-435.0f, 40.0f}, 0.0f,
     0.28f, 2.4f, 0.28f, StartFinish::Steel, true},
    {"airport security fence post south centre", {-435.0f, 78.0f}, 0.0f,
     0.28f, 2.4f, 0.28f, StartFinish::Steel, true},
    {"airport security fence post south outer", {-435.0f, 116.0f}, 0.0f,
     0.28f, 2.4f, 0.28f, StartFinish::Steel, true},

    // Continue the visual/collision boundary around the north and east field.
    // The east line has a 30 m gap where Cargo Service crosses at a controlled
    // gate, rather than leaving the entire grass edge open to the public road.
    {"airport perimeter fence west north lower", {-435.0f, -92.0f}, 0.65f,
     0.18f, 0.10f, 126.0f, StartFinish::Steel, false},
    {"airport perimeter fence west north upper", {-435.0f, -92.0f}, 1.75f,
     0.18f, 0.10f, 126.0f, StartFinish::Steel, false},
    {"airport perimeter fence north lower", {2.5f, -155.0f}, 0.65f,
     875.0f, 0.10f, 0.18f, StartFinish::Steel, false},
    {"airport perimeter fence north upper", {2.5f, -155.0f}, 1.75f,
     875.0f, 0.10f, 0.18f, StartFinish::Steel, false},
    {"airport perimeter fence corner lower", {460.0f, -135.0f}, 0.65f,
     56.6f, 0.10f, 0.18f, StartFinish::Steel, false, 0.0f, 45.0f},
    {"airport perimeter fence corner upper", {460.0f, -135.0f}, 1.75f,
     56.6f, 0.10f, 0.18f, StartFinish::Steel, false, 0.0f, 45.0f},
    {"airport perimeter fence east north lower", {480.0f, 35.0f}, 0.65f,
     0.18f, 0.10f, 300.0f, StartFinish::Steel, false},
    {"airport perimeter fence east north upper", {480.0f, 35.0f}, 1.75f,
     0.18f, 0.10f, 300.0f, StartFinish::Steel, false},
    {"airport perimeter fence east south lower", {480.0f, 227.5f}, 0.65f,
     0.18f, 0.10f, 25.0f, StartFinish::Steel, false},
    {"airport perimeter fence east south upper", {480.0f, 227.5f}, 1.75f,
     0.18f, 0.10f, 25.0f, StartFinish::Steel, false},
    {"airport cargo gate post north", {480.0f, 185.0f}, 0.0f,
     0.40f, 3.2f, 0.40f, StartFinish::Yellow, true},
    {"airport cargo gate post south", {480.0f, 215.0f}, 0.0f,
     0.40f, 3.2f, 0.40f, StartFinish::Yellow, true},
    {"airport cargo gate sign", {480.0f, 218.0f}, 1.0f,
     0.35f, 3.0f, 7.0f, StartFinish::TealDoor, false},
};

inline constexpr std::size_t kAirportDevelopmentPartCount =
    sizeof(kAirportDevelopmentParts) /
    sizeof(kAirportDevelopmentParts[0]);

static_assert(valid_start_parts(kAirportDevelopmentParts,
                                kAirportDevelopmentPartCount),
              "the airport development contains an invalid authored part");

}  // namespace city
}  // namespace apricot
